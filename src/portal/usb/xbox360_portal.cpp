#include "portal/usb/xbox360_portal.h"

#include "portal/usb/winusb_portal_io.h"

#include <algorithm>
#include <chrono>
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

}  // namespace

Xbox360Portal::Xbox360Portal() {
  const std::wstring path = FindWinUsbDevicePath(L"VID_1430&PID_1F17");
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
  // Every endpoint of every alternate setting, for the logs (TrapTeamRecomp #19: is there a second pair?).
  for (UCHAR alt = 0; alt < 8; ++alt) {
    USB_INTERFACE_DESCRIPTOR any{};
    if (!WinUsb_QueryInterfaceSettings(winusb, alt, &any)) break;
    for (UCHAR i = 0; i < any.bNumEndpoints; ++i) {
      WINUSB_PIPE_INFORMATION pipe{};
      if (!WinUsb_QueryPipe(winusb, alt, i, &pipe)) continue;
      REXLOG_INFO("Portal (Xbox 360): setting {} (class {:02x}/{:02x}/{:02x}) endpoint {:02x} type {} packet {} interval {}",
                  alt, any.bInterfaceClass, any.bInterfaceSubClass, any.bInterfaceProtocol, pipe.PipeId,
                  int(pipe.PipeType), pipe.MaximumPacketSize, pipe.Interval);
    }
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
  // A pipe left stalled or with old data by an earlier run starts clean.
  WinUsb_ResetPipe(winusb, in_pipe_);
  WinUsb_ResetPipe(winusb, out_pipe_);
  WinUsb_FlushPipe(winusb, in_pipe_);
  file_ = file;
  winusb_ = winusb;
  write_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  REXLOG_INFO("Portal (Xbox 360): opened on WinUSB, pipes in {:02x} out {:02x}", in_pipe_, out_pipe_);
  reader_ = std::thread([this] { ReaderLoop(); });
}

Xbox360Portal::~Xbox360Portal() {
  stopping_.store(true);
  if (winusb_) WinUsb_AbortPipe(static_cast<WINUSB_INTERFACE_HANDLE>(winusb_), in_pipe_);
  if (reader_.joinable()) reader_.join();
  if (winusb_) WinUsb_Free(static_cast<WINUSB_INTERFACE_HANDLE>(winusb_));
  if (write_event_) CloseHandle(static_cast<HANDLE>(write_event_));
  if (file_) CloseHandle(static_cast<HANDLE>(file_));
}

void Xbox360Portal::ReaderLoop() {
  auto* winusb = static_cast<WINUSB_INTERFACE_HANDLE>(winusb_);
  int logged = 0;
  int other_logged = 0;
  // Overlapped, with an event of its own: a synchronous read held the WinUSB handle while it waited,
  // and the game's writes queued behind it (TrapTeamRecomp #19).
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  // For the first minute, every 5 s: how many statuses and other answers came and how many
  // commands went out since the last line, so a report shows whether the portal goes quiet after
  // the first exchange or only stops answering (TrapTeamRecomp #19, 0.4.1-beta.1: one 'R' answered
  // right after a replug, then no answer to anything).
  const auto opened = std::chrono::steady_clock::now();
  auto last_summary = opened;
  uint32_t statuses = 0, others = 0, commands_before = 0;
  uint8_t last_counter = 0;
  auto summary = [&] {
    const auto now = std::chrono::steady_clock::now();
    if (now - opened > std::chrono::seconds(65) || now - last_summary < std::chrono::seconds(5)) return;
    last_summary = now;
    const uint32_t commands = commands_sent_.load();
    REXLOG_INFO("Portal (Xbox 360): last 5 s: {} statuses (counter now {:02x}), {} other answers, {} commands sent",
                statuses, last_counter, others, commands - commands_before);
    statuses = others = 0;
    commands_before = commands;
  };
  while (!stopping_.load()) {
    // One packet per read, as Xenia reads it: the portal's packets are 32 bytes.
    uint8_t buffer[kFrameSize] = {};
    ULONG got = 0;
    OVERLAPPED overlapped{};
    overlapped.hEvent = event;
    ResetEvent(event);
    BOOL ok = WinUsb_ReadPipe(winusb, in_pipe_, buffer, sizeof(buffer), nullptr, &overlapped);
    DWORD error = ok ? 0 : GetLastError();
    if (ok || error == ERROR_IO_PENDING) {
      while (WaitForSingleObject(event, kPipeTimeoutMs) == WAIT_TIMEOUT) {
        if (stopping_.load()) WinUsb_AbortPipe(winusb, in_pipe_);
        summary();
      }
      ok = WinUsb_GetOverlappedResult(winusb, &overlapped, &got, FALSE);
      error = ok ? 0 : GetLastError();
    }
    if (!ok) {
      if (error == ERROR_SEM_TIMEOUT || error == ERROR_OPERATION_ABORTED) continue;  // idle, or stopping
      if (!stopping_.load()) {
        REXLOG_WARN("Portal (Xbox 360): read failed (error {}); treating it as unplugged", error);
        failed_.store(true);
      }
      break;
    }
    if (got == 0) continue;
    // The portal sends full 32-byte packets (a 64-byte read held two, and the replies to the game's
    // commands were among the dropped halves once: "incompatible portal", TrapTeamRecomp #19).
    // Each frame goes to the game on its own, as Xenia's 32-byte reads hand them over.
    if (frames_read_.load() == 0) {
      const int64_t first = first_command_ms_.load();
      REXLOG_INFO("Portal (Xbox 360): first answer{}", first ? " after the game's first command" : " (before any command)");
    }
    for (size_t offset = 0; offset < got; offset += kFrameSize) {
      frames_read_.fetch_add(1);
      std::array<uint8_t, kFrameSize> frame{};
      const size_t n = std::min<size_t>(got - offset, frame.size());
      std::memcpy(frame.data(), buffer + offset, n);
      if (logged < 12) {  // the first answers raw, as UsbPortal does
        ++logged;
        REXLOG_INFO("Portal (Xbox 360): answer {} <- {}", logged, Hex(frame.data(), 16));
      } else if (frame[2] != 'S' && other_logged < 24) {  // a reply to a command, however late
        ++other_logged;
        REXLOG_INFO("Portal (Xbox 360): answer (not a status) <- {}", Hex(frame.data(), 16));
      }
      if (frame[2] == 'S') {
        ++statuses;
        last_counter = frame[7];
      } else {
        ++others;
      }
      std::lock_guard<std::mutex> lock(answers_mutex_);
      if (answers_.size() >= kMaxQueuedAnswers) answers_.pop_front();
      answers_.push_back(frame);
    }
    summary();
  }
  CloseHandle(event);
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
  if (failed_.load() || !winusb_ || !write_event_) return false;
  // The game's first commands raw, next to the answers they get.
  if (const int n = logged_writes_.fetch_add(1); n < 24) {
    REXLOG_INFO("Portal (Xbox 360): command {} -> {}", n + 1,
                Hex(buffer.data(), std::min<size_t>(buffer.size(), 16)));
  }
  // Silent portal: once, 3 s after the game's first command without any answer.
  const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
  int64_t expected = 0;
  first_command_ms_.compare_exchange_strong(expected, now_ms);
  if (frames_read_.load() == 0 && now_ms - first_command_ms_.load() > 3000 && !warned_silent_.exchange(true)) {
    REXLOG_WARN("Portal (Xbox 360): no answer 3 s after the game's first command ({} commands so far); "
                "unplugging the portal and plugging it back in may help",
                logged_writes_.load());
  }
  // The game's bytes as they are (4 for 'R'), as Xenia sends them. 0.4.0-beta.7 to 0.4.1-beta.1
  // padded them with zeros to 32 bytes: the frame's second byte (0x14) reads like a length, as in
  // the Xbox 360 controllers' messages on the same kind of interface, and after a replug the portal
  // answered the first padded command and nothing after it (TrapTeamRecomp #19). The earlier
  // unpadded writes went out behind a blocking read, so they never proved anything.
  std::span<const uint8_t> packet = buffer;
  auto* winusb = static_cast<WINUSB_INTERFACE_HANDLE>(winusb_);
  auto* event = static_cast<HANDLE>(write_event_);
  ULONG written = 0;
  OVERLAPPED overlapped{};
  overlapped.hEvent = event;
  ResetEvent(event);
  BOOL ok = WinUsb_WritePipe(winusb, out_pipe_, const_cast<PUCHAR>(packet.data()), static_cast<ULONG>(packet.size()),
                             nullptr, &overlapped);
  DWORD error = ok ? 0 : GetLastError();
  if (ok || error == ERROR_IO_PENDING) {
    if (WaitForSingleObject(event, 500) == WAIT_TIMEOUT) {
      WinUsb_AbortPipe(winusb, out_pipe_);  // the portal didn't take it: give up on this one
      WaitForSingleObject(event, 500);
    }
    ok = WinUsb_GetOverlappedResult(winusb, &overlapped, &written, FALSE);
    error = ok ? 0 : GetLastError();
  }
  // What became of the first commands: taken (how many bytes) or not (TrapTeamRecomp #19).
  if (ok) commands_sent_.fetch_add(1);
  if (logged_results_.fetch_add(1) < 24) {
    REXLOG_INFO("Portal (Xbox 360): command sent: {} of {} bytes taken{}", written, packet.size(),
                ok ? "" : (error == ERROR_SEM_TIMEOUT || error == ERROR_OPERATION_ABORTED ? ", timed out" : ", failed"));
  }
  if (ok) return true;
  if (error == ERROR_SEM_TIMEOUT || error == ERROR_OPERATION_ABORTED) {  // not taken in time; the game resends
    const uint32_t n = write_timeouts_.fetch_add(1) + 1;
    if (n == 10 || n == 100 || n == 1000) {
      REXLOG_WARN("Portal (Xbox 360): {} commands timed out so far (the portal isn't taking them)", n);
    }
    return true;
  }
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
