#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>

#include "portal/portal_device.h"

namespace skylanders::portal {

// A portal implemented in software. Answers the game's command reports, holds up to 16 figures,
// and can be driven from another thread through PlaceFigure / RemoveFigure.
class SoftwarePortal : public PortalDevice {
 public:
  void Write(const Report& report) override;
  Report Read() override;
  // Passes speaker audio to the audio sink, if any.
  void WriteAudio(std::span<const int16_t> pcm) override;

  // Where speaker audio goes; nullptr drops it (and 'M' then reports no speaker). Not owned.
  // Thread-safe.
  void SetAudioSink(PortalAudioSink* sink);

  // Control API. Thread-safe.
  //
  // `source`, if given, is remembered alongside `data` under the same lock, so a write callback
  // fired for this slot always sees the source that matches whichever figure is actually live at
  // that instant — even across a swap (PlaceFigure called again on an already-present slot) with
  // no separate, independently-lockable bookkeeping that could ever fall out of step with it.
  bool PlaceFigure(int slot, const FigureData& data,
                   std::optional<std::filesystem::path> source = std::nullopt);  // false if out of range,
                                                                                  // or `source` is in another slot
  bool RemoveFigure(int slot);  // false if out of range or empty; also forgets the source
  bool HasFigure(int slot) const;
  std::optional<FigureData> Figure(int slot) const;
  std::optional<std::filesystem::path> Source(int slot) const;  // nullopt if empty, out of range, or no source

  // Called after a successful figure write ('W' to a present slot and a valid block), with the
  // slot index, the figure's full data at that point, and the source PlaceFigure was given for
  // that slot (nullopt if none). Runs on the calling thread (whichever thread called Write()),
  // outside the portal's lock. Pass nullptr to remove it.
  void SetWriteCallback(
      std::function<void(int slot, const FigureData& data,
                         const std::optional<std::filesystem::path>& source)>
          callback);

  // Called for every 'Q' (read) and 'W' (write) that touches a present slot's block within range,
  // with "read" or "write", the slot, the block index, and a pointer to that block's 16 bytes --
  // purely for optional trace logging of the raw wire bytes, to support the future research phase
  // that determines real level/gold/nickname offsets (see figure_stats.h). Runs synchronously
  // under the portal's lock (like the callback above, but note it fires *inside* Write(), not
  // after unlocking), so it must be cheap and must not call back into this SoftwarePortal.
  // SoftwarePortal deliberately has no ReXGlue dependency of its own (see CMakeLists.txt), so it
  // cannot call REXLOG_TRACE directly -- whoever wires this up (see portal_hook.cpp, which does
  // link ReXGlue) does the actual logging. Pass nullptr to remove it.
  void SetResearchLogCallback(
      std::function<void(const char* op, int slot, int block, const uint8_t* data, size_t n)>
          callback);

 private:
  enum class SlotState : uint8_t { kEmpty = 0, kReady = 1, kRemoving = 2, kAdded = 3 };
  struct Slot {
    bool present = false;
    SlotState state = SlotState::kEmpty;
    int reports_left = 0;  // status reports still to show kAdded
    FigureData data{};
    std::optional<std::filesystem::path> source;
  };

  Report StatusReportLocked();

  std::atomic<PortalAudioSink*> audio_sink_{nullptr};
  mutable std::mutex mu_;
  std::deque<Report> replies_;
  std::array<Slot, kMaxFigures> slots_{};
  bool active_ = false;
  uint8_t counter_ = 0;
  std::function<void(int, const FigureData&, const std::optional<std::filesystem::path>&)> on_write_;
  std::function<void(const char*, int, int, const uint8_t*, size_t)> research_log_;
};

}  // namespace skylanders::portal
