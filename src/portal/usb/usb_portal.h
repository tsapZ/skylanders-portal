#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <memory>

#include <hidapi.h>

#include "portal/audio.h"
#include "portal/portal_device.h"
#include "portal/traffic_log.h"

namespace skylanders::portal {

class WinUsbPortalIo;

// A small set of VID/PID pairs known to be a real Skylanders Portal of Power, matching Cemu's own
// nsyshid whitelist for this device family (read for this fact only, not copied code).
//
// Only the Wii U portal (1430:0150) has actually been tested (docs/architecture.md, "Portal
// layering"). The Xbox 360 portal (1430:1F17) is whitelisted on the same basis Cemu whitelists it,
// but this project has no such hardware to verify against -- it may use a different report size or
// need a different command mechanism than what was found for the Wii U portal.
inline constexpr std::pair<uint16_t, uint16_t> kKnownPortals[] = {
    {0x1430, 0x0150},  // Wii U Skylanders portal -- tested, works
    {0x1430, 0x1F17},  // Xbox 360 Skylanders portal -- untested
};

// A real, physical Portal of Power, opened over USB HID via hidapi -- no WinUSB/Zadig driver
// replacement needed; hid_send_output_report() reaches the device through the stock HID class
// driver (docs/portal-protocol.md).
//
// Commands go out via hid_send_output_report() (a HID SET_REPORT control transfer) -- not
// hid_write(), which prefers this device's interrupt OUT endpoint and is silently ignored by its
// firmware. Reads use plain hid_read_timeout(); the real device's report is exactly 32 bytes both
// ways, matching PortalDevice::Report directly.
class UsbPortal final : public PortalDevice {
 public:
  // Opens the first connected device matching kKnownPortals, in the order listed. Check IsOpen()
  // before installing this as the active portal -- construction never throws; it logs
  // which portal it opened, or the vendor's HID devices when none. The caller decides what to do
  // if no matching device was found.
  UsbPortal();
  ~UsbPortal() override;

  UsbPortal(const UsbPortal&) = delete;
  UsbPortal& operator=(const UsbPortal&) = delete;

  bool IsOpen() const { return device_ != nullptr || winusb_ != nullptr; }

  void Write(const Report& report) override;
  Report Read() override;
  // Repacks speaker audio into the portal's 64-byte packets and queues them for the audio writer
  // thread; never blocks the caller.
  void WriteAudio(std::span<const int16_t> pcm) override;

  // Best-effort status, derived by passively observing replies that already flow through Read()
  // as part of relaying the game's own polling -- no extra USB traffic, no separate poller thread
  // competing with the game's hook thread for the device. Reflects what the game itself has seen
  // so far: it can lag a few seconds behind the real device, since it only updates once the game
  // happens to poll a status frame or read block 1 (where id/variant live) on its own.
  //
  // Tracked per slot (0-15, matching kMaxFigures): a real portal can hold more than one figure at
  // once (Giants supports 2-player co-op plus items), so slot 0 alone is not the whole picture.
  bool FigurePresent() const;                // true if any slot holds a figure
  std::vector<int> PresentSlots() const;      // every slot index that currently holds a figure
  std::optional<std::pair<uint16_t, uint16_t>> DetectedIdVariant(int slot) const;
  bool HadError() const { return write_error_logged_.load() || read_error_logged_.load(); }

  // True once enough consecutive real I/O errors (not idle "nothing to report" timeouts) have
  // happened that the device is very likely physically unplugged. IsOpen() alone can't tell this:
  // it only reflects whether hid_open() succeeded back at construction, not whether the device is
  // still there now -- a real disconnect leaves device_ non-null but every further hid call
  // failing. Used by the hot-plug wrapper in portal_hook.cpp to decide when to retry.
  bool SeemsDisconnected() const { return consecutive_errors_.load() >= kDisconnectThreshold; }

  // Best-effort full figure data for `slot`, built the same passive way as FigurePresent/
  // DetectedIdVariant above -- assembled from whichever blocks the game's own polling has already
  // read, never by actively polling the device ourselves. Returns nullopt until every block
  // ParseFigureStats (figure_stats.h) needs (0x00, 0x01, 0x08, 0x0A, 0x0C) has been seen at least
  // once for this slot since it was placed; cheap and non-blocking, safe to call every frame.
  std::optional<FigureData> CachedFigureData(int slot) const;

  // Dumps all 64 blocks of the real figure in `slot` to a fresh FigureData, saving it as a file
  // is the caller's job (see portal_rex/portal_rex.h's DumpRealFigure). While this runs, the game is
  // told `slot` is empty (a synthetic status frame from Read(); Write() no-ops) instead of really
  // being polled, so there is never a second requester racing this read on the wire -- unlike an
  // always-on active read, this is a rare, short, user-triggered pause the caller explicitly
  // asked for, not a per-frame cost. Blocks for the whole dump (bounded: 64 blocks, ~200ms cap
  // each). Returns nullopt -- with a reason in `error`, if given -- if there's no figure in that
  // slot, it's physically removed mid-dump, or an I/O error occurs; normal passthrough resumes
  // either way before returning.
  std::optional<FigureData> DumpFigure(int slot, std::string* error = nullptr);

 private:
  void ObserveReply(const Report& report);

  void SendRaw(const Report& report);  // unlocked -- callers hold io_mutex_
  void AudioWriterLoop();

  AudioPacketQueue audio_queue_{64};  // ~256 ms of audio at 8 kHz
  PcmPacketizer audio_packetizer_;    // game thread only
  std::thread audio_thread_;          // started on the first WriteAudio
  std::once_flag audio_thread_started_;
  std::atomic<bool> audio_stopping_{false};
  Report ReceiveRaw();                 // unlocked -- callers hold io_mutex_

  std::mutex io_mutex_;  // serializes every raw HID transfer: Write(), Read(), DumpFigure()
  // The game's commands and what it got back, in plain lines for a player's log (traffic_log.h).
  PortalTrafficLog traffic_;

  hid_device* device_ = nullptr;
  // Instead of device_: a 1430:0150 portal on the WinUSB driver (Zadig, for Dolphin or Cemu; Core of
  // Light #58), which hidapi can't open. Same reports, see winusb_portal_io.h.
  std::unique_ptr<WinUsbPortalIo> winusb_;
  uint16_t vendor_id_ = 0;   // of the opened device, for the log
  uint16_t product_id_ = 0;
  // The first few raw commands and replies go to the log (TrapTeamRecomp #19: an Xbox 360
  // Traptanium portal came up as "incompatible"; the Xbox 360 portals were never tested here).
  static constexpr int kRawLogCount = 6;
  std::atomic<int> raw_writes_logged_{0};
  std::atomic<int> raw_reads_logged_{0};
  std::atomic<bool> write_error_logged_{false};
  std::atomic<bool> read_error_logged_{false};
  static constexpr int kDisconnectThreshold = 5;
  std::atomic<int> consecutive_errors_{0};
  std::array<std::atomic<bool>, kMaxFigures> slot_present_{};
  mutable std::mutex detected_mutex_;
  std::array<std::optional<std::pair<uint16_t, uint16_t>>, kMaxFigures> slot_id_variant_;

  // Passive block cache, filled in as the game's own Q replies pass through ObserveReply().
  mutable std::mutex cache_mutex_;
  std::array<FigureData, kMaxFigures> slot_cache_{};
  std::array<std::array<bool, kBlockCount>, kMaxFigures> slot_block_seen_{};

  // DumpFigure() state: while dumping_slot_ is >= 0, Read()/Write() fake that slot as empty
  // instead of touching real hardware for it (see DumpFigure's own comment for why).
  std::atomic<int> dumping_slot_{-1};
  std::atomic<bool> last_active_{true};    // last real 'active' status byte seen
  std::atomic<uint8_t> status_counter_{0};  // running counter for synthetic status frames

  // Read()'s queue, guarded by io_mutex_: command replies in order, status frames collapsed to the
  // newest. In the object, not in function statics, so a portal plugged back in (a new UsbPortal)
  // doesn't pass on what the old one had queued (Core of Light #60, TheBiemGamer).
  std::deque<Report> pending_replies_;
  Report newest_status_{};
  bool have_status_ = false;
  uint32_t added_mask_ = 0;  // bit per slot: a dropped frame reported "added"
};

}  // namespace skylanders::portal
