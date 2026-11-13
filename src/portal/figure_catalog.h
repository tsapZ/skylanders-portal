#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "portal/portal_device.h"

namespace skylanders::portal {

struct SkylanderInfo {
  uint16_t id;
  uint16_t variant;
  std::string_view name;
  std::string_view game;
  std::string_view category;  // group within the game, e.g. "Cores" or "Variants"
};

// The full built-in catalog, sorted by game, then category (in the game's own order), then name -- the order any UI walking it should use.
std::span<const SkylanderInfo> AllSkylanders();

// Looks up a figure's real name/game from its id/variant. Returns nullptr if the pair isn't in
// the built-in catalog (e.g. a homebrew or unrecognized figure).
const SkylanderInfo* FindSkylander(uint16_t id, uint16_t variant);

// The name to show for a figure: for a Trap Team trap holding a villain, the villain's name
// ("Bad Juju"); otherwise the catalog name. Empty if the figure isn't in the catalog.
std::string FigureDisplayName(const FigureData& data);

struct FigureCatalogEntry {
  std::string name;          // filename, without extension
  std::string display_name;  // resolved Skylander name if recognized, else same as `name`
  std::string game;
  std::filesystem::path path;
};

// Recursively finds every figure file under `root`: *.dump, *.sky, and *.bin of exactly 1 KB
// (IsFigureFileName in figure_file.h; any case). A missing root,
// a root that is not a directory, or a root with nothing found all give an empty result — never an
// error. `game` is the top-level folder directly under `root` the file was found in ("" if the
// file sits directly under `root`). Entries are sorted by game, then by name, case-insensitively;
// entries with no game sort first.
std::vector<FigureCatalogEntry> ScanFigureCatalog(const std::filesystem::path& root);

}  // namespace skylanders::portal
