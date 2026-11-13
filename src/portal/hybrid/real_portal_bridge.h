#pragma once

// portal_mode "hybrid": the game talks to a SoftwarePortal, so the portal menu, its item and
// sidekick rules and the portal check keep working, and this bridge drives a real portal next to
// it (docs/hybrid-portal.md in SwapForceRecomp):
//   - every figure standing on the real portal goes into a free emulated slot as a copy once its
//     first kEarlyBlocks blocks are read, and the game reads that copy while the rest comes in: a
//     block it asks for is read next, its reply waiting meanwhile (SoftwarePortal's loading
//     copies), so a toy comes in about as fast as on the real portal alone (TheBiemGamer's idea,
//     Core of Light #62; TrapTeamRecomp #22: reading all 64 blocks first took 1.5 to 1.8 s on an
//     Xbox 360 portal). Once whole, its 1 KB is saved as a backup file, also the copy's source;
//   - blocks the game writes to the copy go back onto the real figure, one at a time, each figure's
//     in order (never block 0 or 1 or a sector trailer, which hold the tag's identity and keys); a
//     write the portal refuses goes out again after a pause, which holds up only that figure's
//     writes;
//   - lifting the figure off the real portal takes the copy off; a copy the portal menu lifted is
//     followed by its source path, so it is written again once the menu puts it back;
//   - the game's light commands (C, and Trap Team's J and L) go on to the real portal.
// No ReXGlue dependency: tested with a second SoftwarePortal standing in for the real one.

#include <array>
#include <atomic>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
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
    // A toy was left out because its backup file is already on the emulated portal through the
    // portal menu (its name); called on the bridge's thread. Optional.
    std::function<void(const std::string& name)> on_backup_in_use;
    // With no sign of an active real portal for this long (a status saying active, a reply), it is
    // set up again (R, then A): a portal can miss the A right after it is plugged in.
    std::chrono::milliseconds restart_after{1000};
    // A block the real portal couldn't read (or didn't answer for) is asked for again after this
    // pause, up to 12 times a block, before the figure is given up (TrapTeamRecomp #22).
    std::chrono::milliseconds read_retry_pause{150};
    // A block write the real portal refused (or didn't answer) goes out again after this pause, up
    // to kWriteAttempts times; three tries in a row came and went within a moment of an older
    // portal not taking writes (the owner's test 2026-11-12, a Legendary Trigger Happy block).
    std::chrono::milliseconds write_retry_pause{150};
  };

  RealPortalBridge(SoftwarePortal* emulated, Options options);

  // One step: handles what the real portal sent and sends the next command. Call every few
  // milliseconds from one thread. `real` is null while no real portal is connected; a change of
  // `real` (connected, unplugged, replaced) takes every copy off and starts over.
  void Step(PortalDevice* real);

  // From the emulated portal's write callback (any thread): queues the blocks of a copy that
  // changed, for writing to the real figure.
  void OnEmulatedWrite(int emulated_slot, const FigureData& data);

  // The game's light commands ('C', and Trap Team's 'J' and 'L') for the portal: the newest per
  // command and side goes to the real portal once it is active, and again after it is set up anew.
  void OnLight(const Report& report);

  // The game's speaker switch ('M'): the newest goes to the real portal once it is active, and again
  // after it is set up anew. Its answer says whether the real portal has a speaker (RealHasSpeaker),
  // so the game's speaker audio can go there instead of the PC (TrapTeamRecomp #22).
  void OnSpeakerSwitch(const Report& report);
  // The real portal answered the speaker switch as one with a speaker (4D, then the audio version
  // kAudioCapableVersion, as a Trap Team portal answers). False until then, without an answer, and
  // again for each new real portal. Any thread.
  bool RealHasSpeaker() const { return real_speaker_.load(); }

  // Waiting for the real portal's reply to a command: the caller polls again soon instead of sleeping.
  bool Busy() const { return pending_.has_value() || wake_ != Wake::kActive; }

  // UIDs (the first four bytes of block 0) of the figures read from the real portal now.
  std::vector<uint32_t> RealUids() const;
  // Whether this emulated slot holds a copy of a figure on the real portal.
  bool IsCopySlot(int emulated_slot) const;

  static uint32_t UidOf(const FigureData& data);

 private:
  using Clock = std::chrono::steady_clock;
  // Setting the real portal up: R sent (waiting for its reply), then A (waiting), then active.
  enum class Wake { kReady, kActivate, kActive };

  struct RealSlot {
    // kReading: its first kEarlyBlocks blocks, nothing on the emulated portal yet. kLoading: its
    // copy is on the emulated portal (a loading one) and the rest is read, a block the game waits
    // for first. kFinishing: every block read, its backup file being written. kMirrored: whole.
    enum class Phase { kEmpty, kReading, kLoading, kFinishing, kMirrored, kFailed } phase = Phase::kEmpty;
    FigureData data{};
    std::bitset<kBlockCount> read;   // the blocks read from the toy
    int attempts = 0;                // for the block being read
    int reading = -1;                // the block asked for last (for the log)
    Clock::time_point retry_at{};    // not asked again before this
    Clock::time_point seen{};        // put on the real portal (for the log)
    uint32_t uid = 0;
    std::string name;
    std::filesystem::path source;  // the backup file, the copy's identity
    bool backup_checked = false;   // an older backup with other data was kept aside (Finish)
  };
  struct PendingWrite {
    int real_slot = 0;
    int block = 0;
    std::array<uint8_t, kBlockSize> bytes{};
    int attempts = 0;
    std::chrono::steady_clock::time_point retry_at{};  // not sent again before this
    bool in_flight = false;  // sent, the real portal's answer not in yet
  };
  struct Command {
    uint8_t cmd = 0;
    int slot = 0;
    int block = 0;
    Clock::time_point sent;
  };

  void Log(const std::string& text) const;
  // Forgets the command waiting for its answer (the portal is set up anew or replaced); a write it
  // was goes out again.
  void DropPending();
  // Under mu_: the write sent last, its answer not in yet, or writes_.end().
  std::deque<PendingWrite>::iterator InFlightWrite();
  // Under mu_: the write `it` was refused or not answered. A newer version of its block queued
  // behind it takes its place; otherwise it goes out again after the pause, and after kWriteAttempts
  // tries it is given up with `given_up` in the log (the backup file has it).
  void WriteMissed(std::deque<PendingWrite>::iterator it, const std::string& given_up);
  void SetUpPortal();
  void Handle(const Report& report);
  void HandleStatus(const Report& report);
  void SendNext();
  // The first block of a toy not read yet, -1 if none (a block the game waits for goes first, in
  // SendNext).
  int NextBlock(int real_slot) const;
  // The toy's first kEarlyBlocks blocks are read: its copy goes onto the emulated portal (loading).
  void Place(int real_slot);
  // A block read from a toy whose copy is loading: into the copy, unless the game wrote it there.
  void Fill(int real_slot, int block);
  // Every block of the toy is read: its backup file gets the copy, which then is whole. Tried again
  // on the next step while the game keeps writing to it.
  void Finish(int real_slot);
  // The emulated slot of a real slot's copy, or -1.
  int CopyOf(int real_slot) const;
  // The copy of a toy given up while it was loading comes off the emulated portal.
  void DropCopy(int real_slot);
  // A block of the figure being read wasn't read (refused or no answer): asked again after the
  // pause, or the figure is given up after too many tries, with a log line.
  void ReadMissed(int real_slot, const char* why);
  void Gone(int real_slot, const char* why);
  // The real portal's answer to the speaker switch (or none): RealHasSpeaker, a log line on a change.
  void NoteSpeaker(bool speaker, const char* why);
  void TrackCopies();
  int FindCopy(const std::filesystem::path& source) const;

  SoftwarePortal* emulated_;
  Options options_;

  // Bridge thread only.
  PortalDevice* real_ = nullptr;
  std::array<RealSlot, kMaxFigures> slots_{};
  std::optional<Command> pending_;
  Clock::time_point last_light_{};
  Wake wake_ = Wake::kActive;
  Clock::time_point last_alive_{};  // the last sign of an active real portal
  bool restart_logged_ = false;
  int speaker_known_ = -1;  // what NoteSpeaker last logged: -1 nothing yet, 0 no speaker, 1 speaker

  // Shared with OnEmulatedWrite / OnLight / the queries (any thread).
  mutable std::mutex mu_;
  std::array<int, kMaxFigures> copy_slot_{};  // real slot -> emulated slot, -1 for none
  std::array<FigureData, kMaxFigures> written_{};  // what the real figure holds (or will)
  std::array<uint32_t, kMaxFigures> uid_{};
  std::array<bool, kMaxFigures> mirrored_{};
  std::deque<PendingWrite> writes_;
  // The newest light per command and side (C: one, J / L: per side byte), and whether it still has
  // to go out.
  std::map<uint16_t, std::pair<Report, bool>> lights_;
  // The game's newest speaker switch, and whether it still has to go out.
  std::optional<Report> speaker_switch_;
  bool speaker_switch_due_ = false;
  // Answered by the real portal (bridge thread writes, any thread reads).
  std::atomic<bool> real_speaker_{false};
};

}  // namespace skylanders::portal
