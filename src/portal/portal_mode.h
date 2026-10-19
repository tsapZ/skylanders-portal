#pragma once

#include <optional>
#include <string_view>

namespace skylanders::portal {

// kHybrid: the emulated portal for the game, with the figures of a real USB portal mirrored into
// it (portal/hybrid/real_portal_bridge.h).
enum class PortalMode { kNone, kSoftware, kUsb, kHybrid };

// Accepts "none", "software", "usb" or "hybrid" (any case, surrounding whitespace ignored).
// Anything else, including modes that do not exist, gives nullopt.
std::optional<PortalMode> ParsePortalMode(std::string_view text);

}  // namespace skylanders::portal
