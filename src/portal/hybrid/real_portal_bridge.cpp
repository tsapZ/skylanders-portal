#include "portal/hybrid/real_portal_bridge.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <system_error>

#include "portal/audio.h"
#include "portal/figure_catalog.h"
#include "portal/figure_file.h"
#include "portal_rex/utf8_path.h"

namespace skylanders::portal {

namespace {

// How long the real portal gets to answer a block read or write before it is asked again.
constexpr auto kReplyTimeout = std::chrono::milliseconds(250);
// A block write, refused or unanswered, is tried this often (Options::write_retry_pause apart) before
// the block is given up (it stays in the backup file). Three tries in a row were all used up within
// a moment of an older portal not taking writes (the owner's test 2026-11-12).
constexpr int kWriteAttempts = 6;
// A block read, refused or unanswered, is tried this often (Options::read_retry_pause apart) before
// the figure is given up: a figure just put on, or one not lying flat, can't be read for a moment.
// With 3 quick tries a figure put back after a swap was given up at once and then ignored until it
// was lifted off and put on again (TrapTeamRecomp #22, an Xbox 360 portal).
constexpr int kReadAttempts = 12;
// A toy's copy goes onto the emulated portal once this many of its blocks are read (0 to 7: its
// identity and the first sector), so a toy that only touched the portal for a moment, or bounced back
// on, isn't shown to the game (TheBiemGamer's number, from his portal, Core of Light #62).
constexpr int kEarlyBlocks = 8;
// The light: at most this often.
constexpr auto kLightInterval = std::chrono::milliseconds(100);
// Reports handled per step: the real portal queues a status report every few milliseconds.
constexpr int kReportsPerStep = 16;

Report Make(std::initializer_list<uint8_t> bytes) {
  Report r{};
  std::copy(bytes.begin(), bytes.end(), r.begin());
  return r;
}

// Blocks the game may change on the real figure: not the tag's identity (0, 1) and not a sector
// trailer (every fourth block, the keys and access bits).
bool Writable(int block) { return block > 1 && block % 4 != 3; }

std::string Hex(uint32_t value) {
  char text[16];
  std::snprintf(text, sizeof(text), "%08X", value);
  return text;
}

}  // namespace

RealPortalBridge::RealPortalBridge(SoftwarePortal* emulated, Options options)
    : emulated_(emulated), options_(std::move(options)) {
  copy_slot_.fill(-1);
}

uint32_t RealPortalBridge::UidOf(const FigureData& data) {
  return uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) |
         (uint32_t(data[3]) << 24);
}

void RealPortalBridge::Log(const std::string& text) const {
  if (options_.log) options_.log(text);
}

void RealPortalBridge::DropPending() {
  pending_.reset();
  std::lock_guard lock(mu_);
  for (PendingWrite& w : writes_) w.in_flight = false;
}

std::deque<RealPortalBridge::PendingWrite>::iterator RealPortalBridge::InFlightWrite() {
  return std::find_if(writes_.begin(), writes_.end(),
                      [](const PendingWrite& w) { return w.in_flight; });
}

void RealPortalBridge::WriteMissed(std::deque<PendingWrite>::iterator it,
                                   const std::string& given_up) {
  it->in_flight = false;
  const int slot = it->real_slot;
  const int block = it->block;
  if (std::any_of(std::next(it), writes_.end(), [&](const PendingWrite& w) {
        return w.real_slot == slot && w.block == block;
      })) {
    writes_.erase(it);  // the game wrote the block again meanwhile: only that version goes out
    return;
  }
  if (++it->attempts >= kWriteAttempts) {
    Log(given_up);
    writes_.erase(it);
    return;
  }
  it->retry_at = Clock::now() + options_.write_retry_pause;
}

// Ready first; activate once the ready is answered (Handle): a portal just plugged in can miss an A
// sent right after the R (Core of Light #62, TheBiemGamer), and then stays switched off.
void RealPortalBridge::SetUpPortal() {
  DropPending();
  wake_ = Wake::kReady;
  last_alive_ = Clock::now();
  real_->Write(Make({'R'}));
}

void RealPortalBridge::Step(PortalDevice* real) {
  if (real != real_) {
    for (int slot = 0; slot < kMaxFigures; ++slot) {
      if (slots_[slot].phase != RealSlot::Phase::kEmpty) Gone(slot, "the real portal went away");
    }
    real_ = real;
    DropPending();
    // Another portal (or none): whether it has a speaker is asked again.
    real_speaker_.store(false);
    speaker_known_ = -1;
    {
      std::lock_guard lock(mu_);
      if (speaker_switch_) speaker_switch_due_ = true;
    }
    if (real_) {
      // Ready, then activate: the portal then reports its figures, each as "added".
      restart_logged_ = false;
      SetUpPortal();
      Log("Hybrid portal: real portal connected");
    }
  }
  if (!real_) return;
  for (int i = 0; i < kReportsPerStep; ++i) {
    const Report report = real_->Read();
    if (report[0] == 0) break;
    Handle(report);
  }
  // No sign of an active portal for a while (it answered R but missed A, or lost power): set it up
  // again. A busy portal sends no status while it answers block reads, so those replies count too.
  if (Clock::now() - last_alive_ > options_.restart_after) {
    if (!restart_logged_) {
      Log("Hybrid portal: the real portal isn't switched on; setting it up again");
      restart_logged_ = true;
    }
    SetUpPortal();
    return;
  }
  if (pending_ && Clock::now() - pending_->sent > kReplyTimeout) {
    const Command timed_out = *pending_;
    pending_.reset();
    if (timed_out.cmd == 'Q') {
      const RealSlot::Phase phase = slots_[timed_out.slot].phase;
      if (phase == RealSlot::Phase::kReading || phase == RealSlot::Phase::kLoading) {
        ReadMissed(timed_out.slot, "no answer");
      }
    } else if (timed_out.cmd == 'W') {
      std::lock_guard lock(mu_);
      if (const auto it = InFlightWrite(); it != writes_.end()) {
        WriteMissed(it, "Hybrid portal: block " + std::to_string(it->block) +
                            " could not be written to the real figure; it is in the backup file");
      }
    } else if (timed_out.cmd == 'M') {
      // A portal from before Trap Team doesn't know the command: no speaker.
      NoteSpeaker(false, "didn't answer the speaker switch");
    }
  }
  TrackCopies();
  for (int slot = 0; slot < kMaxFigures; ++slot) {
    if (slots_[slot].phase == RealSlot::Phase::kFinishing) Finish(slot);
  }
  if (!pending_) SendNext();
}

void RealPortalBridge::Handle(const Report& report) {
  switch (report[0]) {
    case 0x52:  // ready answered: now activate
      last_alive_ = Clock::now();
      if (wake_ == Wake::kReady) {
        wake_ = Wake::kActivate;
        real_->Write(Make({'A', 0x01}));
      }
      break;
    case 0x41:  // activate answered: the portal is on; its lights and the speaker switch again
      last_alive_ = Clock::now();
      if (wake_ == Wake::kActivate && report[1] != 0) {
        wake_ = Wake::kActive;
        std::lock_guard lock(mu_);
        for (auto& [key, light] : lights_) light.second = true;
        if (speaker_switch_) speaker_switch_due_ = true;
      }
      break;
    case 0x4D: {  // speaker switch answered: 4D, its argument, the audio version
      last_alive_ = Clock::now();
      if (!pending_ || pending_->cmd != 'M') break;
      pending_.reset();
      const bool speaker = report[2] == kAudioCapableVersion[0] && report[3] == kAudioCapableVersion[1];
      // The answer's bytes in the log: an Xbox 360 Traptanium portal answered, and was taken for one
      // without a speaker (TrapTeamRecomp #22, 2026-11-13), with bytes no log had kept.
      char answer[64];
      std::snprintf(answer, sizeof(answer), "has no speaker (answered %02X %02X %02X %02X)", report[0], report[1],
                    report[2], report[3]);
      NoteSpeaker(speaker, speaker ? "has a speaker" : answer);
      break;
    }
    case 0x53:
      // Until A is answered a status may be a stale one from a portal still on from an earlier run;
      // reading on it, the portal refused and the figure waited until it was lifted (#62).
      if (wake_ != Wake::kActive) break;
      if (report[6] != 0) last_alive_ = Clock::now();
      HandleStatus(report);
      break;
    case 0x51: {  // a block read: 51, slot | 10 if a figure is there, block, 16 bytes
      last_alive_ = Clock::now();
      if (!pending_ || pending_->cmd != 'Q') break;
      const int slot = report[1] & 0x0F;
      const int block = report[2];
      if (slot != pending_->slot || block != pending_->block) break;
      pending_.reset();
      RealSlot& s = slots_[slot];
      if (s.phase != RealSlot::Phase::kReading && s.phase != RealSlot::Phase::kLoading) break;
      if (block >= int(kBlockCount) || s.read.test(size_t(block))) break;
      if (!(report[1] & 0x10)) {
        ReadMissed(slot, "the portal couldn't read it");
        break;
      }
      std::copy_n(report.begin() + 3, kBlockSize, s.data.begin() + block * kBlockSize);
      s.read.set(size_t(block));
      s.attempts = 0;
      if (s.phase == RealSlot::Phase::kReading) {
        bool early = true;
        for (int b = 0; b < kEarlyBlocks; ++b) early = early && s.read.test(size_t(b));
        if (early) Place(slot);
      } else {
        Fill(slot, block);
      }
      if (s.phase == RealSlot::Phase::kLoading && s.read.all()) {
        s.phase = RealSlot::Phase::kFinishing;
        Finish(slot);
      }
      break;
    }
    case 0x57: {  // a block write: 57, slot | 10 if it was written, block
      last_alive_ = Clock::now();
      if (!pending_ || pending_->cmd != 'W') break;
      const int slot = report[1] & 0x0F;
      const int block = report[2];
      if (slot != pending_->slot || block != pending_->block) break;
      pending_.reset();
      std::lock_guard lock(mu_);
      const auto it = InFlightWrite();
      if (it == writes_.end() || it->real_slot != slot || it->block != block) break;
      if (report[1] & 0x10) {
        writes_.erase(it);
      } else {
        WriteMissed(it, "Hybrid portal: the real portal refused block " + std::to_string(block) +
                            "; it is in the backup file");
      }
      break;
    }
    default:
      break;  // ready / activate replies and anything else
  }
}

void RealPortalBridge::HandleStatus(const Report& report) {
  const uint32_t states = uint32_t(report[1]) | (uint32_t(report[2]) << 8) |
                          (uint32_t(report[3]) << 16) | (uint32_t(report[4]) << 24);
  for (int slot = 0; slot < kMaxFigures; ++slot) {
    const uint32_t state = (states >> (2 * slot)) & 0x03;
    const bool present = state == 0x01 || state == 0x03;  // ready or added; 2 is "removing"
    RealSlot& s = slots_[slot];
    if (present && s.phase == RealSlot::Phase::kEmpty) {
      s = RealSlot{};
      s.phase = RealSlot::Phase::kReading;
      s.seen = Clock::now();
    } else if (!present && s.phase != RealSlot::Phase::kEmpty) {
      Gone(slot, "lifted off the real portal");
    }
  }
}

void RealPortalBridge::SendNext() {
  if (wake_ != Wake::kActive) return;  // nothing goes out before the portal is on
  const Clock::time_point now = Clock::now();
  const auto read = [&](int slot, int block) {
    pending_ = Command{'Q', slot, block, now};
    slots_[slot].reading = block;
    real_->Write(Make({'Q', uint8_t(slot), uint8_t(block)}));
  };
  // A block the game waits for comes first: its reply holds up everything the game asks.
  for (int slot = 0; slot < kMaxFigures; ++slot) {
    const RealSlot& s = slots_[slot];
    if (s.phase != RealSlot::Phase::kLoading || now < s.retry_at) continue;
    const int copy = CopyOf(slot);
    if (copy < 0) continue;
    if (const std::optional<int> wanted = emulated_->WantedBlock(copy);
        wanted && *wanted >= 0 && *wanted < int(kBlockCount) && !s.read.test(size_t(*wanted))) {
      read(slot, *wanted);
      return;
    }
  }
  // Writes next, so the real figure keeps up with the game; then figures still being read. The
  // write goes out after the lock is released: the game's thread takes it for OnEmulatedWrite.
  std::optional<Report> write;
  {
    std::lock_guard lock(mu_);
    while (!writes_.empty() && !mirrored_[writes_.front().real_slot]) writes_.pop_front();
    // Each figure's writes go out in their order (a toy's save is several blocks). One waiting out
    // its pause after a refusal holds up only the writes of its own figure; the other figures',
    // reads and lights go on meanwhile.
    std::array<bool, kMaxFigures> held{};
    for (PendingWrite& w : writes_) {
      if (held[size_t(w.real_slot)]) continue;
      held[size_t(w.real_slot)] = true;
      if (!mirrored_[size_t(w.real_slot)] || now < w.retry_at) continue;
      write = Make({'W', uint8_t(w.real_slot), uint8_t(w.block)});
      std::copy(w.bytes.begin(), w.bytes.end(), write->begin() + 3);
      w.in_flight = true;
      pending_ = Command{'W', w.real_slot, w.block, Clock::now()};
      break;
    }
  }
  if (write) {
    real_->Write(*write);
    return;
  }
  for (int slot = 0; slot < kMaxFigures; ++slot) {
    const RealSlot& s = slots_[slot];
    if ((s.phase != RealSlot::Phase::kReading && s.phase != RealSlot::Phase::kLoading) || now < s.retry_at) {
      continue;
    }
    const int block = NextBlock(slot);
    if (block < 0) continue;
    read(slot, block);
    return;
  }
  // The game's speaker switch, answered with whether the portal has a speaker.
  std::optional<Report> speaker;
  {
    std::lock_guard lock(mu_);
    if (speaker_switch_due_ && speaker_switch_) {
      speaker = speaker_switch_;
      speaker_switch_due_ = false;
    }
  }
  if (speaker) {
    pending_ = Command{'M', -1, 0, now};
    real_->Write(*speaker);
    return;
  }
  std::optional<Report> light;
  {
    std::lock_guard lock(mu_);
    if (Clock::now() - last_light_ >= kLightInterval) {
      for (auto& [key, entry] : lights_) {
        if (!entry.second) continue;
        light = entry.first;
        entry.second = false;
        break;
      }
    }
  }
  if (light) {
    last_light_ = Clock::now();
    real_->Write(*light);  // no reply
  }
}

int RealPortalBridge::NextBlock(int real_slot) const {
  const RealSlot& s = slots_[real_slot];
  for (int block = 0; block < int(kBlockCount); ++block) {
    if (!s.read.test(size_t(block))) return block;
  }
  return -1;
}

int RealPortalBridge::CopyOf(int real_slot) const {
  std::lock_guard lock(mu_);
  return copy_slot_[real_slot];
}

void RealPortalBridge::Place(int real_slot) {
  RealSlot& s = slots_[real_slot];
  s.uid = UidOf(s.data);
  const SkylanderInfo* sky = FindSkylander(ReadFigureId(s.data), ReadFigureVariant(s.data));
  s.name = sky ? std::string(sky->name) : std::string("Unknown figure");
  if (!options_.backup_dir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(options_.backup_dir, ec);
    s.source = options_.backup_dir / (s.name + " " + Hex(s.uid) + ".dump");
    // Its backup file already on the emulated portal through the menu (it lists that folder): the
    // toy would take over that slot and the game's writes from another state would go onto it
    // (#62). Left out, before the file in use is renamed or saved over.
    for (int slot = 0; slot < kMaxFigures; ++slot) {
      const std::optional<std::filesystem::path> in_use = emulated_->Source(slot);
      std::error_code same_ec;
      if (in_use && (*in_use == s.source || std::filesystem::equivalent(*in_use, s.source, same_ec))) {
        s.phase = RealSlot::Phase::kFailed;
        s.source.clear();
        Log("Hybrid portal: " + s.name + "'s backup file is on the portal through the menu; the toy is "
            "left out until that is taken off and the toy put on again");
        if (options_.on_backup_in_use) options_.on_backup_in_use(s.name);
        return;
      }
    }
  }
  // Copies fill the emulated portal from the top, the portal menu from the bottom.
  int target = -1;
  for (int slot = kMaxFigures - 1; slot >= 0; --slot) {
    if (!emulated_->HasFigure(slot)) {
      target = slot;
      break;
    }
  }
  // Loading: the blocks still to come are read while the game reads the copy. The backup file is
  // written once the copy is whole (Finish); the source marks the file as in use meanwhile.
  if (target < 0 ||
      !emulated_->PlaceLoadingFigure(target, s.data, s.read,
                                     s.source.empty() ? std::nullopt
                                                      : std::optional<std::filesystem::path>(s.source))) {
    s.phase = RealSlot::Phase::kFailed;
    s.source.clear();
    Log("Hybrid portal: the emulated portal is full; " + s.name + " is left out");
    return;
  }
  s.phase = RealSlot::Phase::kLoading;
  {
    std::lock_guard lock(mu_);
    copy_slot_[real_slot] = target;
    written_[real_slot] = s.data;  // the blocks not read yet are zero on both sides
    uid_[real_slot] = s.uid;
    mirrored_[real_slot] = true;   // the game's writes go to the toy from now on
  }
  Log("Hybrid portal: " + s.name + " from real slot " + std::to_string(real_slot) +
      " is in emulated slot " + std::to_string(target));
  if (options_.on_arrived) options_.on_arrived(target);
}

void RealPortalBridge::Fill(int real_slot, int block) {
  RealSlot& s = slots_[real_slot];
  const int copy = CopyOf(real_slot);
  const std::span<const uint8_t> bytes(s.data.data() + size_t(block) * kBlockSize, kBlockSize);
  if (copy < 0 || !emulated_->FillBlock(copy, block, bytes)) return;  // the game's own version stays
  std::lock_guard lock(mu_);
  std::copy(bytes.begin(), bytes.end(), written_[real_slot].begin() + size_t(block) * kBlockSize);
}

void RealPortalBridge::Finish(int real_slot) {
  RealSlot& s = slots_[real_slot];
  // A backup with other data than the toy's (played from the portal menu meanwhile, or an older
  // state) is kept under another name rather than overwritten. Once, before the copy is saved there.
  if (!s.source.empty() && !s.backup_checked) {
    s.backup_checked = true;
    std::error_code ec;
    if (std::filesystem::exists(s.source, ec)) {
      const std::optional<FigureData> old = LoadFigureFile(s.source);
      if (!old || *old != s.data) {
        const std::filesystem::path kept =
            UniqueFigurePath(options_.backup_dir, s.name + " " + Hex(s.uid) + " before");
        std::filesystem::rename(s.source, kept, ec);
        Log("Hybrid portal: the backup of " + s.name + " had other data; kept as " +
            Utf8Path(kept.filename()));
      }
    }
  }
  const auto done = [&] {
    s.phase = RealSlot::Phase::kMirrored;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - s.seen).count();
    Log("Hybrid portal: " + s.name + " read whole in " + std::to_string(ms) + " ms");
  };
  const int copy = CopyOf(real_slot);
  if (copy < 0 || !emulated_->IsLoading(copy)) {
    // The copy isn't on the emulated portal (taken off meanwhile): the backup gets the toy as read.
    if (!s.source.empty() && !SaveFigureFileAtomic(s.source, s.data)) {
      Log("Hybrid portal: could not save the backup of " + s.name);
    }
    return done();
  }
  // The copy as it is now (with the game's writes) into the backup file, then the copy is whole;
  // a write that came in between makes FinishLoading refuse, and the newer state is saved.
  for (int attempt = 0; attempt < 3; ++attempt) {
    const std::optional<FigureData> now = emulated_->Figure(copy);
    if (!now) return done();
    if (!s.source.empty() && !SaveFigureFileAtomic(s.source, *now)) {
      Log("Hybrid portal: could not save the backup of " + s.name);
    }
    if (emulated_->FinishLoading(copy, *now)) return done();
  }
  // The game kept writing: again on the next step (kFinishing).
}

void RealPortalBridge::DropCopy(int real_slot) {
  RealSlot& s = slots_[real_slot];
  int copy = -1;
  {
    std::lock_guard lock(mu_);
    copy = copy_slot_[real_slot];
    copy_slot_[real_slot] = -1;
    mirrored_[real_slot] = false;
    uid_[real_slot] = 0;
    writes_.erase(std::remove_if(writes_.begin(), writes_.end(),
                                 [&](const PendingWrite& w) { return w.real_slot == real_slot; }),
                  writes_.end());
  }
  if (copy >= 0 && emulated_->IsLoading(copy) &&
      (s.source.empty() || emulated_->Source(copy) == s.source)) {
    emulated_->RemoveFigure(copy);
  }
}

void RealPortalBridge::ReadMissed(int real_slot, const char* why) {
  RealSlot& s = slots_[real_slot];
  s.retry_at = Clock::now() + options_.read_retry_pause;
  if (++s.attempts < kReadAttempts) return;
  // Given up: a loading copy can't become whole, so it goes (the game sees the toy lifted).
  if (s.phase == RealSlot::Phase::kLoading) DropCopy(real_slot);
  s.phase = RealSlot::Phase::kFailed;
  Log("Hybrid portal: could not read the figure in real slot " + std::to_string(real_slot) +
      " (block " + std::to_string(s.reading) + ", " + why + "); lift it off and put it back");
}

void RealPortalBridge::Gone(int real_slot, const char* why) {
  RealSlot& s = slots_[real_slot];
  size_t dropped = 0;
  int copy = -1;
  {
    std::lock_guard lock(mu_);
    copy = copy_slot_[real_slot];
    const auto before = writes_.size();
    writes_.erase(std::remove_if(writes_.begin(), writes_.end(),
                                 [&](const PendingWrite& w) { return w.real_slot == real_slot; }),
                  writes_.end());
    dropped = before - writes_.size();
    copy_slot_[real_slot] = -1;
    mirrored_[real_slot] = false;
    uid_[real_slot] = 0;
  }
  if (pending_ && pending_->cmd != 'M' && pending_->slot == real_slot) pending_.reset();
  // Only the copy itself: the portal menu may have put another figure into that slot meanwhile.
  // A loading copy without a backup file has no source to check: it is the bridge's own until whole.
  if (copy >= 0 && ((!s.source.empty() && emulated_->Source(copy) == s.source) ||
                    (s.source.empty() && s.phase == RealSlot::Phase::kLoading && emulated_->IsLoading(copy)))) {
    emulated_->RemoveFigure(copy);
  }
  if (s.phase == RealSlot::Phase::kMirrored || s.phase == RealSlot::Phase::kLoading ||
      s.phase == RealSlot::Phase::kFinishing) {
    Log(std::string("Hybrid portal: real slot ") + std::to_string(real_slot) + " " + why +
        (dropped ? "; " + std::to_string(dropped) + " blocks not written, they are in the backup"
                 : std::string()));
  }
  s = RealSlot{};
}

int RealPortalBridge::FindCopy(const std::filesystem::path& source) const {
  if (source.empty()) return -1;
  for (int slot = 0; slot < kMaxFigures; ++slot) {
    if (emulated_->Source(slot) == source) return slot;
  }
  return -1;
}

// The portal menu lifts a player's figure when it opens and may put it back elsewhere; follow the
// copy by its source path.
void RealPortalBridge::TrackCopies() {
  for (int slot = 0; slot < kMaxFigures; ++slot) {
    const RealSlot& s = slots_[slot];
    if (s.phase != RealSlot::Phase::kMirrored && s.phase != RealSlot::Phase::kLoading &&
        s.phase != RealSlot::Phase::kFinishing) {
      continue;
    }
    if (s.source.empty()) continue;  // without backup files the copy stays where it was put
    const int copy = FindCopy(s.source);
    std::lock_guard lock(mu_);
    copy_slot_[slot] = copy;
  }
}

void RealPortalBridge::OnEmulatedWrite(int emulated_slot, const FigureData& data) {
  std::lock_guard lock(mu_);
  for (int slot = 0; slot < kMaxFigures; ++slot) {
    if (!mirrored_[slot] || copy_slot_[slot] != emulated_slot) continue;
    for (int block = 0; block < int(kBlockCount); ++block) {
      if (!Writable(block)) continue;
      const auto begin = data.begin() + block * kBlockSize;
      const auto old_begin = written_[slot].begin() + block * kBlockSize;
      if (std::equal(begin, begin + kBlockSize, old_begin)) continue;
      PendingWrite w;
      w.real_slot = slot;
      w.block = block;
      std::copy_n(begin, kBlockSize, w.bytes.begin());
      // A block queued again replaces its older version that hasn't gone out yet, also one waiting
      // out its pause after a refusal; one sent and not answered yet stays, the new one goes behind.
      auto queued = std::find_if(writes_.begin(), writes_.end(), [&](const PendingWrite& p) {
        return !p.in_flight && p.real_slot == slot && p.block == block;
      });
      if (queued != writes_.end()) {
        queued->bytes = w.bytes;
      } else {
        writes_.push_back(w);
      }
    }
    written_[slot] = data;
    return;
  }
}

void RealPortalBridge::OnSpeakerSwitch(const Report& report) {
  if (report[0] != 'M') return;
  std::lock_guard lock(mu_);
  speaker_switch_ = report;
  speaker_switch_due_ = true;
}

// Bridge thread: the real portal's speaker as its answer (or its silence) says, logged when it changes.
void RealPortalBridge::NoteSpeaker(bool speaker, const char* why) {
  real_speaker_.store(speaker);
  if (speaker_known_ == int(speaker)) return;
  speaker_known_ = int(speaker);
  Log(std::string("Hybrid portal: the real portal ") + why +
      (speaker ? "; the game's speaker sound goes there with portal_audio = auto" : ""));
}

void RealPortalBridge::OnLight(const Report& report) {
  if (report[0] != 'C' && report[0] != 'J' && report[0] != 'L') return;
  // C sets the whole portal; J (a side's colour with a fade) and L (the trap light) say which side.
  const uint16_t key = uint16_t(report[0] << 8) | (report[0] == 'C' ? 0 : report[1]);
  std::lock_guard lock(mu_);
  lights_[key] = {report, true};
}

std::vector<uint32_t> RealPortalBridge::RealUids() const {
  std::lock_guard lock(mu_);
  std::vector<uint32_t> uids;
  for (int slot = 0; slot < kMaxFigures; ++slot) {
    if (mirrored_[slot]) uids.push_back(uid_[slot]);
  }
  return uids;
}

bool RealPortalBridge::IsCopySlot(int emulated_slot) const {
  std::lock_guard lock(mu_);
  for (int slot = 0; slot < kMaxFigures; ++slot) {
    if (mirrored_[slot] && copy_slot_[slot] == emulated_slot) return true;
  }
  return false;
}

}  // namespace skylanders::portal
