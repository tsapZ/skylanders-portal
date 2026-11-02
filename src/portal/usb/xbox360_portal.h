#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <thread>

namespace skylanders::portal {

// A real wired Xbox 360 portal (1430:1F17) that Windows has on the WinUSB driver. Out of the box
// Windows gives it the Xbox controller driver, and it isn't a HID device either way, so UsbPortal
// (hidapi) can't open it; with WinUSB installed for it (Zadig, as Xenia wants it too) this class
// talks to it directly (TrapTeamRecomp #19).
//
// No translation: the Xbox 360 games already speak this portal's own frames through
// XamInputNonControllerGetRaw/SetRaw (32 bytes, 0B 14 and a command; 0B 17 speaker audio), so they
// go to its interrupt OUT pipe unchanged, and its answers from the IN pipe go back unchanged. A
// thread reads the answers into a queue the game's polls take from, so no poll waits for the device
// (Xenia's version waits up to 300 ms per read and plays at 3 FPS with it).
class Xbox360Portal {
 public:
  static constexpr size_t kFrameSize = 32;

  // Opens the first present 1430:1F17 on WinUSB, or nothing (IsOpen() false). Never throws.
  Xbox360Portal();
  ~Xbox360Portal();

  Xbox360Portal(const Xbox360Portal&) = delete;
  Xbox360Portal& operator=(const Xbox360Portal&) = delete;

  bool IsOpen() const { return winusb_ != nullptr; }
  // True once a transfer failed for another reason than a timeout (unplugged, driver gone).
  bool SeemsDisconnected() const { return failed_.load(); }

  // One answer frame for the game in `buffer`, or bytes_read = 0 when none is waiting. False when
  // the device is gone.
  bool Read(std::span<uint8_t> buffer, uint32_t& bytes_read);
  // The game's frame, unchanged. False when the device is gone.
  bool Write(std::span<const uint8_t> buffer);

 private:
  void ReaderLoop();

  void* file_ = nullptr;    // HANDLE of the device interface
  void* winusb_ = nullptr;  // WINUSB_INTERFACE_HANDLE
  uint8_t in_pipe_ = 0;
  uint8_t out_pipe_ = 0;
  std::thread reader_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> failed_{false};
  std::mutex answers_mutex_;
  std::deque<std::array<uint8_t, kFrameSize>> answers_;  // guarded by answers_mutex_
};

}  // namespace skylanders::portal
