#pragma once

// PortalTrafficLog: what a real portal did, in plain lines, for a player's log (F4 > Game > Report a
// problem). The raw traffic is logged only for the first few reports; this follows all of it and says
// what matters when a portal or a figure doesn't work: a figure put on or taken off (and in which
// slot), which figure the game read (its name from the catalog), the reads and writes the portal
// refused, and the game resetting the portal again and again (its sign that the replies don't come
// through). Shared by the HID portals (UsbPortal) and the Xbox 360 one (portal_rex.cpp). Lines go to a
// sink, so this has no ReXGlue dependency and is unit-tested (tests/traffic_log_test.cpp); at most
// `max_lines` of them, so a long session can't fill the log.

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "portal/portal_device.h"

namespace skylanders::portal {

class PortalTrafficLog {
 public:
  using Sink = std::function<void(const std::string&)>;

  // `tag` names the portal in each line ("usb", "Xbox 360").
  PortalTrafficLog(std::string tag, Sink sink, int max_lines = 300);

  // The game's command to the portal.
  void OnCommand(const Report& report);
  // A report the portal gave the game: a command's reply or a status frame.
  void OnReply(const Report& report);

 private:
  struct Slot {
    bool on = false;
    bool named = false;
    int reads = 0;
    int read_errors = 0;
    int writes = 0;
    int write_errors = 0;
  };

  void Line(const std::string& text);  // callers hold mu_
  void Reset(Slot& s) { s = Slot{}; }

  std::mutex mu_;
  std::string tag_;
  Sink sink_;
  int lines_left_;
  int resets_ = 0;
  // The slot of the game's last read / write command: some portals answer a failed one without it.
  int asked_read_slot_ = -1;
  int asked_write_slot_ = -1;
  bool ready_seen_ = false;
  std::array<Slot, kMaxFigures> slots_{};
};

}  // namespace skylanders::portal
