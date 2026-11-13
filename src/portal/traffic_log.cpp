#include "portal/traffic_log.h"

#include <cstdio>
#include <utility>

#include "portal/figure_catalog.h"

namespace skylanders::portal {
namespace {

// The first few errors of a slot are named one by one, the rest only counted for its summary.
constexpr int kErrorsNamedPerSlot = 3;
// The game resets a portal that doesn't answer about every 20 s; the second to the sixth reset are
// named.
constexpr int kResetsNamed = 6;

std::string Hex(const Report& r, size_t n) {
  std::string out;
  char b[4];
  for (size_t i = 0; i < n; ++i) {
    std::snprintf(b, sizeof(b), i ? " %02X" : "%02X", r[i]);
    out += b;
  }
  return out;
}

std::string Fmt(const char* format, int a, int b = 0, int c = 0, int d = 0, int e = 0) {
  char buf[200];
  std::snprintf(buf, sizeof(buf), format, a, b, c, d, e);
  return buf;
}

}  // namespace

PortalTrafficLog::PortalTrafficLog(std::string tag, Sink sink, int max_lines)
    : tag_(std::move(tag)), sink_(std::move(sink)), lines_left_(max_lines) {}

void PortalTrafficLog::Line(const std::string& text) {
  if (lines_left_ <= 0 || !sink_) return;
  if (--lines_left_ == 0) {
    sink_("Portal (" + tag_ + "): " + text + " (no more portal lines this session)");
    return;
  }
  sink_("Portal (" + tag_ + "): " + text);
}

void PortalTrafficLog::OnCommand(const Report& report) {
  std::lock_guard<std::mutex> lock(mu_);
  if (report[0] == 'Q') asked_read_slot_ = report[1] & 0x0F;
  if (report[0] == 'W') asked_write_slot_ = report[1] & 0x0F;
  if (report[0] == 'R') {
    ++resets_;
    if (resets_ >= 2 && resets_ <= kResetsNamed) {
      Line(Fmt("the game reset the portal again (%d times so far): its replies may not be coming through",
               resets_));
    }
  }
}

void PortalTrafficLog::OnReply(const Report& report) {
  std::lock_guard<std::mutex> lock(mu_);
  switch (report[0]) {
    case 0x52:  // 'R' ready: the portal's id
      if (!ready_seen_) {
        ready_seen_ = true;
        Line("the portal answered (ready reply " + Hex(report, 3) + ")");
      }
      return;
    case 0x53: {  // 'S' status: 2 bits a slot, 0 empty, 1 present, 2 being taken off, 3 just put on
      const uint32_t states = uint32_t(report[1]) | (uint32_t(report[2]) << 8) | (uint32_t(report[3]) << 16) |
                              (uint32_t(report[4]) << 24);
      for (int slot = 0; slot < kMaxFigures; ++slot) {
        const uint32_t state = (states >> (2 * slot)) & 0x03;
        const bool on = state == 1 || state == 3;
        Slot& s = slots_[slot];
        if (on && !s.on) {
          Reset(s);
          s.on = true;
          Line(Fmt("slot %d: a figure was put on", slot));
        } else if (!on && s.on) {
          Line(Fmt("slot %d: taken off (%d blocks read, %d written, %d reads and %d writes refused)", slot, s.reads,
                   s.writes, s.read_errors, s.write_errors));
          Reset(s);
        }
      }
      return;
    }
    case 0x51: {  // 'Q' read reply: slot (0x10 set when it worked), block, 16 bytes
      const bool ok = (report[1] & 0x10) != 0;
      const int slot = ok || asked_read_slot_ < 0 ? report[1] & 0x0F : asked_read_slot_;
      const int block = report[2];
      Slot& s = slots_[slot];
      if (ok) {
        ++s.reads;
        if (block == 1 && !s.named) {
          s.named = true;
          // Block 1 holds the figure's id (bytes 0-1) and variant (bytes 12-13), as figure_file.h.
          const uint16_t id = uint16_t(report[3] | (report[4] << 8));
          const uint16_t variant = uint16_t(report[15] | (report[16] << 8));
          bool exact = false;
          const SkylanderInfo* info = FindSkylander(id, variant, &exact);
          Line(Fmt("slot %d: the game read the figure: ", slot) +
               (!info   ? std::string("not in the catalog")
                : exact ? std::string(info->name)
                        : "another variant of " + std::string(info->name)) +
               Fmt(" (id %d, variant %04X)", id, variant));
        }
      } else {
        ++s.read_errors;
        if (s.read_errors <= kErrorsNamedPerSlot) {
          Line(Fmt("slot %d: the portal couldn't read block %d (reply ", slot, block) + Hex(report, 3) + ")");
        }
      }
      return;
    }
    case 0x57: {  // 'W' write reply: slot (0x10 set when it worked), block
      const bool ok = (report[1] & 0x10) != 0;
      const int slot = ok || asked_write_slot_ < 0 ? report[1] & 0x0F : asked_write_slot_;
      const int block = report[2];
      Slot& s = slots_[slot];
      if (ok) {
        ++s.writes;
      } else {
        ++s.write_errors;
        if (s.write_errors <= kErrorsNamedPerSlot) {
          Line(Fmt("slot %d: the portal didn't take the write to block %d (reply ", slot, block) + Hex(report, 3) +
               "): the figure keeps its old data there");
        }
      }
      return;
    }
    default:
      return;
  }
}

}  // namespace skylanders::portal
