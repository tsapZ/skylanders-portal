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
#elif defined(__linux__)
#include <libusb.h>
#endif

namespace skylanders::portal {

namespace {

constexpr size_t kMaxQueuedAnswers = 64;  // about a second of answers; older ones are dropped
constexpr unsigned int kPipeTimeoutMs = 200;  // lets the reader thread see a stop request
constexpr int kWriteTimeoutMs = 500;

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

struct ReaderStats {
  std::chrono::steady_clock::time_point opened = std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point last_summary = opened;
  int logged = 0;
  int other_logged = 0;
  uint32_t statuses = 0;
  uint32_t others = 0;
  uint32_t commands_before = 0;
  uint8_t last_counter = 0;

  void MaybeLogSummary(uint32_t commands) {
    const auto now = std::chrono::steady_clock::now();
    if (now - opened > std::chrono::seconds(65) || now - last_summary < std::chrono::seconds(5)) return;
    last_summary = now;
    REXLOG_INFO("Portal (Xbox 360): last 5 s: {} statuses (counter now {:02x}), {} other answers, {} commands sent",
                statuses, last_counter, others, commands - commands_before);
    statuses = others = 0;
    commands_before = commands;
  }
};

}  // namespace

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

#ifdef _WIN32

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
  // Overlapped, with an event of its own: a synchronous read held the WinUSB handle while it waited,
  // and the game's writes queued behind it (TrapTeamRecomp #19).
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ReaderStats stats;
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
        stats.MaybeLogSummary(commands_sent_.load());
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
      if (stats.logged < 12) {  // the first answers raw, as UsbPortal does
        ++stats.logged;
        REXLOG_INFO("Portal (Xbox 360): answer {} <- {}", stats.logged, Hex(frame.data(), 16));
      } else if (frame[2] == 'M') {  // the speaker switch's answer, always (TrapTeamRecomp #22)
        REXLOG_INFO("Portal (Xbox 360): speaker switch answered <- {}", Hex(frame.data(), 16));
      } else if (frame[2] != 'S' && stats.other_logged < 24) {  // a reply to a command, however late
        ++stats.other_logged;
        REXLOG_INFO("Portal (Xbox 360): answer (not a status) <- {}", Hex(frame.data(), 16));
      }
      if (frame[2] == 'S') {
        ++stats.statuses;
        stats.last_counter = frame[7];
      } else {
        ++stats.others;
      }
      std::lock_guard<std::mutex> lock(answers_mutex_);
      if (answers_.size() >= kMaxQueuedAnswers) answers_.pop_front();
      answers_.push_back(frame);
    }
    stats.MaybeLogSummary(commands_sent_.load());
  }
  CloseHandle(event);
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
    if (WaitForSingleObject(event, kWriteTimeoutMs) == WAIT_TIMEOUT) {
      WinUsb_AbortPipe(winusb, out_pipe_);  // the portal didn't take it: give up on this one
      WaitForSingleObject(event, kWriteTimeoutMs);
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

#elif defined(__linux__)

namespace {

constexpr uint16_t kXbox360PortalVendorId = 0x1430;
constexpr uint16_t kXbox360PortalProductId = 0x1F17;

}  // namespace

Xbox360Portal::Xbox360Portal() {
  libusb_context* ctx = nullptr;
  if (libusb_init(&ctx) != 0 || !ctx) return;

  libusb_device** list = nullptr;
  const ssize_t count = libusb_get_device_list(ctx, &list);
  libusb_device_handle* handle = nullptr;
  uint8_t in_ep = 0;
  uint8_t out_ep = 0;

  for (ssize_t i = 0; i < count; ++i) {
    libusb_device_descriptor desc{};
    if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
    if (desc.idVendor != kXbox360PortalVendorId || desc.idProduct != kXbox360PortalProductId) continue;

    const int open_rc = libusb_open(list[i], &handle);
    if (open_rc != 0 || !handle) {
      REXLOG_WARN("Portal (Xbox 360): found 1430:1f17 but can't open it ({}); install 70-portal-of-power.rules",
                  libusb_strerror(open_rc));
      handle = nullptr;
      continue;
    }

    libusb_set_auto_detach_kernel_driver(handle, 1);
    const int claim_rc = libusb_claim_interface(handle, 0);
    if (claim_rc != 0) {
      REXLOG_WARN("Portal (Xbox 360): can't claim interface 0 ({})", libusb_strerror(claim_rc));
      libusb_close(handle);
      handle = nullptr;
      continue;
    }

    libusb_config_descriptor* cfg = nullptr;
    if (libusb_get_active_config_descriptor(list[i], &cfg) == 0 && cfg) {
      if (cfg->bNumInterfaces > 0) {
        const libusb_interface& iface = cfg->interface[0];
        for (int alt = 0; alt < iface.num_altsetting; ++alt) {
          const libusb_interface_descriptor& alt_desc = iface.altsetting[alt];
          for (uint8_t e = 0; e < alt_desc.bNumEndpoints; ++e) {
            const libusb_endpoint_descriptor& ep = alt_desc.endpoint[e];
            const int type = ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
            REXLOG_INFO("Portal (Xbox 360): setting {} (class {:02x}/{:02x}/{:02x}) endpoint {:02x} type {} packet {} interval {}",
                        alt, alt_desc.bInterfaceClass, alt_desc.bInterfaceSubClass, alt_desc.bInterfaceProtocol,
                        ep.bEndpointAddress, type, ep.wMaxPacketSize, ep.bInterval);
            if (alt == 0 && type == LIBUSB_TRANSFER_TYPE_INTERRUPT) {
              if ((ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
                if (!in_ep) in_ep = ep.bEndpointAddress;
              } else if (!out_ep) {
                out_ep = ep.bEndpointAddress;
              }
            }
          }
        }
      }
      libusb_free_config_descriptor(cfg);
    }

    if (!in_ep || !out_ep) {
      REXLOG_WARN("Portal (Xbox 360): no interrupt IN/OUT pipe pair (in {:02x}, out {:02x})", in_ep, out_ep);
      libusb_release_interface(handle, 0);
      libusb_close(handle);
      handle = nullptr;
      in_ep = out_ep = 0;
      continue;
    }
    break;
  }

  if (list) libusb_free_device_list(list, 1);
  if (!handle) {
    libusb_exit(ctx);
    return;
  }

  // Clear any stalled endpoints left over from an earlier run.
  libusb_clear_halt(handle, in_ep);
  libusb_clear_halt(handle, out_ep);
  file_ = ctx;
  winusb_ = handle;
  in_pipe_ = in_ep;
  out_pipe_ = out_ep;
  REXLOG_INFO("Portal (Xbox 360): opened on libusb, endpoints in {:02x} out {:02x}", in_pipe_, out_pipe_);
  reader_ = std::thread([this] { ReaderLoop(); });
}

Xbox360Portal::~Xbox360Portal() {
  stopping_.store(true);
  if (reader_.joinable()) reader_.join();
  if (winusb_) {
    auto* handle = static_cast<libusb_device_handle*>(winusb_);
    libusb_release_interface(handle, 0);
    libusb_close(handle);
  }
  if (file_) libusb_exit(static_cast<libusb_context*>(file_));
}

void Xbox360Portal::ReaderLoop() {
  auto* handle = static_cast<libusb_device_handle*>(winusb_);
  ReaderStats stats;
  while (!stopping_.load()) {
    uint8_t buffer[kFrameSize] = {};
    int got = 0;
    const int rc = libusb_interrupt_transfer(handle, in_pipe_, buffer, sizeof(buffer), &got, kPipeTimeoutMs);
    if (rc == LIBUSB_ERROR_TIMEOUT || rc == LIBUSB_ERROR_INTERRUPTED) {
      stats.MaybeLogSummary(commands_sent_.load());
      continue;
    }
    if (rc != 0) {
      if (!stopping_.load()) {
        REXLOG_WARN("Portal (Xbox 360): read failed ({}); treating it as unplugged", libusb_strerror(rc));
        failed_.store(true);
      }
      break;
    }
    if (got <= 0) continue;
    if (frames_read_.load() == 0) {
      const int64_t first = first_command_ms_.load();
      REXLOG_INFO("Portal (Xbox 360): first answer{}", first ? " after the game's first command" : " (before any command)");
    }
    for (int offset = 0; offset < got; offset += int(kFrameSize)) {
      frames_read_.fetch_add(1);
      std::array<uint8_t, kFrameSize> frame{};
      const size_t n = std::min<size_t>(size_t(got - offset), frame.size());
      std::memcpy(frame.data(), buffer + offset, n);
      if (stats.logged < 12) {
        ++stats.logged;
        REXLOG_INFO("Portal (Xbox 360): answer {} <- {}", stats.logged, Hex(frame.data(), 16));
      } else if (frame[2] == 'M') {
        REXLOG_INFO("Portal (Xbox 360): speaker switch answered <- {}", Hex(frame.data(), 16));
      } else if (frame[2] != 'S' && stats.other_logged < 24) {
        ++stats.other_logged;
        REXLOG_INFO("Portal (Xbox 360): answer (not a status) <- {}", Hex(frame.data(), 16));
      }
      if (frame[2] == 'S') {
        ++stats.statuses;
        stats.last_counter = frame[7];
      } else {
        ++stats.others;
      }
      std::lock_guard<std::mutex> lock(answers_mutex_);
      if (answers_.size() >= kMaxQueuedAnswers) answers_.pop_front();
      answers_.push_back(frame);
    }
    stats.MaybeLogSummary(commands_sent_.load());
  }
}

bool Xbox360Portal::Write(std::span<const uint8_t> buffer) {
  if (failed_.load() || !winusb_) return false;
  if (const int n = logged_writes_.fetch_add(1); n < 24) {
    REXLOG_INFO("Portal (Xbox 360): command {} -> {}", n + 1,
                Hex(buffer.data(), std::min<size_t>(buffer.size(), 16)));
  }
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
  auto* handle = static_cast<libusb_device_handle*>(winusb_);
  int written = 0;
  const int rc = libusb_interrupt_transfer(handle, out_pipe_, const_cast<uint8_t*>(buffer.data()),
                                           static_cast<int>(buffer.size()), &written, kWriteTimeoutMs);
  const bool ok = (rc == 0);
  const bool timed_out = (rc == LIBUSB_ERROR_TIMEOUT);
  if (ok) commands_sent_.fetch_add(1);
  if (logged_results_.fetch_add(1) < 24) {
    REXLOG_INFO("Portal (Xbox 360): command sent: {} of {} bytes taken{}", written, buffer.size(),
                ok ? "" : (timed_out ? ", timed out" : ", failed"));
  }
  if (ok) return true;
  if (timed_out) {
    const uint32_t n = write_timeouts_.fetch_add(1) + 1;
    if (n == 10 || n == 100 || n == 1000) {
      REXLOG_WARN("Portal (Xbox 360): {} commands timed out so far (the portal isn't taking them)", n);
    }
    return true;
  }
  REXLOG_WARN("Portal (Xbox 360): write failed ({}); treating it as unplugged", libusb_strerror(rc));
  failed_.store(true);
  return false;
}

#else

Xbox360Portal::Xbox360Portal() = default;
Xbox360Portal::~Xbox360Portal() = default;
void Xbox360Portal::ReaderLoop() {}
bool Xbox360Portal::Write(std::span<const uint8_t>) { return false; }

#endif

}  // namespace skylanders::portal
