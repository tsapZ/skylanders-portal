#include "portal/figure_catalog.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <unordered_map>

#include "portal/figure_file.h"
#include "portal/trap_villain.h"
#include "portal/skylander_catalog_data.h"
#include "portal_rex/utf8_path.h"

namespace skylanders::portal {

std::span<const SkylanderInfo> AllSkylanders() { return kSkylanderCatalog; }

const SkylanderInfo* FindSkylander(uint16_t id, uint16_t variant) {
  for (const auto& sky : kSkylanderCatalog) {
    if (sky.id == id && sky.variant == variant) return &sky;
  }
  return nullptr;
}

namespace {

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string TopLevelFolder(const std::filesystem::path& root, const std::filesystem::path& file) {
  std::error_code ec;
  auto rel = std::filesystem::relative(file, root, ec);
  if (ec || rel.empty()) return {};
  auto first = rel.begin();
  if (first == rel.end()) return {};
  // If the first component is the filename itself, the file sits directly under root.
  auto next = first;
  ++next;
  if (next == rel.end()) return {};
  return first->string();
}

// Strips a leading "N. " release-numbering prefix and normalizes "_s " to "'s ", so a folder
// named either "1. Spyro's Adventure" (a user's own numbered dump folder) or plain "Spyro's
// Adventure" (as CreateAndPlaceFigure names it) both match the same catalog game name.
std::string NormalizeGameName(std::string_view raw) {
  std::string s(raw);
  size_t i = 0;
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
  if (i > 0 && i < s.size() && s[i] == '.') {
    s.erase(0, i + 1);
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
  }
  size_t pos = s.find("_s ");
  if (pos != std::string::npos) s.replace(pos, 3, "'s ");
  return s;
}

// Release order for the six mainline games, so the picker and creation lists both group games
// chronologically instead of alphabetically. Unrecognized game names (a custom folder, or "no
// game" for a loose file) sort after all known games, so nothing is hidden or misplaced.
int GameReleaseRank(const std::string& game) {
  if (game.empty()) return 0;
  static const std::unordered_map<std::string, int> kOrder = {
      {"Spyro's Adventure", 1}, {"Giants", 2},   {"Swap Force", 3},
      {"Trap Team", 4},         {"SuperChargers", 5}, {"Imaginators", 6},
  };
  auto it = kOrder.find(NormalizeGameName(game));
  return it != kOrder.end() ? it->second : 999;
}

std::string ResolveDisplayName(const std::filesystem::path& path, const std::string& fallback) {
  auto data = LoadFigureFile(path);
  if (!data) return fallback;
  std::string name = FigureDisplayName(*data);
  return name.empty() ? fallback : name;
}

}  // namespace

std::string FigureDisplayName(const FigureData& data) {
  if (auto trap = ReadTrapVillain(data); trap && trap->villain_id != 0) {
    if (const TrapVillain* v = FindTrapVillain(trap->villain_id, trap->variant)) {
      return std::string(v->name);
    }
  }
  if (const SkylanderInfo* sky = FindSkylander(ReadFigureId(data), ReadFigureVariant(data))) {
    return std::string(sky->name);
  }
  return {};
}

std::vector<FigureCatalogEntry> ScanFigureCatalog(const std::filesystem::path& root) {
  std::vector<FigureCatalogEntry> entries;
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec) || ec) return entries;

  std::filesystem::recursive_directory_iterator it(
      root, std::filesystem::directory_options::skip_permission_denied, ec);
  std::filesystem::recursive_directory_iterator end;
  for (; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || ec) continue;
    const auto& path = it->path();
    // UTF-8, not path::string(): that throws for a name outside the Windows code page (a star or
    // Japanese letters; Core of Light #60, TheBiemGamer) and took the whole list down.
    // .sky too: RPCS3's, Dolphin's and Cemu's virtual figures hold the same 1 KB of toy data as a
    // .dump, so they play as they are (Core of Light #88).
    const std::string extension = Lower(Utf8Path(path.extension()));
    if (extension != ".dump" && extension != ".sky") continue;
    const std::string name = Utf8Path(path.stem());
    entries.push_back({name, ResolveDisplayName(path, name), TopLevelFolder(root, path), path});
  }

  std::sort(entries.begin(), entries.end(), [](const FigureCatalogEntry& a, const FigureCatalogEntry& b) {
    const int ra = GameReleaseRank(a.game), rb = GameReleaseRank(b.game);
    if (ra != rb) return ra < rb;
    const auto ag = Lower(a.game), bg = Lower(b.game);
    if (ag != bg) return ag < bg;
    return Lower(a.name) < Lower(b.name);
  });
  return entries;
}

}  // namespace skylanders::portal
