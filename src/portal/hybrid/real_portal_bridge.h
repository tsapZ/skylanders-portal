#pragma once

// portal_mode "hybrid": the game talks to a SoftwarePortal, so the portal menu, its item and
// sidekick rules and the portal check keep working, and this bridge drives a real portal next to
// it (docs/hybrid-portal.md in SwapForceRecomp):
//   - every figure standing on the real portal is read whole (all 64 blocks) and put into a free
//     emulated slot as a copy, with its 1 KB saved as a backup file that is also the copy's source;
//   - blocks the game writes to the copy go back onto the real figure, one at a time, in order
//     (never block 0 or 1 or a sector trailer, which hold the tag's identity and keys);
//   - lifting the figure off the real portal takes the copy off; a copy the portal menu lifted is
//     followed by its source path, so it is written again once the menu puts it back;
//   - the game's light command goes on to the real portal.
// No ReXGlue dependency: tested with a second SoftwarePortal standing in for the real one.

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "portal/portal_device.h"
#include "portal/software/software_portal.h"

namespace skylanders::portal {

class RealPortalBridge {
 public:
  struct Options {
    // Where each real figure's 1 KB is kept: saved when it arrives and after every write the
    // game makes (through the emulated portal's write callback), and used as the copy's source.
    std::filesystem::path backup_dir;
    std::function<void(const std::string&)> log;  // optional
    // A copy was put into this emulated slot; called on the bridge's thread.
    std::function<void(int emulated_slot)> on_arrived;
  };

  RealPortalBridge(SoftwarePortal* emulated, Options options);

  // One step: handles what the real portal sent and sends the next command. Call every few
  // milliseconds from one thread. `real` is null while no real portal is connected; a change of
  // `real` (connected, unplugged, replaced) takes every copy off and starts over.
  void Step(PortalDevice* real);

  // From the emulated portal's write callback (any thread): queues the blocks of a copy that
  // changed, for writing to the real figure.
  void OnEmulatedWrite(int emulated_slot, const FigureData& data);

  // The game's light command ('C') for the portal; the newest one goes to the real portal.
  void OnLight(const Report& report);

  // UIDs (the first four bytes of block 0) of the figures read from the real portal now.
  std::vector<uint32_t> RealUids() const;
  // Whether this emulated slot holds a copy of a figure on the real portal.
  bool IsCopySlot(int emulated_slot) const;

  static uint32_t UidOf(const FigureData& data);

 private:
  using Clock = std::chrono::steady_clock;

  struct RealSlot {
    enum class Phase { kEmpty, kReading, kMirrored, kFailed } phase = Phase::kEmpty;
    FigureData data{};
    int next_block = 0;
    int attempts = 0;
    uint32_t uid = 0;
    std::filesystem::path source;  // the backup file, the copy's identity
  };
  struct PendingWrite {
    int real_slot = 0;
    int block = 0;
    std::array<uint8_t, kBlockSize> bytes{};
    int attempts = 0;
  };
  struct Command {
    uint8_t cmd = 0;
    int slot = 0;
    int block = 0;
    Clock::time_point sent;
  };

  void Log(const std::string& text) const;
  void Handle(const Report& report);
  void HandleStatus(const Report& report);
  void SendNext();
  void Arrived(int real_slot);
  void Gone(int real_slot, const char* why);
  void TrackCopies();
  int FindCopy(const std::filesystem::path& source) const;

  SoftwarePortal* emulated_;
  Options options_;

  // Bridge thread only.
  PortalDevice* real_ = nullptr;
  std::array<RealSlot, kMaxFigures> slots_{};
  std::optional<Command> pending_;
  Clock::time_point last_light_{};

  // Shared with OnEmulatedWrite / OnLight / the queries (any thread).
  mutable std::mutex mu_;
  std::array<int, kMaxFigures> copy_slot_{};  // real slot -> emulated slot, -1 for none
  std::array<FigureData, kMaxFigures> written_{};  // what the real figure holds (or will)
  std::array<uint32_t, kMaxFigures> uid_{};
  std::array<bool, kMaxFigures> mirrored_{};
  std::deque<PendingWrite> writes_;
  std::optional<Report> light_;
};

}  // namespace skylanders::portal
