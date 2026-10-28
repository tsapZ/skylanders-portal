// Where the player is, for problem reports. A game reports its level and player 1's position from
// its own thread (every frame is fine); the log gets a "Where:" line once a new level has its
// player, every five minutes of play when the player moved, at a crash and with a problem note. So
// a player's log says where a problem happened, and a test run can go back there without the
// player's save.

#pragma once

#include <string>

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

}  // namespace skylanders::dev
