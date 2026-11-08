#include "portal/software/software_portal.h"

#include <algorithm>
#include <system_error>
#include <initializer_list>

namespace skylanders::portal {

namespace {

// A new figure shows as "added" for this many status reports before it settles to "ready".
constexpr int kAddedReports = 8;

Report MakeReport(std::initializer_list<uint8_t> bytes) {
  Report r{};
  std::copy(bytes.begin(), bytes.end(), r.begin());
  return r;
}

}  // namespace

namespace {
struct WriteEvent {
  int slot;
  FigureData data;
  std::optional<std::filesystem::path> source;
};
}  // namespace

void SoftwarePortal::Write(const Report& in) {
  std::optional<WriteEvent> written;
  std::function<void(int, const FigureData&, const std::optional<std::filesystem::path>&)> callback;
  {
    std::lock_guard<std::mutex> lock(mu_);
    switch (in[0]) {
      case 'R':  // ready
        replies_.push_back(MakeReport({0x52, 0x02, 0x1B}));
        break;
      case 'A': {  // activate (argument != 0) or deactivate (argument == 0)
        const bool activate = in[1] != 0;
        if (activate && !active_) {
          // Figures are announced when the portal goes from inactive to active. The game repeats
          // 'A 01' about every 10 seconds while it is already active; announcing again then made
          // every figure look taken off and put straight back.
          for (Slot& s : slots_) {
            if (s.present) {
              s.state = SlotState::kAdded;
              s.reports_left = kAddedReports;
            }
          }
        } else if (!activate) {
          for (Slot& s : slots_) {
            if (s.swap_pending) ApplySwapLocked(s);  // nobody watches: the swap happens at once
            s.state = SlotState::kEmpty;  // figures stay; just not reported
          }
        }
        active_ = activate;
        replies_.push_back(MakeReport({0x41, in[1], 0xFF, 0x77}));
        break;
      }
      case 'M': {  // audio firmware version: says whether the portal has a speaker
        PortalAudioSink* sink = audio_sink_.load();
        replies_.push_back(sink ? MakeReport({0x4D, in[1], kAudioCapableVersion[0],
                                              kAudioCapableVersion[1]})
                                : MakeReport({0x4D, in[1], 0x00, 0x19}));
        break;
      }
      case 'J':  // Trap Team portal sync: acknowledged with a bare 4A report
        replies_.push_back(MakeReport({0x4A}));
        break;
      case 'Q': {  // read one block of a figure
        const uint8_t slot = in[1] & 0x0F;
        const uint8_t block = in[2];
        Report out = MakeReport({0x51, slot, block});
        const Slot& s = slots_[slot];
        if (s.present && block < kBlockCount) {
          out[1] |= 0x10;
          std::copy_n(s.data.begin() + block * kBlockSize, kBlockSize, out.begin() + 3);
          if (research_log_) {
            research_log_("read", slot, block, &s.data[block * kBlockSize], kBlockSize);
          }
        }
        replies_.push_back(out);
        break;
      }
      case 'W': {  // write one block of a figure
        const uint8_t slot = in[1] & 0x0F;
        const uint8_t block = in[2];
        Report out = MakeReport({0x57, slot, block});
        Slot& s = slots_[slot];
        if (s.present && block < kBlockCount) {
          out[1] |= 0x10;
          std::copy_n(in.begin() + 3, kBlockSize, s.data.begin() + block * kBlockSize);
          if (research_log_) {
            research_log_("write", slot, block, &s.data[block * kBlockSize], kBlockSize);
          }
          written = WriteEvent{static_cast<int>(slot), s.data, s.source};
        }
        replies_.push_back(out);
        break;
      }
      default:
        // 'S' and 'V' (status is answered by idle reads), the LED commands 'C' and 'L', and
        // anything unknown: no reply.
        break;
    }
    // Copy the callback out while still locked; invoke it after unlocking below, since it may do
    // file I/O and must never run with the portal's lock held.
    if (written) callback = on_write_;
  }
  if (written && callback) callback(written->slot, written->data, written->source);
}

Report SoftwarePortal::Read() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!replies_.empty()) {
    Report r = replies_.front();
    replies_.pop_front();
    return r;
  }
  return StatusReportLocked();
}

Report SoftwarePortal::StatusReportLocked() {
  uint32_t states = 0;
  if (active_) {
    for (int i = 0; i < kMaxFigures; ++i) {
      states |= static_cast<uint32_t>(slots_[i].state) << (2 * i);
    }
  }
  Report r = MakeReport({0x53, static_cast<uint8_t>(states), static_cast<uint8_t>(states >> 8),
                         static_cast<uint8_t>(states >> 16), static_cast<uint8_t>(states >> 24),
                         counter_++, static_cast<uint8_t>(active_ ? 1 : 0)});
  // Advance transitions once they have been reported.
  for (Slot& s : slots_) {
    if (s.state == SlotState::kAdded) {
      if (--s.reports_left <= 0) s.state = SlotState::kReady;
    } else if (s.state == SlotState::kRemoving) {
      if (s.swap_pending) {
        ApplySwapLocked(s);  // the old figure was reported as removing: now the new one
      } else {
        s.state = SlotState::kEmpty;
      }
    }
  }
  return r;
}

void SoftwarePortal::ApplySwapLocked(Slot& s) {
  s.swap_pending = false;
  s.present = true;
  s.data = s.next_data;
  s.source = std::move(s.next_source);
  s.next_data = FigureData{};
  s.next_source.reset();
  if (active_) {
    s.state = SlotState::kAdded;
    s.reports_left = kAddedReports;
  } else {
    s.state = SlotState::kEmpty;
  }
}

namespace {

// The same file under two spellings (relative and absolute, other case) counts as the same.
bool SameFigureFile(const std::filesystem::path& a, const std::filesystem::path& b) {
  if (a == b) return true;
  std::error_code ec;
  const bool same = std::filesystem::equivalent(a, b, ec);
  return !ec && same;
}

}  // namespace

bool SoftwarePortal::PlaceFigure(int slot, const FigureData& data,
                                 std::optional<std::filesystem::path> source) {
  if (slot < 0 || slot >= kMaxFigures) return false;
  std::lock_guard<std::mutex> lock(mu_);
  // One file, one slot: two slots on the same file save over each other, and one of them loses its
  // progress (Core of Light #60, TheBiemGamer). The menus show such a figure as taken; this catches
  // every other way in. The same slot again (a swap to the same file) is fine.
  if (source) {
    for (int other = 0; other < kMaxFigures; ++other) {
      const Slot& o = slots_[other];
      if (other != slot && o.present && o.source && SameFigureFile(*o.source, *source)) return false;
      if (other != slot && o.swap_pending && o.next_source && SameFigureFile(*o.next_source, *source)) return false;
    }
  }
  Slot& s = slots_[slot];
  // A swap while the game watches: writes it still sends for the old figure (right after a villain
  // capture, say) would land in the new figure's file (Core of Light #60, TheBiemGamer). The old one
  // is reported as removing for one status report first, its writes still going to its own file.
  if (active_ && s.present) {
    s.swap_pending = true;
    s.next_data = data;
    s.next_source = std::move(source);
    s.state = SlotState::kRemoving;
    return true;
  }
  s.swap_pending = false;
  s.next_source.reset();
  s.present = true;
  s.data = data;
  s.source = std::move(source);
  if (active_) {
    s.state = SlotState::kAdded;
    s.reports_left = kAddedReports;
  } else {
    s.state = SlotState::kEmpty;  // shown as added once the game activates the portal
  }
  return true;
}

bool SoftwarePortal::RemoveFigure(int slot) {
  if (slot < 0 || slot >= kMaxFigures) return false;
  std::lock_guard<std::mutex> lock(mu_);
  Slot& s = slots_[slot];
  if (!s.present) return false;
  s.present = false;
  s.data = FigureData{};
  s.source.reset();
  s.swap_pending = false;
  s.next_data = FigureData{};
  s.next_source.reset();
  s.state = active_ ? SlotState::kRemoving : SlotState::kEmpty;
  return true;
}

bool SoftwarePortal::HasFigure(int slot) const {
  if (slot < 0 || slot >= kMaxFigures) return false;
  std::lock_guard<std::mutex> lock(mu_);
  return slots_[slot].present;
}

void SoftwarePortal::SetWriteCallback(
    std::function<void(int, const FigureData&, const std::optional<std::filesystem::path>&)>
        callback) {
  std::lock_guard<std::mutex> lock(mu_);
  on_write_ = std::move(callback);
}

void SoftwarePortal::SetResearchLogCallback(
    std::function<void(const char*, int, int, const uint8_t*, size_t)> callback) {
  std::lock_guard<std::mutex> lock(mu_);
  research_log_ = std::move(callback);
}

std::optional<FigureData> SoftwarePortal::Figure(int slot) const {
  if (slot < 0 || slot >= kMaxFigures) return std::nullopt;
  std::lock_guard<std::mutex> lock(mu_);
  if (!slots_[slot].present) return std::nullopt;
  return slots_[slot].data;
}

std::optional<std::filesystem::path> SoftwarePortal::Source(int slot) const {
  if (slot < 0 || slot >= kMaxFigures) return std::nullopt;
  std::lock_guard<std::mutex> lock(mu_);
  if (!slots_[slot].present) return std::nullopt;
  return slots_[slot].source;
}

void SoftwarePortal::SetAudioSink(PortalAudioSink* sink) { audio_sink_.store(sink); }

void SoftwarePortal::WriteAudio(std::span<const int16_t> pcm) {
  if (PortalAudioSink* sink = audio_sink_.load()) sink->Submit(pcm);
}

}  // namespace skylanders::portal
