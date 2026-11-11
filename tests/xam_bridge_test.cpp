#include <vector>

#include "portal/g726.h"

#include "portal/xam_bridge.h"
#include "test_util.h"

using namespace skylanders::portal;

namespace {
struct FakeDevice : PortalDevice {
  std::vector<Report> writes;
  std::vector<int16_t> audio;
  Report next{};
  void Write(const Report& r) override { writes.push_back(r); }
  Report Read() override { return next; }
  void WriteAudio(std::span<const int16_t> pcm) override {
    audio.insert(audio.end(), pcm.begin(), pcm.end());
  }
};
}  // namespace

int main() {
  // A real portal only sends status reports once the game has activated it ('A 01'); an inactive
  // status report is withheld ("no new data"), since Trap Team resets its portal handshake on one.
  {
    XamBridge quiet;
    FakeDevice idle;
    idle.next[0] = 'S';  // active flag (byte 6, bit 0) clear
    std::array<uint8_t, 32> b{};
    uint32_t n = 99;
    uint16_t s = 7;
    CHECK(quiet.Read(&idle, b, n, s) == kXamSuccess);
    CHECK(s == 0 && n == 0);
    CHECK(quiet.Read(&idle, b, n, s) == kXamSuccess);
    CHECK(s == 0 && n == 0);
    idle.next[0] = 'R';  // replies are always delivered
    CHECK(quiet.Read(&idle, b, n, s) == kXamSuccess);
    CHECK(s == 1 && n == 32 && b[2] == 'R');
    // An empty report (a portal busy reading a figure, nothing queued) is "no new data" too, not a
    // report of zeros.
    idle.next = Report{};
    CHECK(quiet.Read(&idle, b, n, s) == kXamSuccess);  // right after a report: "no new data" anyway
    CHECK(quiet.Read(&idle, b, n, s) == kXamSuccess);
    CHECK(s == 0 && n == 0);
  }

  XamBridge bridge;
  FakeDevice dev;
  dev.next[0] = 'S';
  dev.next[1] = 0x01;
  dev.next[6] = 0x01;  // active

  // The game drains reads until one says "no new data" (state 0), then sends its commands. So
  // reads alternate: a report (state 1), then "nothing new" (state 0, no bytes).
  std::array<uint8_t, 32> buf{};
  uint32_t bytes = 0;
  uint16_t state = 7;
  CHECK(bridge.Read(&dev, buf, bytes, state) == kXamSuccess);
  CHECK(buf[0] == 0x0B && buf[1] == 0x14 && buf[2] == 'S' && buf[3] == 0x01);
  CHECK(bytes == 32);
  CHECK(state == 1);
  CHECK(bridge.Read(&dev, buf, bytes, state) == kXamSuccess);
  CHECK(state == 0);
  CHECK(bytes == 0);
  CHECK(bridge.Read(&dev, buf, bytes, state) == kXamSuccess);
  CHECK(state == 1 && bytes == 32);
  CHECK(bridge.Read(&dev, buf, bytes, state) == kXamSuccess);  // back to "nothing new"
  CHECK(state == 0);

  // A shorter buffer gets only what fits.
  std::array<uint8_t, 8> small{};
  CHECK(bridge.Read(&dev, small, bytes, state) == kXamSuccess);
  CHECK(state == 1 && bytes == 8 && small[0] == 0x0B);

  // Command write: header stripped.
  std::array<uint8_t, 32> frame{};
  frame[0] = 0x0B;
  frame[1] = 0x14;
  frame[2] = 'A';
  frame[3] = 0x01;
  CHECK(bridge.Write(&dev, frame) == kXamSuccess);
  REQUIRE_OR_RETURN(dev.writes.size() == 1);
  CHECK(dev.writes[0][0] == 'A' && dev.writes[0][1] == 0x01);

  // Frame without the header: ignored, still success (the game retries).
  std::array<uint8_t, 32> bad{};
  CHECK(bridge.Write(&dev, bad) == kXamSuccess);
  CHECK(dev.writes.size() == 1);

  // Speaker audio: 0B 17 frames carry 30 bytes of G.726 codes; the device gets 60 decoded samples.
  std::array<uint8_t, 32> speaker{};
  speaker[0] = 0x0B;
  speaker[1] = 0x17;
  for (size_t i = 2; i < speaker.size(); ++i) speaker[i] = uint8_t(i * 37);
  const auto codes = std::span<const uint8_t>(speaker).subspan(2);
  CHECK(bridge.Write(&dev, speaker) == kXamSuccess);
  G726Decoder ref;
  std::vector<int16_t> want;
  DecodeSpeakerAudio(codes, ref, want);
  CHECK(dev.audio.size() == 60 && dev.audio == want);
  CHECK(dev.writes.size() == 1);  // not treated as a command
  // One decoder across frames: the second frame continues from the first's state.
  dev.audio.clear();
  CHECK(bridge.Write(&dev, speaker) == kXamSuccess);
  std::vector<int16_t> next;
  DecodeSpeakerAudio(codes, ref, next);
  CHECK(dev.audio == next);
  // 'M' (speaker on/off) starts a new stream.
  const std::array<uint8_t, 4> m = {0x0B, 0x14, 'M', 0x01};
  CHECK(bridge.Write(&dev, m) == kXamSuccess);
  dev.audio.clear();
  CHECK(bridge.Write(&dev, speaker) == kXamSuccess);
  CHECK(dev.audio == want);

  // No device.
  CHECK(bridge.Read(nullptr, buf, bytes, state) == kXamDeviceNotConnected);
  CHECK(bridge.Write(nullptr, frame) == kXamDeviceNotConnected);
  return Finish("xam_bridge");
}
