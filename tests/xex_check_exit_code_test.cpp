#include "xex/xex_check_exit_code.h"

#include <cstdio>

using skylanders::xex::ExitCodeForStatus;
using skylanders::xex::XexCheck;

static int g_failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

int main() {
  CHECK(ExitCodeForStatus(XexCheck::Match) == 0);
  CHECK(ExitCodeForStatus(XexCheck::Mismatch) == 2);
  CHECK(ExitCodeForStatus(XexCheck::Unreadable) == 3);
  CHECK(ExitCodeForStatus(XexCheck::BadExpected) == 4);

  if (g_failures == 0) std::puts("all xex_check_exit_code tests passed");
  return g_failures == 0 ? 0 : 1;
}
