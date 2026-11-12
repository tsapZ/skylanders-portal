#pragma once

// The mods a game starts with, and F4 > Mods (portal/mod_list.h says what a mod is). The mods
// folder is "mods" in the game's data folder (UseSharedDataFolder decides that folder, so call it
// first), e.g. Documents\Skylanders\Swap Force\mods\<id>.

#include <string_view>

#include "portal_rex/settings_menu.h"

namespace skylanders {

// Reads the mods folder, checks the mods the `mods` setting switches on (in its order), and hands
// their files folders to the SDK (vfs_overlay_roots), which lays them over the disc's files when it
// mounts game:. Logs one line per mod switched on and the folder. Call in OnPreSetup: the settings
// file is read by then, and the runtime isn't built yet. `game`: ssa, giants, swapforce, trapteam,
// superchargers or imaginators. `disc_pin`: the running disc's code pin if the game knows it (a
// mod naming discs is checked against it), else empty.
void ApplyMods(std::string_view game, std::string_view disc_pin = {});

// The Mods page: a row per mod in the folder (off / on, with its name, version, author and what it
// does in the help; why it can't be used if it can't), and "Open the mods folder". Changes apply at
// the next start, like the language.
SettingsPage ModsPage(std::string_view game);

}  // namespace skylanders
