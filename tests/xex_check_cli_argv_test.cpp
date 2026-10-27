// Runs the game's built <game>_xexcheck (SKYLANDERS_XEXCHECK_NAME, from skylanders_game_xex())
// against a non-ASCII path, to catch argv narrowing.
#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32

namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

static fs::path ExePath() {
  wchar_t buf[MAX_PATH];
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return fs::path(buf);
}

static int RunXexCheck(const fs::path& xexcheck_exe, const fs::path& target) {
  std::wstring cmdline = L"\"" + xexcheck_exe.wstring() + L"\" \"" + target.wstring() + L"\"";
  std::vector<wchar_t> mutable_cmdline(cmdline.begin(), cmdline.end());
  mutable_cmdline.push_back(L'\0');

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, mutable_cmdline.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                       &si, &pi)) {
    return -1;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return static_cast<int>(code);
}

int main() {
  const fs::path xexcheck_exe = ExePath().parent_path() / SKYLANDERS_XEXCHECK_NAME ".exe";
  CHECK(fs::exists(xexcheck_exe));

  // Non-ASCII directory name: if argv narrows to the system code page (a plain `main(argc, char**
  // argv)` on Windows does this), the path fails to resolve and the tool reports "unreadable" (exit
  // 3) instead of correctly reading the (wrong-content) file and reporting a hash mismatch (exit 2).
  const fs::path dir = fs::temp_directory_path() / L"gr_xexcheck_日本_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  const fs::path target = dir / L"default.xex";
  { std::ofstream(target, std::ios::binary) << "not the real xex"; }

  const int code = RunXexCheck(xexcheck_exe, target);
  CHECK(code == 2);  // kExitMismatch: file WAS read; wrong content, not "wrong/unreadable path"

  fs::remove_all(dir, ec);

  if (g_failures == 0) std::puts("all xex_check_cli_argv tests passed");
  return g_failures == 0 ? 0 : 1;
}

#else  // The argv-narrowing bug this test targets is Windows-specific (POSIX argv is UTF-8 native).

int main() {
  std::puts("skipped (Windows-only test)");
  return 0;
}

#endif
