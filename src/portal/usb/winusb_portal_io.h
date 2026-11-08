#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace skylanders::portal {

// The device interface path of the first present USB device whose hardware id contains
// `hardware_id` (L"VID_1430&PID_0150") and that has the WinUSB driver, or empty. Zadig (like any
// WinUSB install) records the interface GUID in the device's registry key as DeviceInterfaceGUIDs
// (or DeviceInterfaceGUID); the path comes from that interface. Windows only (empty elsewhere).
std::wstring FindWinUsbDevicePath(const wchar_t* hardware_id);

// A PS3 / Wii / Wii U Portal of Power (1430:0150) that was given the WinUSB driver, mostly with
// Zadig for Dolphin's or Cemu's USB passthrough (Core of Light #58: Device Manager shows it as
// "Spyro Porta" then, not as a USB input device). It is no HID device any more, so hidapi can't
// open it; UsbPortal talks to it through this instead, with the same 32-byte reports: commands as
// HID SET_REPORT control transfers (what hid_send_output_report sends), answers from the interrupt
// IN pipe (a reader thread queues them), speaker audio on the interrupt OUT pipe.
class WinUsbPortalIo {
 public:
  static constexpr size_t kReportSize = 32;

  // Opens the first present 1430:`product_id` on WinUSB. Check IsOpen(); never throws.
  explicit WinUsbPortalIo(uint16_t product_id);
  ~WinUsbPortalIo();

  WinUsbPortalIo(const WinUsbPortalIo&) = delete;
  WinUsbPortalIo& operator=(const WinUsbPortalIo&) = delete;

  bool IsOpen() const { return winusb_ != nullptr; }
  // A command (kReportSize bytes) as a SET_REPORT of output report 0; false if it wasn't taken.
  bool SetReport(const uint8_t* data, size_t size);
  // The oldest queued answer into `out` (kReportSize bytes): 1 if there was one, 0 if none, -1 once
  // reading failed (unplugged).
  int Read(uint8_t* out, size_t size);
  // Speaker audio on the interrupt OUT pipe; false if there is none or it wasn't taken.
  bool WriteInterrupt(const uint8_t* data, size_t size);

 private:
  void ReaderLoop();

  void* file_ = nullptr;    // HANDLE
  void* winusb_ = nullptr;  // WINUSB_INTERFACE_HANDLE
  void* write_event_ = nullptr;
  uint8_t interface_number_ = 0;
  uint8_t in_pipe_ = 0;
  uint8_t out_pipe_ = 0;
  uint16_t in_packet_size_ = kReportSize;
  std::thread reader_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> failed_{false};
  std::mutex answers_mutex_;
  std::deque<std::array<uint8_t, kReportSize>> answers_;
  std::mutex write_mutex_;  // one interrupt OUT write at a time (write_event_)
};

}  // namespace skylanders::portal
