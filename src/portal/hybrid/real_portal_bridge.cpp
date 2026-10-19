#include "portal/hybrid/real_portal_bridge.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <system_error>

#include "portal/figure_catalog.h"
#include "portal/figure_file.h"

namespace skylanders::portal {

namespace {

// How long the real portal gets to answer a block read or write before it is asked again, and how
// often; then the figure (or the block) is given up.
constexpr auto kReplyTimeout = std::chrono::milliseconds(250);
constexpr int kAttempts = 3;
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

void RealPortalBridge::Step(PortalDevice* real) {
  if (real != real_) {
    for (int slot = 0; slot < kMaxFigures; ++slot) {
      if (slots_[slot].phase != RealSlot::Phase::kEmpty) Gone(slot, "the real portal went away");
    }
    real_ = real;
    pending_.reset();
    if (real_) {
      // Ready, then activate: the portal then reports its figures, each as "added".
      real_->Write(Make({'R'}));
      real_->Write(Make({'A', 0x01}));
      Log("Hybrid portal: real portal connected");
    }
  }
  if (!real_) return;
  for (int i = 0; i < kReportsPerStep; ++i) {
    const Report report = real_->Read();
    if (report[0] == 0) break;
    Handle(report);
  }
  if (pending_ && Clock::now() - pending_->sent > kReplyTimeout) {
    const Command timed_out = *pending_;
    pending_.reset();
    if (timed_out.cmd == 'Q') {
      RealSlot& s = slots_[timed_out.slot];
      if (s.phase == RealSlot::Phase::kReading && ++s.attempts >= kAttempts) {
        s.phase = RealSlot::Phase::kFailed;
        Log("Hybrid portal: could not read the figure in real slot " +
            std::to_string(timed_out.slot) + "; lift it off and put it back");
      }
    } else if (timed_out.cmd == 'W') {
      std::lock_guard lock(mu_);
      if (!writes_.empty() && ++writes_.front().attempts >= kAttempts) {
        Log("Hybrid portal: block " + std::to_string(writes_.front().block) +
            " could not be written to the real figure; it is in the backup file");
        writes_.pop_front();
      }
    }
  }
  TrackCopies();
  if (!pending_) SendNext();
}

void RealPortalBridge::Handle(const Report& report) {
  switch (report[0]) {
    case 0x53:
      HandleStatus(report);
      break;
    case 0x51: {  // a block read: 51, slot | 10 if a figure is there, block, 16 bytes
      if (!pending_ || pending_->cmd != 'Q') break;
      const int slot = report[1] & 0x0F;
      const int block = report[2];
      if (slot != pending_->slot || block != pending_->block) break;
      pending_.reset();
      RealSlot& s = slots_[slot];
      if (s.phase != RealSlot::Phase::kReading) break;
      if (!(report[1] & 0x10)) {
        if (++s.attempts >= kAttempts) s.phase = RealSlot::Phase::kFailed;
        break;
      }
      std::copy_n(report.begin() + 3, kBlockSize, s.data.begin() + block * kBlockSize);
      if (++s.next_block >= int(kBlockCount)) Arrived(slot);
      break;
    }
    case 0x57: {  // a block write: 57, slot | 10 if it was written, block
      if (!pending_ || pending_->cmd != 'W') break;
      const int slot = report[1] & 0x0F;
      const int block = report[2];
      if (slot != pending_->slot || block != pending_->block) break;
      pending_.reset();
      std::lock_guard lock(mu_);
      if (writes_.empty() || writes_.front().real_slot != slot || writes_.front().block != block) {
        break;
      }
      if (report[1] & 0x10) {
        writes_.pop_front();
      } else if (++writes_.front().attempts >= kAttempts) {
        Log("Hybrid portal: the real portal refused block " + std::to_string(block) +
            "; it is in the backup file");
        writes_.pop_front();
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
    } else if (!present && s.phase != RealSlot::Phase::kEmpty) {
      Gone(slot, "lifted off the real portal");
    }
  }
}

void RealPortalBridge::SendNext() {
  // Writes first, so the real figure keeps up with the game; then figures still being read. The
  // write goes out after the lock is released: the game's thread takes it for OnEmulatedWrite.
  std::optional<Report> write;
  {
    std::lock_guard lock(mu_);
    while (!writes_.empty() && !mirrored_[writes_.front().real_slot]) writes_.pop_front();
    if (!writes_.empty()) {
      const PendingWrite& w = writes_.front();
      write = Make({'W', uint8_t(w.real_slot), uint8_t(w.block)});
      std::copy(w.bytes.begin(), w.bytes.end(), write->begin() + 3);
      pending_ = Command{'W', w.real_slot, w.block, Clock::now()};
    }
  }
  if (write) {
    real_->Write(*write);
    return;
  }
  for (int slot = 0; slot < kMaxFigures; ++slot) {
    const RealSlot& s = slots_[slot];
    if (s.phase != RealSlot::Phase::kReading) continue;
    pending_ = Command{'Q', slot, s.next_block, Clock::now()};
    real_->Write(Make({'Q', uint8_t(slot), uint8_t(s.next_block)}));
    return;
  }
  std::optional<Report> light;
  {
    std::lock_guard lock(mu_);
    if (light_ && Clock::now() - last_light_ >= kLightInterval) {
      light = light_;
      light_.reset();
    }
  }
  if (light) {
    last_light_ = Clock::now();
    real_->Write(*light);  // no reply
  }
}

void RealPortalBridge::Arrived(int real_slot) {
  RealSlot& s = slots_[real_slot];
  s.uid = UidOf(s.data);
  const SkylanderInfo* sky = FindSkylander(ReadFigureId(s.data), ReadFigureVariant(s.data));
  const std::string name = sky ? std::string(sky->name) : std::string("Unknown figure");
  if (!options_.backup_dir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(options_.backup_dir, ec);
    s.source = options_.backup_dir / (name + " " + Hex(s.uid) + ".dump");
    // A backup with other data (played from the portal menu meanwhile, or an older state) is kept
    // under another name rather than overwritten.
    if (std::filesystem::exists(s.source)) {
      const std::optional<FigureData> old = LoadFigureFile(s.source);
      if (!old || *old != s.data) {
        const std::filesystem::path kept =
            UniqueFigurePath(options_.backup_dir, name + " " + Hex(s.uid) + " before");
        std::filesystem::rename(s.source, kept, ec);
        Log("Hybrid portal: the backup of " + name + " had other data; kept as " +
            kept.filename().string());
      }
    }
    if (!SaveFigureFileAtomic(s.source, s.data)) {
      Log("Hybrid portal: could not save the backup of " + name);
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
  if (target < 0) {
    s.phase = RealSlot::Phase::kFailed;
    Log("Hybrid portal: the emulated portal is full; " + name + " is left out");
    return;
  }
  emulated_->PlaceFigure(target, s.data,
                         s.source.empty() ? std::nullopt
                                          : std::optional<std::filesystem::path>(s.source));
  s.phase = RealSlot::Phase::kMirrored;
  {
    std::lock_guard lock(mu_);
    copy_slot_[real_slot] = target;
    written_[real_slot] = s.data;
    uid_[real_slot] = s.uid;
    mirrored_[real_slot] = true;
  }
  Log("Hybrid portal: " + name + " from real slot " + std::to_string(real_slot) +
      " is in emulated slot " + std::to_string(target));
  if (options_.on_arrived) options_.on_arrived(target);
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
  if (pending_ && pending_->slot == real_slot) pending_.reset();
  // Only the copy itself: the portal menu may have put another figure into that slot meanwhile.
  if (copy >= 0 && !s.source.empty() && emulated_->Source(copy) == s.source) {
    emulated_->RemoveFigure(copy);
  }
  if (s.phase == RealSlot::Phase::kMirrored) {
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
    if (s.phase != RealSlot::Phase::kMirrored) continue;
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
      // A block queued again replaces its older version that hasn't gone out yet.
      auto queued = std::find_if(writes_.begin() + (writes_.empty() ? 0 : 1), writes_.end(),
                                 [&](const PendingWrite& p) {
                                   return p.real_slot == slot && p.block == block;
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

void RealPortalBridge::OnLight(const Report& report) {
  std::lock_guard lock(mu_);
  light_ = report;
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
