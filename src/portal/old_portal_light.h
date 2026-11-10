#pragma once

#include <optional>

#include "portal/portal_device.h"

namespace skylanders::portal {

// A portal from before Trap Team (its ready reply is 52 01 xx) has one light, set with
// C r g b. Trap Team, SuperChargers and Imaginators light a Traptanium portal side by side
// instead (J side r g b time, L for the trap light), and an old portal ignores those: in Trap
// Team on Automatic it stayed dark (the owner's portal, 52 01 3D, 2026-11-10). The game is told
// every old portal is a Traptanium one (UsbPortal, so the newer games take it), so this is
// what such a portal gets for a light command instead:
//   C               as it is
//   J for side 0    C with that colour, at once (an old portal has no fade)
//   J other sides   nothing: they would only make the one light jump between two colours
//   L               nothing (no trap light)
// Anything that isn't a light command comes back as it is.
std::optional<Report> LightForOldPortal(const Report& command);

}  // namespace skylanders::portal
