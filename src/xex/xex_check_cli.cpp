#include "xex/xex_check_exit_code.h"
#include "xex/xex_verify.h"

#include <cstdio>
#include <filesystem>

// <game>_xexcheck <path-to-default.xex>: prints OK, or the problem and exits with its code
// (xex_check_exit_code.h). Built per game by skylanders_game_xex() (cmake/game_xex.cmake), which
// defines the hashes, the game's title and the tool's name.
#if !defined(SKYLANDERS_XEX_SHA256) || !defined(SKYLANDERS_GAME_TITLE) || !defined(SKYLANDERS_XEXCHECK_NAME)
#error "SKYLANDERS_XEX_SHA256, SKYLANDERS_GAME_TITLE and SKYLANDERS_XEXCHECK_NAME come from skylanders_game_xex()"
#endif

namespace {

int RunCheck(const std::filesystem::path& xex) {
  const auto result = skylanders::xex::VerifyXex(xex, SKYLANDERS_XEX_SHA256);
  const auto problem = skylanders::xex::DescribeXexProblem(xex, result, SKYLANDERS_XEX_SHA256,
                                                           SKYLANDERS_GAME_TITLE);

  if (problem.empty()) {
    std::puts("OK");
    return skylanders::xex::kExitMatch;
  }
  std::fputs(problem.c_str(), stderr);
  std::fputc('\n', stderr);
  return skylanders::xex::ExitCodeForStatus(result.status);
}

}  // namespace

#ifdef _WIN32

// A plain main(argc, char** argv) receives argv narrowed to the system code page on Windows, so any
// path character outside it (e.g. a non-ASCII Windows username under a user's own AppData path)
// silently becomes '?' and the file is reported as unreadable. wmain gets the real Unicode argv,
// and std::filesystem::path's wstring constructor preserves it exactly -- no narrowing anywhere.
int wmain(int argc, wchar_t** argv) {
  if (argc != 2) {
    std::fwprintf(stderr, L"usage: %s <path-to-default.xex>\n",
                  argc > 0 ? argv[0] : L"" SKYLANDERS_XEXCHECK_NAME);
    return skylanders::xex::kExitBadExpected;
  }
  return RunCheck(std::filesystem::path(argv[1]));
}

#else

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <path-to-default.xex>\n",
                 argc > 0 ? argv[0] : SKYLANDERS_XEXCHECK_NAME);
    return skylanders::xex::kExitBadExpected;
  }
  return RunCheck(std::filesystem::path(argv[1]));
}

#endif
