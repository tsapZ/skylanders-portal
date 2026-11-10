#include "portal/audio.h"

namespace skylanders::portal {

void PcmPacketizer::Add(std::span<const int16_t> samples,
                        const std::function<void(const AudioPacket&)>& emit) {
  for (int16_t sample : samples) {
    pending_[count_ * 2] = uint8_t(sample & 0xFF);
    pending_[count_ * 2 + 1] = uint8_t((uint16_t(sample) >> 8) & 0xFF);
    if (++count_ == kAudioSamplesPerPacket) {
      emit(pending_);
      count_ = 0;
    }
  }
}

void AudioPacketQueue::Push(const AudioPacket& packet) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (closed_) return;
    if (packets_.size() >= capacity_) {
      packets_.pop_front();
      ++dropped_;
    }
    packets_.push_back(packet);
  }
  cv_.notify_one();
}

std::optional<AudioPacket> AudioPacketQueue::PopWait(std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(mu_);
  cv_.wait_for(lock, timeout, [this] { return closed_ || !packets_.empty(); });
  if (packets_.empty()) return std::nullopt;
  AudioPacket p = packets_.front();
  packets_.pop_front();
  return p;
}

size_t AudioPacketQueue::Size() const {
  std::lock_guard<std::mutex> lock(mu_);
  return packets_.size();
}

size_t AudioPacketQueue::Dropped() const {
  std::lock_guard<std::mutex> lock(mu_);
  return dropped_;
}

void AudioPacketQueue::Close() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    closed_ = true;
    packets_.clear();
  }
  cv_.notify_all();
}

}  // namespace skylanders::portal
