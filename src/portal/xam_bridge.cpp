#include "portal/xam_bridge.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "portal/xbox_frame.h"

namespace skylanders::portal {

uint32_t XamBridge::Read(PortalDevice* device, std::span<uint8_t> buffer, uint32_t& bytes_read,
                         uint16_t& state) {
  if (!device) {
    delivered_last_ = false;
    return kXamDeviceNotConnected;
  }
  // The game keeps reading until a read says "no new data" (state 0), and only then sends its
  // own commands; a failed read means the portal is gone. A PortalDevice always has a report to
  // give (a status report when nothing else is queued), so every other read answers "no new
  // data": each of the game's polls gets one report, then it goes on to write.
  if (delivered_last_) {
    delivered_last_ = false;
    bytes_read = 0;
    state = 0;
    return kXamSuccess;
  }
  const Report report = device->Read();
  // Nothing to give (the emulated portal waiting for a block of a toy it reads from the real portal,
  // a HID portal with nothing queued): "no new data", as the real portal answers while it is busy.
  // It went to the game as a report of zeros before.
  if (report[0] == 0) {
    delivered_last_ = false;
    bytes_read = 0;
    state = 0;
    return kXamSuccess;
  }
  // A real portal only streams status reports once activated ('A 01'). Trap Team restarts its
  // portal handshake when it sees an inactive one, so those are withheld as "no new data".
  constexpr size_t kStatusActiveByte = 6;
  if (report[0] == 'S' && !(report[kStatusActiveByte] & 1)) {
    delivered_last_ = false;
    bytes_read = 0;
    state = 0;
    return kXamSuccess;
  }
  std::array<uint8_t, kFrameSize> frame{};
  FrameFromReport(report, frame.data());
  const size_t n = std::min(buffer.size(), frame.size());
  std::memcpy(buffer.data(), frame.data(), n);
  bytes_read = static_cast<uint32_t>(n);
  state = 1;
  delivered_last_ = true;
  return kXamSuccess;
}

uint32_t XamBridge::Write(PortalDevice* device, std::span<const uint8_t> buffer) {
  if (!device) return kXamDeviceNotConnected;
  constexpr uint8_t kSpeakerFrameType = 0x17;  // 0B 17: G.726 speaker audio
  if (buffer.size() > 2 && buffer[0] == kFrameHeader0 && buffer[1] == kSpeakerFrameType) {
    pcm_.clear();
    DecodeSpeakerAudio(buffer.subspan(2), speaker_decoder_, pcm_);
    device->WriteAudio(pcm_);
    return kXamSuccess;
  }
  std::array<uint8_t, kFrameSize> frame{};
  std::memcpy(frame.data(), buffer.data(), std::min(buffer.size(), frame.size()));
  if (auto report = ReportFromFrame(frame.data())) {
    if ((*report)[0] == 'M') speaker_decoder_.Reset();  // speaker switched: a new stream starts
    device->Write(*report);
  }
  return kXamSuccess;
}

}  // namespace skylanders::portal
