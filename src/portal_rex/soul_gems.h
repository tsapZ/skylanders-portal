#pragma once
// A Soul Gem picked up (CoL #71, groundwork for the Subspace mode). Every game plays the movie of
// the Skylander whose Soul Gem was just found, one file per Skylander: Spyro's Adventure
// movies\<name>.bik, Giants movies\<Name>.mov, Swap Force archives\Soul_Gem_<name>_Mix_360.bk2,
// Trap Team movies\soulGem_<toy id>.mov, SuperChargers and Imaginators archives\SG_<name>.bk2.
// Each game names the Skylander for a path it opened (its own SoulGemOf); the log gets one line
// per Skylander ("Soul Gem movie: Chop Chop (game:\movies\chopchop.bik)"). Nothing acts on it
// yet: first the runs show that these movies open at a pickup and nowhere else.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace skylanders {

// `skylander_of`: the Skylander for a path the game opened if the path is one of its Soul Gem
// movies, else "". Set it once, before the game runs.
void WatchSoulGems(std::string (*skylander_of)(std::string_view path));

// The Skylanders whose Soul Gem movie opened since the game started, oldest first, each once.
// Any thread.
std::vector<std::string> SoulGemsSeen();

// For a game's skylander_of. The file name of `path` without its folders and extension if the
// extension is `extension` (any case), else "": ("game:\movies\Bash.mov", ".mov") -> "Bash".
std::string_view FileStem(std::string_view path, std::string_view extension);

// Whether the file's own folder is `folder` (any case): ("game:\movies\Bash.mov", "movies") -> true.
bool InFolder(std::string_view path, std::string_view folder);

// `text` from `prefix` on cut off (any case), else "": ("SG_KingPen", "SG_") -> "KingPen".
std::string_view AfterPrefix(std::string_view text, std::string_view prefix);

// The catalog's Skylander whose name, letters and digits only in lower case, is the same as
// `name`'s ("chopchop", "ChopChop" -> "Chop Chop"; the first in release order), else "".
std::string SkylanderNamed(std::string_view name);

// The catalog's Skylander with this toy id (its first entry), else "".
std::string SkylanderWithId(uint16_t id);

}  // namespace skylanders
