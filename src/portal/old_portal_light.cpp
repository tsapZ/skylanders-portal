#include "portal/old_portal_light.h"

namespace skylanders::portal {

std::optional<Report> LightForOldPortal(const Report& command) {
  switch (command[0]) {
    case 'J': {  // J side r g b time-lo time-hi
      if (command[1] != 0x00) return std::nullopt;
      Report c{};
      c[0] = 'C';
      c[1] = command[2];
      c[2] = command[3];
      c[3] = command[4];
      return c;
    }
    case 'L':
      return std::nullopt;
    default:
      return command;
  }
}

}  // namespace skylanders::portal
