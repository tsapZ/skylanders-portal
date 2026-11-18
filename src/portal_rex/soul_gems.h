#pragma once
// A Soul Gem picked up (CoL #71, groundwork for the Subspace mode). Every game plays the movie of
// the Skylander whose Soul Gem was just found, one file per Skylander: Spyro's Adventure
// movies\<name>.bik, Giants movies\<Name>.mov, Swap Force archives\Soul_Gem_<name>_Mix_360.bk2,
// Trap Team movies\soulGem_<toy id>.mov, SuperChargers and Imaginators archives\SG_<name>.bk2.
// Each game names the Skylander for a path it opened (its own SoulGemOf, with the helpers of
// portal/soul_gem_names.h); the log gets one line per Skylander ("Soul Gem movie: Chop Chop
// (game:\movies\chopchop.bik)"), and a Soul Gem run (run_rules.h) unlocks it.

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "portal/soul_gem_names.h"

namespace skylanders {

// `skylander_of`: the Skylander for a path the game opened if the path is one of its Soul Gem
// movies, else "". Set it once, before the game runs.
void WatchSoulGems(std::string (*skylander_of)(std::string_view path));

// The Skylanders whose Soul Gem movie opened since the game started, oldest first, each once.
// Any thread.
std::vector<std::string> SoulGemsSeen();

// Soul Gems from the game's slot saves, for a game whose movie only opens when the player watches the
// preview (Spyro's Adventure, 2026-11-16: with No nothing opens; the save always gets the gem, see
// portal/save_medals.h). `slots` lists the slot files to watch; `finds` names the Skylanders whose gems
// the newer save has and the older one hasn't (an empty `before` for a slot that is new). A thread of its
// own looks at the files' times once a second and reads a file once its time stayed the same for a
// second (the game has finished writing it); each name goes the movie's way (a log line "Soul Gem
// (save): <name>", SoulGemsSeen, OnSoulGem). Set once, before the game runs.
void WatchSoulGemSaves(std::vector<std::filesystem::path> (*slots)(),
                       std::vector<std::string> (*finds)(std::span<const uint8_t> before,
                                                         std::span<const uint8_t> after));

// Called with each Skylander's name right after its line (the portal menu: a Soul Gem run's
// find), on the thread that opened the movie or read the save; `from_save` for a find from a save
// (only a real pickup sets it, also in a hub; a movie can be watched again there). Set once, before
// the game runs.
void OnSoulGem(void (*found)(const std::string& skylander, bool from_save));

// The games' SoulGemOf helpers (portal/soul_gem_names.h), here as the games know them.
using portal::AfterPrefix;
using portal::FileStem;
using portal::InFolder;
using portal::SameName;
using portal::SkylanderNamed;
using portal::SkylanderWithId;

}  // namespace skylanders
