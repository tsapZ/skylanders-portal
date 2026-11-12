#pragma once
// Naming the Skylander of a Soul Gem movie a game opens (portal_rex/soul_gems.h, Core of Light #71):
// the path helpers and the catalog lookups the games' SoulGemOf functions use, without ReXGlue, so
// they are unit-tested (tests/soul_gem_names_test.cpp).

#include <cstdint>
#include <string>
#include <string_view>

namespace skylanders::portal {

// The file name of `path` without its folders and extension if the extension is `extension` (any
// case), else "": ("game:\movies\Bash.mov", ".mov") -> "Bash".
std::string_view FileStem(std::string_view path, std::string_view extension);

// Whether the file's own folder is `folder` (any case): ("game:\movies\Bash.mov", "movies") -> true.
bool InFolder(std::string_view path, std::string_view folder);

// `text` from `prefix` on cut off (any case), else "": ("SG_KingPen", "SG_") -> "KingPen".
std::string_view AfterPrefix(std::string_view text, std::string_view prefix);

// Whether two names are the same in any case: ("DaBomb", "Dabomb") -> true.
bool SameName(std::string_view a, std::string_view b);

// The catalog's Skylander whose name, letters and digits only in lower case, is the same as `name`'s
// ("chopchop", "ChopChop" -> "Chop Chop"; the first in release order), else "".
std::string SkylanderNamed(std::string_view name);

// The catalog's Skylander with this toy id (its first entry), else "".
std::string SkylanderWithId(uint16_t id);

}  // namespace skylanders::portal
