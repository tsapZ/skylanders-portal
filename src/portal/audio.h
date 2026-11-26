#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <span>

namespace skylanders::portal {

// Portal speaker audio is 8 kHz mono. The Xbox 360 game sends it G.726-encoded (see g726.h); the
// Wii U/PS3 Traptanium portal takes it as 64-byte packets of 32 signed 16-bit little-endian
// samples on its interrupt OUT endpoint.
constexpr int kAudioSampleRate = 8000;
constexpr size_t kAudioPacketSize = 64;
constexpr size_t kAudioSamplesPerPacket = 32;
using AudioPacket = std::array<uint8_t, kAudioPacketSize>;

// Reply bytes 2-3 to the 'M' command that tell the game the portal has a speaker.
inline constexpr std::array<uint8_t, 2> kAudioCapableVersion = {0x01, 0x19};

// A real portal's speaker writes: after kGiveUpAfter failures in a row it gives up for good, and the
// speaker's sound goes elsewhere (Core of Light #102: a portal on WinUSB took none, and every try held
// up the game for half a second).
class SpeakerWriteHealth {
 public:
  static constexpr int kGiveUpAfter = 3;
  // One write's result. Returns true only for the write that made it give up (so the caller logs once).
  bool Note(bool ok) {
    if (gave_up_.load()) return false;
    if (ok) {
      failures_ = 0;
      return false;
    }
    if (++failures_ < kGiveUpAfter) return false;
    gave_up_.store(true);
    return true;
  }
  bool GaveUp() const { return gave_up_.load(); }   // any thread
 private:
  int failures_ = 0;               // writer thread only
  std::atomic<bool> gave_up_{false};
};

// A speaker packet written to the HID driver as one report (report ID + 64 bytes) that came back with only the report
// ID and the first half taken (33 of 65): the portal's HID description gives its output reports 32 bytes, and Windows'
// own driver sends just that much of a longer write. A PlayStation Traptanium portal on that driver played every
// packet's first half only and sounded choppy; two 33-byte reports per packet played clean (Core of Light #154,
// 2026-11-26, both logs). Any other count (a failed write, the whole packet, another cut) is not this case.
inline bool HidSpeakerWriteTookHalf(int taken) { return taken == static_cast<int>(1 + kAudioPacketSize / 2); }

// Collects PCM samples into full AudioPackets; leftover samples wait for the next Add.
class PcmPacketizer {
 public:
  void Add(std::span<const int16_t> samples, const std::function<void(const AudioPacket&)>& emit);

 private:
  AudioPacket pending_{};
  size_t count_ = 0;  // samples in pending_
};

// Receives decoded speaker audio. Called on the game's thread: must not block.
class PortalAudioSink {
 public:
  virtual ~PortalAudioSink() = default;
  virtual void Submit(std::span<const int16_t> samples) = 0;
};

// Bounded packet queue between the game's thread (Push, never blocks) and a writer thread.
// When full, the oldest packet is dropped.
class AudioPacketQueue {
 public:
  explicit AudioPacketQueue(size_t capacity) : capacity_(capacity) {}
  void Push(const AudioPacket& packet);
  // Waits up to `timeout` for a packet. nullopt on timeout or after Close().
  std::optional<AudioPacket> PopWait(std::chrono::milliseconds timeout);
  size_t Size() const;
  size_t Dropped() const;
  void Close();

 private:
  const size_t capacity_;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<AudioPacket> packets_;
  size_t dropped_ = 0;
  bool closed_ = false;
};

}  // namespace skylanders::portal
