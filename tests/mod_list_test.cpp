#include <filesystem>
#include <fstream>
#include <string>

#include "portal/mod_list.h"
#include "test_util.h"

using namespace skylanders::portal;
namespace fs = std::filesystem;

static void WriteFile(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

static void ManifestTests() {
  ModManifest m;
  std::string error;
  CHECK(ParseModManifest("# a comment\n"
                         "name = \"Bigger \\\"hats\\\"\"   # after\n"
                         "version = '1.0'\n"
                         "game = \"swapforce\"\r\n"
                         "discs = [\"823E0000:0dcab6aa\", 'x']\n"
                         "requires = []\n"
                         "conflicts = [ \"other\" , ]\n"
                         "unknown_key = \"left alone\"\n",
                         m, error));
  CHECK(error.empty());
  CHECK(m.name == "Bigger \"hats\"");
  CHECK(m.version == "1.0");
  CHECK(m.game == "swapforce");
  CHECK(m.discs.size() == 2 && m.discs[0] == "823E0000:0dcab6aa" && m.discs[1] == "x");
  CHECK(m.needs.empty());
  CHECK(m.conflicts.size() == 1 && m.conflicts[0] == "other");
  // A byte order mark is skipped.
  CHECK(ParseModManifest("\xEF\xBB\xBFgame = \"ssa\"\n", m, error) && m.game == "ssa");
  // Broken lines name the line.
  CHECK(!ParseModManifest("game = \"ssa\"\nname = \"no end\n", m, error));
  CHECK(error.find("line 2") != std::string::npos);
  CHECK(!ParseModManifest("game ssa\n", m, error));
  CHECK(!ParseModManifest("game = \"a\"\ngame = \"b\"\n", m, error));
  CHECK(!ParseModManifest("requires = \"a\"\n", m, error));
  CHECK(!ParseModManifest("requires = [\"a\"\n", m, error));
  CHECK(!ParseModManifest("name = \"a\" b\n", m, error));
  CHECK(!ParseModManifest("name = \"bad \\q\"\n", m, error));
  CHECK(!ParseModManifest("[table]\n", m, error));
}

static void ListTests() {
  CHECK(ModListIds(" a; ;b ;a") == (std::vector<std::string>{"a", "b", "a"}));
  CHECK(ModListHas("a;b", "b") && !ModListHas("a;b", "c"));
  CHECK(ModListWith("a;b", "c", true) == "a;b;c");
  CHECK(ModListWith("a;b", "a", true) == "b;a");
  CHECK(ModListWith("a;b;c", "b", false) == "a;c");
  CHECK(ModListWith("", "x", false).empty());
}

static void FolderTests() {
  const fs::path root = fs::temp_directory_path() / "skylanders_mod_list_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  // Usable ones.
  WriteFile(root / "hats" / "mod.toml", "name = \"Hats\"\nversion = \"2\"\ngame = \"swapforce\"\n");
  WriteFile(root / "hats" / "files" / "text" / "a.txt", "x");
  WriteFile(root / "voices" / "mod.toml", "game = \"swapforce\"\nrequires = [\"hats\"]\n");
  WriteFile(root / "voices" / "files" / "b.txt", "x");
  WriteFile(root / "rival" / "mod.toml", "game = \"swapforce\"\nconflicts = [\"hats\"]\n");
  WriteFile(root / "rival" / "files" / "c.txt", "x");
  WriteFile(root / "pinned" / "mod.toml", "game = \"swapforce\"\ndiscs = [\"P1\"]\n");
  WriteFile(root / "pinned" / "files" / "d.txt", "x");
  // Not usable.
  WriteFile(root / "giants-only" / "mod.toml", "game = \"giants\"\n");
  WriteFile(root / "giants-only" / "files" / "e.txt", "x");
  WriteFile(root / "renamed" / "mod.toml", "id = \"other-name\"\ngame = \"swapforce\"\n");
  WriteFile(root / "broken" / "mod.toml", "game = \n");
  WriteFile(root / "no-files" / "mod.toml", "game = \"swapforce\"\n");
  WriteFile(root / "no-game" / "mod.toml", "name = \"x\"\n");
  fs::create_directories(root / "empty");
  WriteFile(root / "a file.txt", "not a mod");

  const std::vector<FoundMod> found = FindMods(root);
  CHECK(found.size() == 10);
  auto problem_of = [&](const std::string& id) -> std::string {
    for (const FoundMod& mod : found) {
      if (mod.folder.filename() == id) return mod.problem.empty() ? "ok" : mod.problem;
    }
    return "missing";
  };
  CHECK(problem_of("hats") == "ok");
  CHECK(problem_of("renamed").find("other-name") != std::string::npos);
  CHECK(problem_of("broken").find("line 1") != std::string::npos);
  CHECK(problem_of("no-game").find("which game") != std::string::npos);
  CHECK(problem_of("empty") == "no mod.toml");
  CHECK(problem_of("a file.txt") == "missing");
  for (const FoundMod& mod : found) {
    if (mod.folder.filename() == "hats") CHECK(mod.manifest.name == "Hats" && mod.manifest.id == "hats");
    if (mod.folder.filename() == "voices") CHECK(mod.manifest.name == "voices");
  }

  // In order, each checked.
  ModPlan plan = PlanMods(found, "voices;hats;rival;giants-only;missing;no-files;hats", "swapforce", "");
  CHECK(plan.overlay_roots.size() == 2);
  CHECK(plan.overlay_roots.size() == 2 && plan.overlay_roots[0] == root / "voices" / "files" &&
        plan.overlay_roots[1] == root / "hats" / "files");
  CHECK(plan.lines.size() == 6);
  CHECK(plan.lines.size() == 6 && plan.lines[0] == "mod voices: on");
  CHECK(plan.lines.size() == 6 && plan.lines[1] == "mod hats 2: on");
  CHECK(plan.lines.size() == 6 && plan.lines[2] == "mod rival: left out, can't run with hats");
  CHECK(plan.lines.size() == 6 && plan.lines[3].find("another game (giants)") != std::string::npos);
  CHECK(plan.lines.size() == 6 && plan.lines[4].find("not in the mods folder") != std::string::npos);
  CHECK(plan.lines.size() == 6 && plan.lines[5].find("no files folder") != std::string::npos);
  // A requirement left out takes its dependants with it.
  plan = PlanMods(found, "voices", "swapforce", "");
  CHECK(plan.overlay_roots.empty());
  CHECK(plan.lines.size() == 1 && plan.lines[0].find("needs hats") != std::string::npos);
  // Disc pins: checked only when both are known.
  CHECK(PlanMods(found, "pinned", "swapforce", "P1").overlay_roots.size() == 1);
  CHECK(PlanMods(found, "pinned", "swapforce", "").overlay_roots.size() == 1);
  CHECK(PlanMods(found, "pinned", "swapforce", "P2").overlay_roots.empty());
  // Nothing switched on, no folder at all.
  CHECK(PlanMods(found, "", "swapforce", "").lines.empty());
  CHECK(FindMods(root / "not-there").empty());
  fs::remove_all(root, ec);
}

int main() {
  ManifestTests();
  ListTests();
  FolderTests();
  return Finish("mod_list");
}
