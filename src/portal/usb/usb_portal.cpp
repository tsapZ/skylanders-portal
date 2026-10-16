#include "portal/usb/usb_portal.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <string_view>

#include <rex/logging.h>
#include <rex/string/utf8.h>

namespace skylanders::portal {

namespace {


// hid_error() returns a UTF-16 string on Windows (wchar_t and char16_t are the same width there,
// just distinct types) -- narrow it for REXLOG, which expects UTF-8.
std::string HidErrorUtf8(hid_device* device) {
  const wchar_t* message = hid_error(device);
  if (message == nullptr) return "(no error message)";
  return rex::string::to_utf8(std::u16string_view(reinterpret_cast<const char16_t*>(message)));
}

std::string HexBytes(const uint8_t* data, size_t n) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(n * 3);
  for (size_t i = 0; i < n; ++i) {
    if (i) out += ' ';
    out += kHex[data[i] >> 4];
    out += kHex[data[i] & 0xF];
  }
  return out;
}

}  // namespace

UsbPortal::UsbPortal() {
  if (hid_init() != 0) return;
  for (const auto& [vendor_id, product_id] : kKnownPortals) {
    device_ = hid_open(vendor_id, product_id, nullptr);
    if (device_ != nullptr) break;
  }
  // Deliberately never call hid_exit(): it finalizes the whole hidapi library, not just this
  // device, and this portal lives for the whole process (see portal_rex.cpp's InstallPortal,
  // which never frees its SoftwarePortal either, for the same reason). Process exit cleans this up.
}

UsbPortal::~UsbPortal() {
  audio_stopping_.store(true);
  audio_queue_.Close();
  if (audio_thread_.joinable()) audio_thread_.join();
  if (device_ != nullptr) hid_close(device_);
}

void UsbPortal::SendRaw(const Report& report) {
  if (device_ == nullptr) return;
  // hid_send_output_report() sends via a HID SET_REPORT control transfer -- unlike hid_write(),
  // which prefers this device's interrupt OUT endpoint and is accepted at the transport level but
  // silently discarded by its firmware. Needs a leading report-ID byte (0, this device has no
  // numbered reports), so the buffer is one longer than the report itself.
  std::array<uint8_t, kReportSize + 1> buffer{};  // buffer[0] = report ID 0
  std::copy(report.begin(), report.end(), buffer.begin() + 1);
  const int written = hid_send_output_report(device_, buffer.data(), buffer.size());
  if (written < 0) {
    consecutive_errors_.fetch_add(1);
    if (!write_error_logged_.exchange(true)) {
      REXLOG_WARN("Portal (usb): hid_send_output_report failed: {}", HidErrorUtf8(device_));
    }
  } else {
    consecutive_errors_.store(0);
  }
}

Report UsbPortal::ReceiveRaw() {
  if (device_ == nullptr) return Report{};
  Report report{};
  // The game polls the read side "continuously (tens of times a second) regardless of whether a
  // portal answers" (docs/portal-protocol.md), synchronously on whatever thread calls into the
  // portal hook -- so every idle poll blocks that thread for however long this timeout is. 1ms is
  // enough for a real device's normal (low-single-digit-ms) reply latency, without making an idle
  // poll itself the bottleneck the way a 50ms timeout did; a genuinely dead device still returns
  // (an all-zero Report on timeout/failure is a shape the game already tolerates), just faster.
  const int bytes_read = hid_read_timeout(device_, report.data(), report.size(), 1);
  // 0 means "no report within the timeout", hidapi's normal outcome for an idle poll, not a
  // failure -- resets consecutive_errors_ same as a real read, since it proves the transport
  // itself is still working. -1 is the actual error indicator.
  if (bytes_read < 0) {
    consecutive_errors_.fetch_add(1);
    if (!read_error_logged_.exchange(true)) {
      REXLOG_WARN("Portal (usb): hid_read failed: {}", HidErrorUtf8(device_));
    }
  } else {
    consecutive_errors_.store(0);
  }
  // Swap Force rejects Spyro's Adventure/Giants portals ("Wrong Portal version") by the ID in the
  // ready reply (52 01 xx). Those portals otherwise work, so report the ID SoftwarePortal answers
  // with (52 02 1B), which the game accepts. Newer portals are passed through unchanged.
  if (bytes_read > 0 && report[0] == 0x52 && report[1] == 0x01) {
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("Portal (usb): old portal (ready reply 52 01 {:02X}), reporting 52 02 1B", report[2]);
    }
    report[1] = 0x02;
    report[2] = 0x1B;
  }
  return bytes_read > 0 ? report : Report{};
}

void UsbPortal::Write(const Report& report) {
  std::lock_guard<std::mutex> lock(io_mutex_);
  // A dump in progress owns the device exclusively; the game's own writes are dropped rather than
  // reaching real hardware (see DumpFigure's comment).
  if (dumping_slot_.load() >= 0) return;
  if (report[0] == 0x57) {  // 'W': write one figure block -- log for offset research.
    const int slot = report[1] & 0x0F;
    const int block = report[2];
    REXLOG_TRACE("Portal figure research: usb slot {} block {} write -> {}", slot, block,
                 HexBytes(&report[3], kBlockSize));
  }
  SendRaw(report);
}

Report UsbPortal::Read() {
  std::lock_guard<std::mutex> lock(io_mutex_);
  const int dumping = dumping_slot_.load();
  if (dumping >= 0) {
    // Fake a status frame reporting `dumping` as empty, so the game sees a clean removal instead
    // of the portal simply going silent -- other slots keep reporting whatever they last did.
    Report synthetic{};
    synthetic[0] = 0x53;
    uint32_t states = 0;
    for (int s = 0; s < kMaxFigures; ++s) {
      if (s != dumping && slot_present_[s].load()) states |= (0x1u << (2 * s));
    }
    synthetic[1] = static_cast<uint8_t>(states);
    synthetic[2] = static_cast<uint8_t>(states >> 8);
    synthetic[3] = static_cast<uint8_t>(states >> 16);
    synthetic[4] = static_cast<uint8_t>(states >> 24);
    synthetic[5] = status_counter_.fetch_add(1);
    synthetic[6] = last_active_.load() ? 1 : 0;
    ObserveReply(synthetic);  // keeps PresentSlots()/the passive cache consistent with this
    return synthetic;
  }
  // The device streams status frames faster than Swap Force polls (~10 Hz), so
  // the HID input queue grows and command replies reach the game seconds late; it then resets the
  // portal every ~20 s and never reads a figure. Drain what is queued: command replies are passed
  // on in order, status frames collapse to the newest one, keeping a slot's "added" state (3) if a
  // dropped frame had it. All state below is guarded by io_mutex_.
  static std::deque<Report> pending_replies;
  static Report newest_status{};
  static bool have_status = false;
  static uint32_t added_mask = 0;  // bit per slot: a dropped frame reported "added"
  for (int i = 0; i < 64; ++i) {
    Report r = ReceiveRaw();
    if (r[0] == 0) break;
    if (r[0] != 0x53) {
      pending_replies.push_back(r);
      continue;
    }
    const uint32_t states = static_cast<uint32_t>(r[1]) | (static_cast<uint32_t>(r[2]) << 8) |
                            (static_cast<uint32_t>(r[3]) << 16) | (static_cast<uint32_t>(r[4]) << 24);
    for (int slot = 0; slot < kMaxFigures; ++slot) {
      if (((states >> (2 * slot)) & 0x03) == 0x03) added_mask |= 1u << slot;
    }
    newest_status = r;
    have_status = true;
  }
  Report report{};
  if (!pending_replies.empty()) {
    report = pending_replies.front();
    pending_replies.pop_front();
  } else if (have_status) {
    report = newest_status;
    have_status = false;
    uint32_t states = static_cast<uint32_t>(report[1]) | (static_cast<uint32_t>(report[2]) << 8) |
                      (static_cast<uint32_t>(report[3]) << 16) | (static_cast<uint32_t>(report[4]) << 24);
    for (int slot = 0; slot < kMaxFigures; ++slot) {
      if ((added_mask >> slot & 1u) && ((states >> (2 * slot)) & 0x03) == 0x01) {
        states |= 0x03u << (2 * slot);  // present -> added, so the game sees the new figure
      }
    }
    added_mask = 0;
    report[1] = static_cast<uint8_t>(states);
    report[2] = static_cast<uint8_t>(states >> 8);
    report[3] = static_cast<uint8_t>(states >> 16);
    report[4] = static_cast<uint8_t>(states >> 24);
  } else {
    return report;  // nothing queued
  }
  ObserveReply(report);
  return report;
}

bool UsbPortal::FigurePresent() const {
  for (int slot = 0; slot < kMaxFigures; ++slot) {
    if (slot_present_[slot].load()) return true;
  }
  return false;
}

std::vector<int> UsbPortal::PresentSlots() const {
  std::vector<int> slots;
  for (int slot = 0; slot < kMaxFigures; ++slot) {
    if (slot_present_[slot].load()) slots.push_back(slot);
  }
  return slots;
}

std::optional<std::pair<uint16_t, uint16_t>> UsbPortal::DetectedIdVariant(int slot) const {
  if (slot < 0 || slot >= kMaxFigures) return std::nullopt;
  std::lock_guard<std::mutex> lock(detected_mutex_);
  return slot_id_variant_[slot];
}

void UsbPortal::ObserveReply(const Report& report) {
  // 'S' status frame: 0x53, then 4 bytes of little-endian slot state (2 bits each, slot 0
  // lowest -- up to kMaxFigures slots, matching a real portal holding more than one figure at
  // once, e.g. Giants' 2-player co-op plus items), a counter, and an active flag
  // (docs/portal-protocol.md).
  if (report[0] == 0x53) {
    const uint32_t states = static_cast<uint32_t>(report[1]) | (static_cast<uint32_t>(report[2]) << 8) |
                            (static_cast<uint32_t>(report[3]) << 16) |
                            (static_cast<uint32_t>(report[4]) << 24);
    last_active_.store(report[6] != 0);
    for (int slot = 0; slot < kMaxFigures; ++slot) {
      const bool present = ((states >> (2 * slot)) & 0x03) != 0;
      slot_present_[slot].store(present);
      if (!present) {
        {
          std::lock_guard<std::mutex> lock(detected_mutex_);
          slot_id_variant_[slot].reset();
        }
        std::lock_guard<std::mutex> lock(cache_mutex_);
        slot_cache_[slot] = FigureData{};
        slot_block_seen_[slot].fill(false);
      }
    }
    return;
  }
  if (report[0] == 0x51 && (report[1] & 0x10) != 0) {
    const int slot = report[1] & 0x0F;
    const int block = report[2];
    REXLOG_TRACE("Portal figure research: usb slot {} block {} read -> {}", slot, block,
                 HexBytes(&report[3], kBlockSize));
    if (block < static_cast<int>(kBlockCount)) {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      std::copy_n(report.begin() + 3, kBlockSize, slot_cache_[slot].begin() + block * kBlockSize);
      slot_block_seen_[slot][block] = true;
    }
  }
  // 'Q' reply to a block-1 read: 0x51, slot (low nibble) with 0x10 set if present, block index,
  // then the block's 16 data bytes. Block 1 covers global figure offsets 0x10-0x1F, where id
  // (offset 0x10) and variant (offset 0x1C) live -- see figure_file.h's
  // ReadFigureId/ReadFigureVariant, which read the same two fields from a full 1024-byte dump.
  if (report[0] == 0x51 && (report[1] & 0x10) != 0 && report[2] == 1) {
    const int slot = report[1] & 0x0F;  // always 0-15, matching kMaxFigures
    const uint16_t id = static_cast<uint16_t>(report[3]) | (static_cast<uint16_t>(report[4]) << 8);
    const uint16_t variant =
        static_cast<uint16_t>(report[15]) | (static_cast<uint16_t>(report[16]) << 8);
    std::lock_guard<std::mutex> lock(detected_mutex_);
    slot_id_variant_[slot] = std::make_pair(id, variant);
  }
}

std::optional<FigureData> UsbPortal::DumpFigure(int slot, std::string* error) {
  auto Fail = [&](std::string_view msg) -> std::optional<FigureData> {
    if (error) *error = std::string(msg);
    return std::nullopt;
  };
  if (slot < 0 || slot >= kMaxFigures) return Fail("invalid slot");
  if (device_ == nullptr) return Fail("no USB portal open");
  if (!slot_present_[slot].load()) return Fail("no figure in that slot");

  dumping_slot_.store(slot);
  FigureData data{};
  bool ok = true;
  {
    std::lock_guard<std::mutex> lock(io_mutex_);
    for (int block = 0; block < static_cast<int>(kBlockCount); ++block) {
      Report request{};
      request[0] = 0x51;  // 'Q'
      request[1] = static_cast<uint8_t>(slot & 0x0F);
      request[2] = static_cast<uint8_t>(block);
      SendRaw(request);

      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
      Report reply{};
      bool matched = false;
      while (std::chrono::steady_clock::now() < deadline) {
        reply = ReceiveRaw();
        if (reply[0] == 0) continue;  // timeout/no-report this poll; keep trying within the window
        if (reply[0] == 0x53) {
          // The device still reports its genuine state even though we're not forwarding it to
          // the game -- check the dumped slot's own bit to catch a physical removal mid-dump.
          const uint32_t states =
              static_cast<uint32_t>(reply[1]) | (static_cast<uint32_t>(reply[2]) << 8) |
              (static_cast<uint32_t>(reply[3]) << 16) | (static_cast<uint32_t>(reply[4]) << 24);
          const uint32_t slot_state = (states >> (2 * slot)) & 0x03;
          if (slot_state == 0 || slot_state == 2) {  // 0 empty, 2 removing
            ok = false;
            break;
          }
          continue;
        }
        if (reply[0] == 0x51 && (reply[1] & 0x10) != 0 && reply[2] == block) {
          matched = true;
          break;
        }
      }
      if (!ok) break;
      if (!matched) {
        ok = false;
        break;
      }
      std::copy_n(reply.begin() + 3, kBlockSize, data.begin() + block * kBlockSize);
    }
  }
  dumping_slot_.store(-1);

  if (!ok) return Fail("the figure was removed or stopped responding during the dump");
  return data;
}

std::optional<FigureData> UsbPortal::CachedFigureData(int slot) const {
  if (slot < 0 || slot >= kMaxFigures) return std::nullopt;
  // The AES key for every block is derived from blocks 0 and 1 (figure_stats.h), so those two
  // are needed before any of the rest can be decrypted; 0x08/0x0A/0x0C are the blocks
  // ParseFigureStats itself reads (area 0's gold/level, and the nickname).
  static constexpr int kNeededBlocks[] = {0x00, 0x01, 0x08, 0x0A, 0x0C};
  std::lock_guard<std::mutex> lock(cache_mutex_);
  for (int block : kNeededBlocks) {
    if (!slot_block_seen_[slot][block]) return std::nullopt;
  }
  return slot_cache_[slot];
}

void UsbPortal::WriteAudio(std::span<const int16_t> pcm) {
  if (!device_) return;
  std::call_once(audio_thread_started_,
                 [this] { audio_thread_ = std::thread([this] { AudioWriterLoop(); }); });
  // Never blocks the game thread: full packets go to the writer thread's bounded queue.
  audio_packetizer_.Add(pcm, [this](const AudioPacket& packet) { audio_queue_.Push(packet); });
}

// Speaker audio goes out on the interrupt OUT endpoint (hid_write), unlike commands, which the
// firmware only accepts as SET_REPORT (hid_send_output_report, see SendRaw). PopWait also returns
// nullopt on a timeout, so the loop only ends once the destructor sets audio_stopping_.
void UsbPortal::AudioWriterLoop() {
  for (;;) {
    auto packet = audio_queue_.PopWait(std::chrono::milliseconds(500));
    if (!packet) {
      if (audio_stopping_.load()) return;
      continue;
    }
    std::lock_guard<std::mutex> lock(io_mutex_);
    uint8_t out[1 + kAudioPacketSize];
    out[0] = 0;  // report ID
    std::memcpy(out + 1, packet->data(), kAudioPacketSize);
    if (hid_write(device_, out, sizeof(out)) < 0) REXLOG_WARN("Portal: speaker audio write failed");
  }
}

}  // namespace skylanders::portal
