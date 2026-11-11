#pragma once

#include <array>
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
