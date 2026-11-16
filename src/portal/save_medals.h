#pragma once
// Soul Gems from a game's own slot save (Core of Light #71, 2026-11-16). Spyro's Adventure keeps, per
// level, a list of what was done and collected, keyed by the zlib CRC-32 of names: when a level's Soul
// Gem is picked up, the level's group (crc32("Level_027") = 2F2FA0CC) gets the item "champion upgrade
// medal" (crc32 5E19E0C0) with the value 1, also when the player says No to the preview movie (so that
// movie is no signal there), and the game saves the slot right away. Found by diffing the slot before and
// after the pickup. A group as it sits in SLOT.DAT, big-endian, on a 2-byte boundary:
//   u32 group key, u16 count, u16 0, then count x (u32 item key, u32 value).

#include <cstdint>
#include <span>
#include <string_view>

namespace skylanders::portal {

// The zlib CRC-32 of `name` (the keys of the save's groups and items).
uint32_t NameCrc32(std::string_view name);

// The value of `item` in the save's group `group`, or 0 when the group or the item isn't there. A
// group key found with a header that doesn't fit (a count over 512, the u16 after it not 0, the
// items past the end) is passed over: the same four bytes can stand anywhere in other data.
uint32_t GroupItem(std::span<const uint8_t> save, uint32_t group, uint32_t item);

// "champion upgrade medal": the Soul Gem item in Spyro's Adventure's level groups.
inline constexpr uint32_t kSsaSoulGemItem = 0x5E19E0C0;

}  // namespace skylanders::portal
