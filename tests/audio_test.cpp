#include <chrono>
#include <thread>
#include <vector>

#include "portal/audio.h"
#include "test_util.h"

using namespace skylanders::portal;

int main() {
  // PCM is repacked into the Wii U/PS3 portal's 64-byte packets: 32 signed 16-bit little-endian
  // samples. Leftover samples wait for the next call.
  {
    PcmPacketizer packetizer;
    std::vector<AudioPacket> packets;
    auto emit = [&](const AudioPacket& p) { packets.push_back(p); };
    std::vector<int16_t> pcm(60);
    for (int i = 0; i < 60; ++i) pcm[i] = int16_t(i * 100 - 3000);
    packetizer.Add(pcm, emit);
    CHECK(packets.size() == 1);
    CHECK(packets[0][0] == uint8_t(-3000 & 0xFF) && packets[0][1] == uint8_t((-3000 >> 8) & 0xFF));
    CHECK(packets[0][62] == uint8_t(100) && packets[0][63] == uint8_t(0));  // sample 31 = 100
    packetizer.Add(std::span<const int16_t>(pcm).first(4), emit);
    REQUIRE_OR_RETURN(packets.size() == 2);
    // The second packet starts with sample 32 of the first call (3200 - 3000 = 200).
    CHECK(packets[1][0] == uint8_t(200) && packets[1][1] == 0);
  }

  // Queue: bounded, drops the oldest, never blocks the producer.
  AudioPacketQueue q(2);
  AudioPacket a1{}, a2{}, a3{};
  a1[0] = 1;
  a2[0] = 2;
  a3[0] = 3;
  q.Push(a1);
  q.Push(a2);
  q.Push(a3);
  CHECK(q.Dropped() == 1);
  CHECK(q.PopWait(std::chrono::milliseconds(0))->at(0) == 2);
  CHECK(q.PopWait(std::chrono::milliseconds(0))->at(0) == 3);
  CHECK(!q.PopWait(std::chrono::milliseconds(0)).has_value());
  // Close wakes a waiting consumer.
  std::thread t([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    q.Close();
  });
  CHECK(!q.PopWait(std::chrono::seconds(5)).has_value());
  t.join();

  // A portal's speaker gives up after three failed writes in a row, and only the third says so.
  {
    SpeakerWriteHealth health;
    CHECK(!health.Note(false));
    CHECK(!health.Note(false));
    CHECK(!health.Note(true));  // a success resets the count
    CHECK(!health.Note(false));
    CHECK(!health.Note(false));
    CHECK(health.Note(false));  // the third failure in a row gives up
    CHECK(health.GaveUp());
    CHECK(!health.Note(false));  // not again
    CHECK(!health.Note(true));   // a later success doesn't undo it
    CHECK(health.GaveUp());
  }

  // A 65-byte speaker write on Windows' HID driver that took 33 bytes (report ID + the first half) switches the
  // writer to two reports a packet (Core of Light #154); a failed write, the whole packet or another count doesn't.
  CHECK(HidSpeakerWriteTookHalf(33));
  CHECK(!HidSpeakerWriteTookHalf(65));
  CHECK(!HidSpeakerWriteTookHalf(-1));
  CHECK(!HidSpeakerWriteTookHalf(0));
  CHECK(!HidSpeakerWriteTookHalf(17));
  return Finish("audio");
}
