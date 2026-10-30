// The first line of a game's log: which build it is and what it runs on, for problem reports
// (2026-10-30: a log didn't say its version). Only what helps with a report: no names, no paths.

#include "xex/xex_verify.h"

#include <cstring>
#include <string>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

namespace skylanders::xex {
namespace {

std::string CpuName() {
#if defined(_WIN32) || defined(__x86_64__) || defined(__i386__)
  int regs[4] = {};
  char brand[49] = {};
  for (int i = 0; i < 3; ++i) {
#if defined(_WIN32)
    __cpuid(regs, static_cast<int>(0x80000002u + i));
#else
    __cpuid(0x80000002u + i, regs[0], regs[1], regs[2], regs[3]);
#endif
    std::memcpy(brand + 16 * i, regs, 16);
  }
  std::string name(brand);
  const auto first = name.find_first_not_of(' ');
  const auto last = name.find_last_not_of(' ');
  return first == std::string::npos ? std::string("unknown CPU") : name.substr(first, last - first + 1);
#else
  return "unknown CPU";
#endif
}

std::string OsName() {
#if defined(_WIN32)
  // RtlGetVersion: GetVersionEx answers 8 without a manifest that names newer Windows.
  using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
  OSVERSIONINFOW info = {sizeof(info)};
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto get = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
  if (!get || get(&info) != 0) return "Windows";
  const unsigned major = info.dwMajorVersion == 10 && info.dwBuildNumber >= 22000 ? 11 : info.dwMajorVersion;
  return "Windows " + std::to_string(major) + " build " + std::to_string(info.dwBuildNumber);
#elif defined(__APPLE__)
  return "macOS";
#else
  return "Linux";
#endif
}

std::string MemoryText() {
#if defined(_WIN32)
  MEMORYSTATUSEX status = {sizeof(status)};
  if (GlobalMemoryStatusEx(&status)) {
    return std::to_string((status.ullTotalPhys + (512ull << 20)) >> 30) + " GB RAM";
  }
#endif
  return "RAM unknown";
}

}  // namespace

std::string BuildAndSystemLine(std::string_view title) {
  const unsigned threads = std::thread::hardware_concurrency();
  return std::string(title) + " " + GameVersion() + " (build " + GameCommit() + "); " + OsName() + "; " +
         CpuName() + ", " + std::to_string(threads) + " threads; " + MemoryText();
}

}  // namespace skylanders::xex
