#include "portal_rex/remapper_check.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <tlhelp32.h>
#include <wchar.h>
#endif

namespace skylanders {

bool ControllerRemapperRunning() {
#ifdef _WIN32
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return false;
  }
  bool found = false;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL more = Process32FirstW(snapshot, &entry); more && !found;
       more = Process32NextW(snapshot, &entry)) {
    found = _wcsicmp(entry.szExeFile, L"DS4Windows.exe") == 0 ||
            _wcsicmp(entry.szExeFile, L"DSX.exe") == 0 ||
            _wcsicmp(entry.szExeFile, L"DualSenseX.exe") == 0;
  }
  CloseHandle(snapshot);
  return found;
#else
  return false;
#endif
}

}  // namespace skylanders
