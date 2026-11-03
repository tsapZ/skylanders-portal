#include "portal/usb/xbox360_portal.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <rex/logging.h>
#include <rex/string/utf8.h>

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

constexpr size_t kMaxQueuedAnswers = 64;  // about a second of answers; older ones are dropped

std::string Hex(const uint8_t* data, size_t n) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string hex;
  for (size_t i = 0; i < n; ++i) {
    if (i) hex += ' ';
    hex += kHex[data[i] >> 4];
    hex += kHex[data[i] & 0xF];
  }
  return hex;
}
constexpr ULONG kPipeTimeoutMs = 200;     // lets the reader thread see a stop request

std::string Utf8(const wchar_t* text) {
  return rex::string::to_utf8(std::u16string_view(reinterpret_cast<const char16_t*>(text)));
}

// The device interface path of the first present 1430:1F17 that has the WinUSB driver, or empty.
// Zadig (like any WinUSB install) records the interface GUID in the device's registry key as
// DeviceInterfaceGUIDs (or DeviceInterfaceGUID); the path comes from that interface.
std::wstring FindWinUsbPortalPath() {
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
        wcsstr(ids, L"VID_1430&PID_1F17") == nullptr) {
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

}  // namespace

Xbox360Portal::Xbox360Portal() {
  const std::wstring path = FindWinUsbPortalPath();
  if (path.empty()) return;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    REXLOG_WARN("Portal (Xbox 360): found on WinUSB but can't open it (error {})", GetLastError());
    return;
  }
  WINUSB_INTERFACE_HANDLE winusb = nullptr;
  if (!WinUsb_Initialize(file, &winusb)) {
    REXLOG_WARN("Portal (Xbox 360): WinUsb_Initialize failed (error {})", GetLastError());
    CloseHandle(file);
    return;
  }
  USB_INTERFACE_DESCRIPTOR descriptor{};
  if (WinUsb_QueryInterfaceSettings(winusb, 0, &descriptor)) {
    for (UCHAR i = 0; i < descriptor.bNumEndpoints; ++i) {
      WINUSB_PIPE_INFORMATION pipe{};
      if (!WinUsb_QueryPipe(winusb, 0, i, &pipe) || pipe.PipeType != UsbdPipeTypeInterrupt) continue;
      if (USB_ENDPOINT_DIRECTION_IN(pipe.PipeId)) {
        if (!in_pipe_) in_pipe_ = pipe.PipeId;
      } else if (!out_pipe_) {
        out_pipe_ = pipe.PipeId;
      }
    }
  }
  if (!in_pipe_ || !out_pipe_) {
    REXLOG_WARN("Portal (Xbox 360): no interrupt IN/OUT pipe pair (in {:02x}, out {:02x})", in_pipe_, out_pipe_);
    WinUsb_Free(winusb);
    CloseHandle(file);
    return;
  }
  ULONG timeout = kPipeTimeoutMs;
  WinUsb_SetPipePolicy(winusb, in_pipe_, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout);
  WinUsb_SetPipePolicy(winusb, out_pipe_, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout);
  file_ = file;
  winusb_ = winusb;
  REXLOG_INFO("Portal (Xbox 360): opened on WinUSB, pipes in {:02x} out {:02x}", in_pipe_, out_pipe_);
  reader_ = std::thread([this] { ReaderLoop(); });
}

Xbox360Portal::~Xbox360Portal() {
  stopping_.store(true);
  if (winusb_) WinUsb_AbortPipe(static_cast<WINUSB_INTERFACE_HANDLE>(winusb_), in_pipe_);
  if (reader_.joinable()) reader_.join();
  if (winusb_) WinUsb_Free(static_cast<WINUSB_INTERFACE_HANDLE>(winusb_));
  if (file_) CloseHandle(static_cast<HANDLE>(file_));
}

void Xbox360Portal::ReaderLoop() {
  auto* winusb = static_cast<WINUSB_INTERFACE_HANDLE>(winusb_);
  int logged = 0;
  while (!stopping_.load()) {
    uint8_t buffer[64] = {};
    ULONG got = 0;
    if (!WinUsb_ReadPipe(winusb, in_pipe_, buffer, sizeof(buffer), &got, nullptr)) {
      const DWORD error = GetLastError();
      if (error == ERROR_SEM_TIMEOUT) continue;  // nothing within the timeout: normal while idle
      if (!stopping_.load()) {
        REXLOG_WARN("Portal (Xbox 360): read failed (error {}); treating it as unplugged", error);
        failed_.store(true);
      }
      return;
    }
    if (got == 0) continue;
    // The portal sends full 32-byte packets, so a 64-byte read ends only after the second one: each
    // read can hold two frames (the answers' counters went up by two, and the replies to the game's
    // commands were among the dropped halves: "incompatible portal", TrapTeamRecomp #19). Each
    // frame goes to the game on its own, as Xenia's 32-byte reads hand them over.
    for (size_t offset = 0; offset < got; offset += kFrameSize) {
      std::array<uint8_t, kFrameSize> frame{};
      const size_t n = std::min<size_t>(got - offset, frame.size());
      std::memcpy(frame.data(), buffer + offset, n);
      if (logged < 12) {  // the first answers raw, as UsbPortal does
        ++logged;
        REXLOG_INFO("Portal (Xbox 360): answer {} <- {}", logged, Hex(frame.data(), 16));
      }
      std::lock_guard<std::mutex> lock(answers_mutex_);
      if (answers_.size() >= kMaxQueuedAnswers) answers_.pop_front();
      answers_.push_back(frame);
    }
  }
}

bool Xbox360Portal::Read(std::span<uint8_t> buffer, uint32_t& bytes_read) {
  bytes_read = 0;
  if (failed_.load()) return false;
  std::lock_guard<std::mutex> lock(answers_mutex_);
  if (answers_.empty()) return true;
  const auto& frame = answers_.front();
  const size_t n = std::min(buffer.size(), frame.size());
  std::memcpy(buffer.data(), frame.data(), n);
  answers_.pop_front();
  bytes_read = static_cast<uint32_t>(n);
  return true;
}

bool Xbox360Portal::Write(std::span<const uint8_t> buffer) {
  if (failed_.load() || !winusb_) return false;
  // The game's first commands raw, next to the answers they get.
  if (const int n = logged_writes_.fetch_add(1); n < 12) {
    REXLOG_INFO("Portal (Xbox 360): command {} -> {}", n + 1,
                Hex(buffer.data(), std::min<size_t>(buffer.size(), 16)));
  }
  ULONG written = 0;
  if (WinUsb_WritePipe(static_cast<WINUSB_INTERFACE_HANDLE>(winusb_), out_pipe_, const_cast<PUCHAR>(buffer.data()),
                       static_cast<ULONG>(buffer.size()), &written, nullptr)) {
    return true;
  }
  const DWORD error = GetLastError();
  if (error == ERROR_SEM_TIMEOUT) return true;  // the portal didn't take it in time; the game resends
  REXLOG_WARN("Portal (Xbox 360): write failed (error {}); treating it as unplugged", error);
  failed_.store(true);
  return false;
}

#else  // not Windows: no WinUSB; the class never opens.

Xbox360Portal::Xbox360Portal() = default;
Xbox360Portal::~Xbox360Portal() = default;
void Xbox360Portal::ReaderLoop() {}
bool Xbox360Portal::Read(std::span<uint8_t>, uint32_t& bytes_read) {
  bytes_read = 0;
  return false;
}
bool Xbox360Portal::Write(std::span<const uint8_t>) { return false; }

#endif

}  // namespace skylanders::portal
