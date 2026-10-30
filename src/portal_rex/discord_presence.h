#pragma once

// The game as the player's Discord status ("Rich Presence"): which game, with which Skylander, in
// which level and for how long, on the player's Discord profile while the Discord app runs on the
// same PC. Off unless the player switches it on (discord_presence): it tells their Discord friends
// what they play. Only names from the figure catalog and the game's level names go out, never a
// figure's nickname or anything else from the player's files.
//
// It talks to the Discord app over its local pipe (\\.\pipe\discord-ipc-0 .. 9), without Discord's
// SDK. Each game is a Discord application of its own ("Skylanders | SuperChargers"), with the
// game's logo uploaded there as the art asset "logo"; Discord shows the application's name as the
// game.

#include <functional>
#include <string>
#include <string_view>

#include "portal_rex/settings_menu.h"

namespace skylanders {

// Starts the status for `game` ("Spyro's Adventure", "Giants", "Swap Force", "Trap Team",
// "SuperChargers", "Imaginators", as for UseSharedDataFolder): a thread of its own that connects
// to Discord while discord_presence is on and Discord runs, and lets go when it is switched off.
// Call once from ReXApp::OnPostSetup, after InstallPortal. Nothing for another name.
void StartDiscordPresence(std::string_view game);

// The level's name as the game shows it, for the level name dev/where.h has from the game
// ("Level_014", "nat1"); empty leaves the level out (menus, levels without a name). Without a
// provider the status shows no level. Called on the status thread.
using DiscordLevelNameProvider = std::function<std::string(const std::string& level)>;
void SetDiscordLevelNames(DiscordLevelNameProvider provider);

// The F4 > Spiel row for discord_presence (on / off, takes effect at once). Every game's game
// page has it.
Setting DiscordPresenceSetting();

}  // namespace skylanders
