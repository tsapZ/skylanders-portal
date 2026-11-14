#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "portal/audio.h"

namespace skylanders::portal {

// A raw portal report: what a portal sends and receives, with no console framing.
constexpr size_t kReportSize = 32;
using Report = std::array<uint8_t, kReportSize>;

// One figure's tag data: 64 blocks of 16 bytes.
constexpr size_t kBlockSize = 16;
constexpr size_t kBlockCount = 64;
constexpr size_t kFigureSize = kBlockSize * kBlockCount;
using FigureData = std::array<uint8_t, kFigureSize>;

// A portal holds up to 16 figures.
constexpr int kMaxFigures = 16;

// The game's view of a portal. Implemented by SoftwarePortal and UsbPortal.
class PortalDevice {
 public:
  virtual ~PortalDevice() = default;

  // Game to portal: one command report.
  virtual void Write(const Report& report) = 0;

  // Portal to game: the next report. A queued command reply if there is one, otherwise a status report.
  virtual Report Read() = 0;

  // Game to portal: decoded speaker audio (8 kHz mono PCM). Default: no speaker, dropped.
  virtual void WriteAudio(std::span<const int16_t> pcm) { (void)pcm; }

  // Whether this is an Xbox 360 portal. Its answer to the speaker switch has no audio version, so
  // any answer from it means a speaker.
  virtual bool IsXbox360Portal() const { return false; }
};

}  // namespace skylanders::portal
