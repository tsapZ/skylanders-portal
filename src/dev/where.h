// Where the player is, for problem reports. A game reports its level and player 1's position from
// its own thread (every frame is fine); the log gets a "Where:" line once a new level has its
// player, every five minutes of play when the player moved, at a crash and with a problem note. So
// a player's log says where a problem happened, and a test run can go back there without the
// player's save.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace skylanders::dev {

// The level's name and, if the level has a player yet, player 1's position (x, y, z as the game
// keeps them).
void ReportPlace(const std::string& level, bool has_player, float x, float y, float z);

// "Level_014 at -10.71 -2.04 22.70", "Level_014 (no player)", or empty before the first report.
// Safe from any thread, also at a crash.
std::string PlaceText();

// Logs the "Where:" line now, with the reason ("crash", "problem note"); nothing before the first
// report.
void LogPlace(const char* why);

// For a game that names its level after the level archive it opened last (Swap Force,
// SuperChargers, Imaginators keep each level in one archive): `name_of` turns a path the game
// opened ("game:\archives\Level_503.pak") into the level's name, or "" for any other file. Called
// for every file the game opens (the SDK's rex::filesystem open observer), on the thread that
// opened it. Set it once, before the game runs.
void WatchLevelArchives(std::string (*name_of)(std::string_view path));

// The level of the last level archive the game opened ("" before the first), for ReportPlace.
std::string ArchiveLevel();

// For a game that sees each player once a frame without knowing which one is player 1 (the
// per-player checks of Swap Force, SuperChargers and Imaginators): `who` the player's object, its
// position, and whether that is relative to something it stands in or on (a vehicle). The first
// of the players reported in the last half second counts as player 1; its reports go to
// ReportPlace with ArchiveLevel() as the level, from a second after it came. From the game's
// thread.
void ReportPlayerFrame(uint32_t who, float x, float y, float z, bool relative);

}  // namespace skylanders::dev
