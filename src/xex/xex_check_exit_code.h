#pragma once

#include "xex/xex_verify.h"

namespace skylanders::xex {

// Exit codes for a game's <game>_xexcheck CLI. The installer's Inno Setup script reads these via
// Exec()'s ResultCode to decide whether to proceed past the fingerprint-check wizard step.
enum XexCheckExitCode {
  kExitMatch = 0,
  kExitMismatch = 2,
  kExitUnreadable = 3,
  kExitBadExpected = 4,
  // Another build of the game whose own exe ships next to this one (XexCheckResult::sibling_exe): the setup takes the
  // disc, the game starts that exe (2026-11-26, SuperChargers' French / Dutch and Scandinavian discs).
  kExitSibling = 5,
};

inline int ExitCodeForStatus(XexCheck status) {
  switch (status) {
    case XexCheck::Match:
    case XexCheck::SameCode:  // another pressing with the same code: runs as well
      return kExitMatch;
    case XexCheck::Mismatch:
      return kExitMismatch;
    case XexCheck::Unreadable:
      return kExitUnreadable;
    case XexCheck::BadExpected:
      return kExitBadExpected;
  }
  return kExitBadExpected;
}

inline int ExitCodeForResult(const XexCheckResult& result) {
  if (result.status == XexCheck::Mismatch && result.other_build && !result.sibling_exe.empty()) return kExitSibling;
  return ExitCodeForStatus(result.status);
}

}  // namespace skylanders::xex
