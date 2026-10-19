#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "portal/portal_device.h"

namespace skylanders::portal {

// When the figure was last on a portal, as the game wrote it (local time of the console).
struct FigureDate {
  uint16_t year;
  uint8_t month, day, hour, minute;
};

// A Skylander's in-game progress, decoded from its save-data bytes.
struct FigureStats {
  uint8_t level;
  uint32_t xp;       // total, all games' parts together
  uint32_t level_xp;  // total XP the current level starts at
  uint32_t next_xp;  // total XP for the next level; 0 at the top level
  uint32_t gold;
  std::string nickname;
  uint32_t play_seconds;
  uint16_t upgrades;   // the raw upgrade bits (bit 0: a path is chosen, bit 1: the second path)
  uint16_t hat;        // the game's hat number (kTfbSpyroTag_Hat_*), 0 = none
  uint16_t hero_points;
  uint8_t platforms;   // bit 0 Wii, bit 1 Xbox 360, bit 2 PS3
  std::optional<FigureDate> last_played;
};

// Upgrades bought: the upgrade bits without the two path bits.
int UpgradeCount(const FigureStats& stats);
// 0 = no path chosen yet, 1 = the first, 2 = the second.
int UpgradePath(const FigureStats& stats);

// Decodes a figure's progress from its raw bytes. Returns nullopt for a figure with no save data
// yet (freshly created, or never placed in a game) or bytes that don't checksum-validate as real
// save data -- never a guessed or partial result.
//
// Gameplay data lives in AES-128-ECB-encrypted blocks (community reverse-engineering: Marijn
// Kneppers, "Reverse engineering Skylanders' Toys-to-life mechanics", 2024; field layout as used by
// Dolphin's SkylanderFigure, SkyReader and Modified_SkyEditGUI, facts only, no code copied). The
// per-block key is MD5(first 0x20 bytes of the tag + the 1-byte block index + a 53-byte constant,
// " Copyright (C) 2010 Activision. All Rights Reserved. " -- note the trailing space).
//
// The save data is kept twice, in two areas starting at blocks 0x08 and 0x24, and the game writes
// the older one each time: the area whose sequence byte (+0x09) is one more than the other's is
// the newer. An area counts only if its header block's CRC16/CCITT-FALSE (+0x0E, computed with that
// field replaced by the bytes 0x05,0x00) matches. A newly created figure has one area still empty
// (its first save goes to the second), so neither area may be assumed. Giants and later games add
// a second pair (blocks 0x11 and 0x2D, sequence byte +0x02) with their parts of the XP and their
// hats. Verified against the owner's dumps: the newer area holds the higher play time and XP.
std::optional<FigureStats> ParseFigureStats(const FigureData& data);

}  // namespace skylanders::portal
