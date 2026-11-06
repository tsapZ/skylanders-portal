// PortalTrafficLog: the lines a real portal's traffic turns into (figure on / off, the figure's name,
// refused reads and writes, repeated resets) and their cap.

#include <initializer_list>
#include <string>
#include <vector>

#include "portal/traffic_log.h"
#include "test_util.h"

using namespace skylanders::portal;

namespace {

Report R(std::initializer_list<uint8_t> bytes) {
  Report r{};
  size_t i = 0;
  for (uint8_t b : bytes) r[i++] = b;
  return r;
}

// A status frame with `state` (2 bits) in `slot`.
Report Status(int slot, uint32_t state) {
  const uint32_t states = state << (2 * slot);
  return R({0x53, uint8_t(states), uint8_t(states >> 8), uint8_t(states >> 16), uint8_t(states >> 24), 0x01, 0x01});
}

bool Has(const std::vector<std::string>& lines, const std::string& part) {
  for (const auto& l : lines) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

int main() {
  std::vector<std::string> lines;
  PortalTrafficLog log("usb", [&](const std::string& l) { lines.push_back(l); });

  // The ready reply is named once.
  log.OnReply(R({0x52, 0x02, 0x1B}));
  log.OnReply(R({0x52, 0x02, 0x1B}));
  CHECK(lines.size() == 1);
  CHECK(Has(lines, "Portal (usb): the portal answered (ready reply 52 02 1B)"));

  // A figure put on slot 2 (state 3), still there (1): one line.
  log.OnReply(Status(2, 3));
  log.OnReply(Status(2, 1));
  CHECK(Has(lines, "slot 2: a figure was put on"));
  CHECK(lines.size() == 2);

  // The game reads block 1: Chop Chop (id 30, variant 0) by name, once.
  Report block1 = R({0x51, 0x12, 0x01});
  block1[3] = 30;
  log.OnCommand(R({'Q', 0x02, 0x01}));
  log.OnReply(block1);
  log.OnReply(block1);
  CHECK(Has(lines, "slot 2: the game read the figure: Chop Chop (id 30, variant 0000)"));
  CHECK(lines.size() == 3);

  // A refused read whose reply lacks the slot (Q 01): named for the slot the game asked for.
  log.OnCommand(R({'Q', 0x02, 0x05}));
  log.OnReply(R({0x51, 0x01, 0x05}));
  CHECK(Has(lines, "slot 2: the portal couldn't read block 5 (reply 51 01 05)"));

  // A refused write, and a taken write.
  log.OnCommand(R({'W', 0x02, 0x08}));
  log.OnReply(R({0x57, 0x02, 0x08}));
  CHECK(Has(lines, "slot 2: the portal didn't take the write to block 8"));
  log.OnCommand(R({'W', 0x02, 0x09}));
  log.OnReply(R({0x57, 0x12, 0x09}));

  // Taken off: the summary.
  log.OnReply(Status(2, 0));
  CHECK(Has(lines, "slot 2: taken off (2 blocks read, 1 written, 1 reads and 1 writes refused)"));

  // The first reset is normal, the second on is named.
  log.OnCommand(R({'R'}));
  CHECK(!Has(lines, "reset the portal again"));
  log.OnCommand(R({'R'}));
  CHECK(Has(lines, "the game reset the portal again (2 times so far)"));

  // The cap: at most max_lines, the last one says so.
  std::vector<std::string> few;
  PortalTrafficLog capped("Xbox 360", [&](const std::string& l) { few.push_back(l); }, 3);
  for (int i = 0; i < 10; ++i) {
    capped.OnReply(Status(0, 3));
    capped.OnReply(Status(0, 0));
  }
  CHECK(few.size() == 3);
  CHECK(few.back().find("(no more portal lines this session)") != std::string::npos);
  CHECK(few.front().rfind("Portal (Xbox 360): ", 0) == 0);

  return Finish("traffic_log");
}
