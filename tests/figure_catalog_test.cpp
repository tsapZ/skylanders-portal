#include <filesystem>
#include <fstream>
#include <string_view>
#include <utility>
#include <vector>

#include "portal/figure_catalog.h"
#include "portal/figure_file.h"
#include "portal/trap_villain.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace skylanders::portal;

static void Touch(const fs::path& p) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << "x";
}

int main() {
  fs::path root = fs::temp_directory_path() / L"gr_catalog_root";
  fs::remove_all(root);

  // A missing root gives an empty catalog, not an error.
  CHECK(ScanFigureCatalog(root).empty());

  // A root that is a plain file (not a directory) also gives an empty catalog.
  fs::create_directories(root.parent_path());
  {
    std::ofstream(root, std::ios::binary) << "not a directory";
  }
  CHECK(ScanFigureCatalog(root).empty());
  fs::remove(root);

  // An empty, existing directory gives an empty catalog.
  fs::create_directories(root);
  CHECK(ScanFigureCatalog(root).empty());

  // Files with other extensions are ignored; only *.dump counts.
  Touch(root / L"1. Spyro's Adventure" / L"Spyro.dump");
  Touch(root / L"1. Spyro's Adventure" / L"notes.txt");
  Touch(root / L"1. Spyro's Adventure" / L"readme.dump.bak");
  Touch(root / L"2. Giants" / L"1) Giants" / L"Tree Rex.dump");
  Touch(root / L"2. Giants" / L"1) Giants" / L"Bouncer.dump");
  Touch(root / L"2. Giants" / L"2) New (Series 1)" / L"Chill.dump");
  Touch(root / L"loose.dump");  // directly under root, no game subfolder

  auto entries = ScanFigureCatalog(root);
  CHECK(entries.size() == 5);

  // Sorted by game then name (case-insensitive); entries with no game (game == "") sort first.
  CHECK(entries[0].game.empty());
  CHECK(entries[0].name == "loose");
  CHECK(entries[1].game == "1. Spyro's Adventure");
  CHECK(entries[1].name == "Spyro");
  CHECK(entries[2].game == "2. Giants");
  CHECK(entries[2].name == "Bouncer");
  CHECK(entries[3].game == "2. Giants");
  CHECK(entries[3].name == "Chill");
  CHECK(entries[4].game == "2. Giants");
  CHECK(entries[4].name == "Tree Rex");

  // The game field is the *top-level* folder under root, even for a .dump nested deeper.
  CHECK(entries[3].game == "2. Giants");  // Chill is two levels deep, under "2) New (Series 1)"

  // A path can be opened and matches what was created.
  CHECK(fs::equivalent(entries[4].path, root / L"2. Giants" / L"1) Giants" / L"Tree Rex.dump"));

  // Extension matching is case-insensitive (".DUMP" counts too).
  Touch(root / L"3. Swap Force" / L"Wash Buckler.DUMP");
  auto entries2 = ScanFigureCatalog(root);
  CHECK(entries2.size() == 6);

  // An emulator's figure (.sky: RPCS3, Dolphin, Cemu; the same 1 KB of toy data) counts too, in
  // any case; a name ending in .sky.bak doesn't (Core of Light #88).
  Touch(root / L"3. Swap Force" / L"Blast Zone.sky");
  Touch(root / L"3. Swap Force" / L"Rattle Shake.SKY");
  Touch(root / L"3. Swap Force" / L"old.sky.bak");
  auto entries3 = ScanFigureCatalog(root);
  CHECK(entries3.size() == 8);
  bool sky_listed = false;
  for (const auto& e : entries3) sky_listed = sky_listed || e.name == "Blast Zone";
  CHECK(sky_listed);

  // A raw NFC dump (.bin) counts when it has a figure's size; a .bin of another size is something
  // else and stays out.
  {
    std::ofstream(root / L"3. Swap Force" / L"Hoot Loop.bin", std::ios::binary) << std::string(kFigureSize, 'x');
    std::ofstream(root / L"3. Swap Force" / L"notes.bin", std::ios::binary) << "x";
  }
  auto entries4 = ScanFigureCatalog(root);
  CHECK(entries4.size() == 9);
  bool bin_listed = false;
  for (const auto& e : entries4) bin_listed = bin_listed || e.name == "Hoot Loop";
  CHECK(bin_listed);
  CHECK(IsFigureFileName(L"a.Dump") && IsFigureFileName(L"b.sky") && IsFigureFileName(L"c.BIN"));
  CHECK(!IsFigureFileName(L"old.sky.bak") && !IsFigureFileName(L"readme.txt") && !IsFigureFileName(L"dump"));

  fs::remove_all(root);

  // Games sort by release order, not alphabetically -- "Trap Team" (4th) must come before
  // "Imaginators" (6th), even though "Imaginators" sorts first alphabetically. Unprefixed folder
  // names (as CreateAndPlaceFigure makes them) are recognized the same as numbered ones.
  {
    fs::path order_root = fs::temp_directory_path() / L"gr_catalog_order";
    fs::remove_all(order_root);
    Touch(order_root / L"Imaginators" / L"Ember.dump");
    Touch(order_root / L"Trap Team" / L"Food Fight.dump");
    auto ordered = ScanFigureCatalog(order_root);
    CHECK(ordered.size() == 2);
    if (ordered.size() == 2) {
      CHECK(ordered[0].game == "Trap Team");
      CHECK(ordered[1].game == "Imaginators");
    }
    fs::remove_all(order_root);
  }

  // AllSkylanders/FindSkylander: the built-in catalog is non-empty, sorted by game then name, and
  // a known figure resolves; an unrecognized id/variant does not.
  {
    auto all = AllSkylanders();
    CHECK(!all.empty());
    const SkylanderInfo* tree_rex = FindSkylander(112, 4614);  // real catalog variant, not 0
    CHECK(tree_rex != nullptr);
    if (tree_rex) {
      CHECK(tree_rex->name == "Tree Rex");
      CHECK(tree_rex->game == "Giants");
    }
    CHECK(FindSkylander(0xFFFF, 0xFFFF) == nullptr);
  }

  // Each game's figures are grouped into categories, in this order, with no category split up.
  // Spyro's Adventure chase variants have the same id/variant as the normal figure, so they add
  // no entries of their own.
  {
    const std::vector<std::pair<std::string_view, std::vector<std::string_view>>> expected = {
        {"Spyro's Adventure", {"Cores", "Items", "Sidekicks", "Variants"}},
        {"Giants",
         {"Giants", "Cores", "LightCores", "Sidekicks", "Items", "Variants", "Chase Variants",
          "Unreleased"}},
        {"Swap Force",
         {"Swappers", "Cores", "LightCores", "Items", "Variants", "Chase Variants", "Unreleased"}},
        {"Trap Team",
         {"Trap Masters", "Cores", "Eon's Elite", "Minis", "Items", "Traps", "Variants",
          "Chase Variants", "Unreleased"}},
        {"SuperChargers",
         {"SuperChargers", "Vehicles", "Items", "Eon's Elite", "Variants", "Vehicle Variants",
          "Chase Variants", "Unreleased"}},
        {"Imaginators",
         {"Senseis", "Villain Senseis", "Creation Crystals", "Items", "Sensei Variants",
          "Villain Variants", "Chase Variants", "Unreleased"}},
    };
    std::vector<std::pair<std::string_view, std::vector<std::string_view>>> seen;
    for (const SkylanderInfo& s : AllSkylanders()) {
      if (seen.empty() || seen.back().first != s.game) seen.push_back({s.game, {}});
      auto& cats = seen.back().second;
      if (cats.empty() || cats.back() != s.category) cats.push_back(s.category);
    }
    CHECK(seen == expected);

    const SkylanderInfo* tree_rex = FindSkylander(112, 4614);
    CHECK(tree_rex && tree_rex->category == "Giants");
  }

  // A real .dump file (correct id/variant bytes) resolves display_name via the catalog, even
  // though its filename on disk is something else entirely.
  {
    fs::path dir = fs::temp_directory_path() / L"gr_catalog_display_name";
    fs::remove_all(dir);
    fs::create_directories(dir / L"2. Giants");
    FigureData d{};
    d[0x10] = 112 & 0xFF;
    d[0x11] = 112 >> 8;
    d[0x1C] = 4614 & 0xFF;
    d[0x1D] = 4614 >> 8;  // Tree Rex (real catalog id/variant)
    {
      std::ofstream out(dir / L"2. Giants" / L"my_weird_filename.dump", std::ios::binary);
      out.write(reinterpret_cast<const char*>(d.data()), d.size());
    }
    auto entries = ScanFigureCatalog(dir);
    CHECK(entries.size() == 1);
    if (!entries.empty()) {
      CHECK(entries[0].name == "my_weird_filename");    // filename is unchanged
      CHECK(entries[0].display_name == "Tree Rex");      // but the resolved name is correct
    }
    fs::remove_all(dir);
  }

  // A file with unrecognized id/variant bytes falls back to the filename for display_name.
  {
    fs::path dir = fs::temp_directory_path() / L"gr_catalog_unknown";
    fs::remove_all(dir);
    fs::create_directories(dir);
    FigureData d{};
    d[0x10] = 0xFF;
    d[0x11] = 0xFF;
    d[0x1C] = 0xFF;
    d[0x1D] = 0xFF;
    {
      std::ofstream out(dir / L"Mystery.dump", std::ios::binary);
      out.write(reinterpret_cast<const char*>(d.data()), d.size());
    }
    auto entries = ScanFigureCatalog(dir);
    CHECK(entries.size() == 1);
    if (!entries.empty()) CHECK(entries[0].display_name == "Mystery");
    fs::remove_all(dir);
  }

  // Round-trip: a figure built by CreateBlankFigure is recognized by FindSkylander when read back.
  {
    FigureData created = CreateBlankFigure(110, 4614);  // Bouncer (real catalog variant)
    const SkylanderInfo* found = FindSkylander(ReadFigureId(created), ReadFigureVariant(created));
    CHECK(found != nullptr);
    if (found) CHECK(found->name == "Bouncer");
  }

  // A trap holding a villain is shown by the villain's name; an empty trap by its trap name.
  {
    const TrapVillain* juju = FindTrapVillain(37, false);
    auto villain_trap = CreateTrapWithVillain(212, 12305, *juju);
    CHECK(villain_trap && FigureDisplayName(*villain_trap) == "Bad Juju");
    CHECK(FigureDisplayName(CreateBlankFigure(212, 12305)) == "Air Screamer (Storm Warning)");
    CHECK(FigureDisplayName(CreateBlankFigure(1, 0xFFFF)).empty());

    fs::path dir = fs::temp_directory_path() / L"sp_catalog_villain";
    fs::remove_all(dir);
    fs::create_directories(dir / L"Trap Team");
    CHECK(SaveFigureFileAtomic(dir / L"Trap Team" / L"some trap.dump", *villain_trap));
    auto entries = ScanFigureCatalog(dir);
    CHECK(entries.size() == 1 && entries[0].display_name == "Bad Juju");
    fs::remove_all(dir);
  }

  return Finish("figure_catalog");
}
