#pragma once

// The portal menu's favourite figures (SkylandersCoreOfLight #93): one character name per line in
// a player's file (portal_menu_favourites_p<player>.txt), like the pinned entries' file, and which
// of the library's characters those names are. No ReXGlue dependency, so it is unit-tested
// (tests/menu_favourites_test.cpp).

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace skylanders::portal {

struct SkylanderInfo;

// The names in `file`, one per line, blank lines skipped and a trailing CR dropped. A file that
// isn't there has none.
std::vector<std::string> LoadFavourites(const std::filesystem::path& file);

// Writes `names` one per line; an empty list leaves an empty file (the folder is not created).
void SaveFavourites(const std::filesystem::path& file, const std::vector<std::string>& names);

// Whether `name` is in `names` (exact, as Character::name is written to the file).
bool HasFavourite(const std::vector<std::string>& names, std::string_view name);

// Adds `name` at the end, or removes it; true if it was added.
bool ToggleFavourite(std::vector<std::string>& names, std::string_view name);

// The indices of the entries in `characters` whose name is one of `favourites`, in the order of
// `characters`, each once.
std::vector<size_t> FavouriteCharacters(const std::vector<std::string>& characters,
                                        const std::vector<std::string>& favourites);

// The name a figure's favourite is kept under: its character's, as the portal menu's game tabs
// group the versions ("Chop Chop" for "Legendary Chop Chop"; a swapper's halves under the
// swapper's name). Empty for a magic item, which has no favourite. Core of Light's figure
// library uses it too, so a mark made in one shows in the other.
std::string FavouriteName(const SkylanderInfo& sky);

}  // namespace skylanders::portal
