#pragma once
// A Soul Gem picked up (CoL #71, groundwork for the Subspace mode). Every game plays the movie of
// the Skylander whose Soul Gem was just found, one file per Skylander: Spyro's Adventure
// movies\<name>.bik, Giants movies\<Name>.mov, Swap Force archives\Soul_Gem_<name>_Mix_360.bk2,
// Trap Team movies\soulGem_<toy id>.mov, SuperChargers and Imaginators archives\SG_<name>.bk2.
// Each game names the Skylander for a path it opened (its own SoulGemOf, with the helpers of
// portal/soul_gem_names.h); the log gets one line per Skylander ("Soul Gem movie: Chop Chop
// (game:\movies\chopchop.bik)"), and a Soul Gem run (run_rules.h) unlocks it.

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

// Called with each Skylander's name right after its line (the portal menu: a Soul Gem run's
// find), on the thread that opened the movie. Set once, before the game runs.
void OnSoulGem(void (*found)(const std::string& skylander));

// The games' SoulGemOf helpers (portal/soul_gem_names.h), here as the games know them.
using portal::AfterPrefix;
using portal::FileStem;
using portal::InFolder;
using portal::SameName;
using portal::SkylanderNamed;
using portal::SkylanderWithId;

}  // namespace skylanders
