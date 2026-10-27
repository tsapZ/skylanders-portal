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
};

inline int ExitCodeForStatus(XexCheck status) {
  switch (status) {
    case XexCheck::Match:
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

}  // namespace skylanders::xex
