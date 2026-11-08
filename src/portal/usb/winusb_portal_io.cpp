#include "portal/usb/winusb_portal_io.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <rex/logging.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <setupapi.h>
#include <winusb.h>
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "winusb.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace skylanders::portal {

#ifdef _WIN32

namespace {
constexpr size_t kMaxQueuedAnswers = 64;  // a second or two of answers; older ones are dropped
constexpr ULONG kPipeTimeoutMs = 200;     // lets the reader thread see a stop request
constexpr ULONG kControlTimeoutMs = 500;  // a command the portal doesn't take in time is dropped
}  // namespace

std::wstring FindWinUsbDevicePath(const wchar_t* hardware_id) {
  HDEVINFO devices = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
  if (devices == INVALID_HANDLE_VALUE) return {};
  std::wstring path;
  SP_DEVINFO_DATA info{};
  info.cbSize = sizeof(info);
  for (DWORD i = 0; path.empty() && SetupDiEnumDeviceInfo(devices, i, &info); ++i) {
    wchar_t ids[512] = {};
    wchar_t service[64] = {};
    if (!SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_HARDWAREID, nullptr, reinterpret_cast<PBYTE>(ids),
                                           sizeof(ids) - 2 * sizeof(wchar_t), nullptr) ||
        wcsstr(ids, hardware_id) == nullptr) {
      continue;
    }
    SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_SERVICE, nullptr, reinterpret_cast<PBYTE>(service),
                                      sizeof(service) - sizeof(wchar_t), nullptr);
    if (_wcsicmp(service, L"WinUSB") != 0) continue;
    HKEY key = SetupDiOpenDevRegKey(devices, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE) continue;
    wchar_t guid_text[512] = {};
    DWORD size = sizeof(guid_text) - 2 * sizeof(wchar_t);
    DWORD type = 0;
    LSTATUS status = RegQueryValueExW(key, L"DeviceInterfaceGUIDs", nullptr, &type,
                                      reinterpret_cast<LPBYTE>(guid_text), &size);
    if (status != ERROR_SUCCESS) {
      size = sizeof(guid_text) - 2 * sizeof(wchar_t);
      status = RegQueryValueExW(key, L"DeviceInterfaceGUID", nullptr, &type, reinterpret_cast<LPBYTE>(guid_text),
                                &size);
    }
    RegCloseKey(key);
    GUID guid{};
    if (status != ERROR_SUCCESS || FAILED(CLSIDFromString(guid_text, &guid))) continue;  // the first GUID
    HDEVINFO interfaces = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (interfaces == INVALID_HANDLE_VALUE) continue;
    SP_DEVICE_INTERFACE_DATA interface_data{};
    interface_data.cbSize = sizeof(interface_data);
    if (SetupDiEnumDeviceInterfaces(interfaces, nullptr, &guid, 0, &interface_data)) {
      DWORD needed = 0;
      SetupDiGetDeviceInterfaceDetailW(interfaces, &interface_data, nullptr, 0, &needed, nullptr);
      std::vector<uint8_t> detail_buffer(needed + sizeof(wchar_t));
      auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detail_buffer.data());
      detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
      if (needed && SetupDiGetDeviceInterfaceDetailW(interfaces, &interface_data, detail, needed, nullptr, nullptr)) {
        path = detail->DevicePath;
      }
    }
    SetupDiDestroyDeviceInfoList(interfaces);
  }
  SetupDiDestroyDeviceInfoList(devices);
  return path;
}

WinUsbPortalIo::WinUsbPortalIo(uint16_t product_id) {
  wchar_t hardware_id[32];
  swprintf(hardware_id, 32, L"VID_1430&PID_%04X", product_id);
  const std::wstring path = FindWinUsbDevicePath(hardware_id);
  if (path.empty()) return;
  // The game tries again every second while no portal opens: each failure goes to the log once.
  static std::atomic<bool> open_warned{false};
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    if (!open_warned.exchange(true)) {
      REXLOG_WARN("Portal (usb): 1430:{:04x} found on WinUSB but can't open it (error {}; another program using it?)",
                  product_id, error);
    }
    return;
  }
  WINUSB_INTERFACE_HANDLE winusb = nullptr;
  if (!WinUsb_Initialize(file, &winusb)) {
    const DWORD error = GetLastError();
    if (!open_warned.exchange(true)) {
      REXLOG_WARN("Portal (usb): 1430:{:04x} WinUsb_Initialize failed (error {})", product_id, error);
    }
    CloseHandle(file);
    return;
  }
  USB_INTERFACE_DESCRIPTOR descriptor{};
  if (WinUsb_QueryInterfaceSettings(winusb, 0, &descriptor)) {
    interface_number_ = descriptor.bInterfaceNumber;
    for (UCHAR i = 0; i < descriptor.bNumEndpoints; ++i) {
      WINUSB_PIPE_INFORMATION pipe{};
      if (!WinUsb_QueryPipe(winusb, 0, i, &pipe) || pipe.PipeType != UsbdPipeTypeInterrupt) continue;
      if (USB_ENDPOINT_DIRECTION_IN(pipe.PipeId)) {
        if (!in_pipe_) {
          in_pipe_ = pipe.PipeId;
          in_packet_size_ = pipe.MaximumPacketSize;
        }
      } else if (!out_pipe_) {
        out_pipe_ = pipe.PipeId;
      }
    }
  }
  if (!in_pipe_) {
    REXLOG_WARN("Portal (usb): 1430:{:04x} on WinUSB has no interrupt IN pipe", product_id);
    WinUsb_Free(winusb);
    CloseHandle(file);
    return;
  }
  ULONG timeout = kPipeTimeoutMs;
  WinUsb_SetPipePolicy(winusb, in_pipe_, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout);
  ULONG control_timeout = kControlTimeoutMs;
  WinUsb_SetPipePolicy(winusb, 0, PIPE_TRANSFER_TIMEOUT, sizeof(control_timeout), &control_timeout);
  if (out_pipe_) WinUsb_SetPipePolicy(winusb, out_pipe_, PIPE_TRANSFER_TIMEOUT, sizeof(control_timeout), &control_timeout);
  // A pipe left stalled or with old data by an earlier run (or an emulator) starts clean.
  WinUsb_ResetPipe(winusb, in_pipe_);
  WinUsb_FlushPipe(winusb, in_pipe_);
  file_ = file;
  winusb_ = winusb;
  write_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  REXLOG_INFO("Portal (usb): opened 1430:{:04x} on WinUSB (interface {}, in {:02x} size {}, out {:02x})", product_id,
              interface_number_, in_pipe_, in_packet_size_, out_pipe_);
  reader_ = std::thread([this] { ReaderLoop(); });
}

WinUsbPortalIo::~WinUsbPortalIo() {
  stopping_.store(true);
  if (winusb_) WinUsb_AbortPipe(static_cast<WINUSB_INTERFACE_HANDLE>(winusb_), in_pipe_);
  if (reader_.joinable()) reader_.join();
  if (winusb_) WinUsb_Free(static_cast<WINUSB_INTERFACE_HANDLE>(winusb_));
  if (write_event_) CloseHandle(static_cast<HANDLE>(write_event_));
  if (file_) CloseHandle(static_cast<HANDLE>(file_));
}

void WinUsbPortalIo::ReaderLoop() {
  auto* winusb = static_cast<WINUSB_INTERFACE_HANDLE>(winusb_);
  // Overlapped, with an event of its own, so a waiting read never holds up the commands (the
  // Xbox 360 portal's lesson, TrapTeamRecomp #19).
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  std::vector<uint8_t> buffer(std::max<size_t>(in_packet_size_, kReportSize));
  while (!stopping_.load()) {
    ULONG got = 0;
    OVERLAPPED overlapped{};
    overlapped.hEvent = event;
    ResetEvent(event);
    BOOL ok = WinUsb_ReadPipe(winusb, in_pipe_, buffer.data(), static_cast<ULONG>(buffer.size()), nullptr, &overlapped);
    DWORD error = ok ? 0 : GetLastError();
    if (ok || error == ERROR_IO_PENDING) {
      while (WaitForSingleObject(event, kPipeTimeoutMs) == WAIT_TIMEOUT) {
        if (stopping_.load()) WinUsb_AbortPipe(winusb, in_pipe_);
      }
      ok = WinUsb_GetOverlappedResult(winusb, &overlapped, &got, FALSE);
      error = ok ? 0 : GetLastError();
    }
    if (!ok) {
      if (error == ERROR_SEM_TIMEOUT || error == ERROR_OPERATION_ABORTED) continue;  // idle, or stopping
      if (!stopping_.load()) {
        REXLOG_WARN("Portal (usb): WinUSB read failed (error {}); treating the portal as unplugged", error);
        failed_.store(true);
      }
      break;
    }
    // The portal's reports are 32 bytes; a read of a bigger packet size is split into them.
    for (size_t offset = 0; offset < got; offset += kReportSize) {
      std::array<uint8_t, kReportSize> report{};
      std::memcpy(report.data(), buffer.data() + offset, std::min<size_t>(got - offset, kReportSize));
      std::lock_guard<std::mutex> lock(answers_mutex_);
      if (answers_.size() >= kMaxQueuedAnswers) answers_.pop_front();
      answers_.push_back(report);
    }
  }
  CloseHandle(event);
}

bool WinUsbPortalIo::SetReport(const uint8_t* data, size_t size) {
  if (!winusb_ || failed_.load()) return false;
  // HID SET_REPORT (class request to the interface): output report, id 0.
  WINUSB_SETUP_PACKET setup{};
  setup.RequestType = 0x21;
  setup.Request = 0x09;
  setup.Value = 0x0200;
  setup.Index = interface_number_;
  setup.Length = static_cast<USHORT>(std::min(size, kReportSize));
  uint8_t packet[kReportSize] = {};
  std::memcpy(packet, data, setup.Length);
  ULONG sent = 0;
  return WinUsb_ControlTransfer(static_cast<WINUSB_INTERFACE_HANDLE>(winusb_), setup, packet, setup.Length, &sent,
                                nullptr) != FALSE;
}

int WinUsbPortalIo::Read(uint8_t* out, size_t size) {
  std::lock_guard<std::mutex> lock(answers_mutex_);
  if (answers_.empty()) return failed_.load() ? -1 : 0;
  std::memcpy(out, answers_.front().data(), std::min(size, kReportSize));
  answers_.pop_front();
  return 1;
}

bool WinUsbPortalIo::WriteInterrupt(const uint8_t* data, size_t size) {
  if (!winusb_ || !out_pipe_ || failed_.load()) return false;
  std::lock_guard<std::mutex> lock(write_mutex_);
  auto* winusb = static_cast<WINUSB_INTERFACE_HANDLE>(winusb_);
  auto* event = static_cast<HANDLE>(write_event_);
  OVERLAPPED overlapped{};
  overlapped.hEvent = event;
  ResetEvent(event);
  ULONG written = 0;
  BOOL ok = WinUsb_WritePipe(winusb, out_pipe_, const_cast<PUCHAR>(data), static_cast<ULONG>(size), nullptr,
                             &overlapped);
  if (!ok && GetLastError() == ERROR_IO_PENDING) {
    if (WaitForSingleObject(event, kControlTimeoutMs) == WAIT_TIMEOUT) {
      WinUsb_AbortPipe(winusb, out_pipe_);
      WaitForSingleObject(event, kControlTimeoutMs);
    }
    ok = WinUsb_GetOverlappedResult(winusb, &overlapped, &written, FALSE);
  }
  return ok != FALSE;
}

#else  // not Windows: no WinUSB; the class never opens.

std::wstring FindWinUsbDevicePath(const wchar_t*) { return {}; }
WinUsbPortalIo::WinUsbPortalIo(uint16_t) {}
WinUsbPortalIo::~WinUsbPortalIo() = default;
void WinUsbPortalIo::ReaderLoop() {}
bool WinUsbPortalIo::SetReport(const uint8_t*, size_t) { return false; }
int WinUsbPortalIo::Read(uint8_t*, size_t) { return -1; }
bool WinUsbPortalIo::WriteInterrupt(const uint8_t*, size_t) { return false; }

#endif

}  // namespace skylanders::portal
