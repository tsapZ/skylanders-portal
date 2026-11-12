#pragma once

// Mods (the owner's go, 2026-11-09): a mod is a folder in the game's mods folder
// (Documents\Skylanders\<game>\mods\<id>) with a mod.toml and a `files` folder whose files replace
// or add the disc's files at the same paths (as under rom\, e.g. files\text\eng\global.lxb). No
// code: a mod can't run anything, so mods are safe to share and the ports never patch their code
// for one. The mods switched on are a list of ids in order ("a;b", the `mods` setting); a later one
// wins on a file both have. This part reads the manifests and plans what to lay over the disc; it
// has no ReXGlue dependency and is unit-tested (tests/mod_list_test.cpp). portal_rex/mods.cpp hands
// the plan to the SDK (vfs_overlay_roots) and lists the mods on F4 > Mods.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace skylanders::portal {

struct ModManifest {
  // The folder's name if mod.toml leaves it out; if it names one, the folder must be called that
  // (requires and conflicts name mods by it).
  std::string id;
  std::string name, version, author, description;
  // The game it is for, as Core of Light names them: ssa, giants, swapforce, trapteam,
  // superchargers, imaginators.
  std::string game;
  // The disc code pins it was made for (a different pressing can have other files); empty: any.
  std::vector<std::string> discs;
  // Other mods (ids) it needs switched on (mod.toml's `requires`), or can't run with.
  std::vector<std::string> needs;
  std::vector<std::string> conflicts;
};

// mod.toml, a small part of TOML: one `key = "text"` or `key = ["a", "b"]` per line ('text' too,
// as TOML's literal strings), `#` starts a comment; nothing else (no tables, no values over
// several lines). "Text" takes \" \\ \n \t. Unknown keys are left alone. False with `error` naming
// the line.
bool ParseModManifest(std::string_view text, ModManifest& manifest, std::string& error);

struct FoundMod {
  std::filesystem::path folder;
  ModManifest manifest;
  // Why the folder can't be used at all (no or a broken mod.toml, its id isn't the folder's name,
  // no game); empty if it can.
  std::string problem;
};

// Every folder in `mods_dir` (nothing if it isn't there), by name.
std::vector<FoundMod> FindMods(const std::filesystem::path& mods_dir);

struct ModPlan {
  // The mods' files folders to lay over the disc, in order (a later one wins).
  std::vector<std::filesystem::path> overlay_roots;
  // One line per mod switched on: used, or left out and why (for the log and the Mods page).
  std::vector<std::string> lines;
};

// The mods `enabled` names ("a;b"), in that order, each checked: found and usable, for `game`,
// for the disc (when it names discs and `disc_pin` is known), the mods it requires switched on and
// used, none it conflicts with used before it, a files folder there. A mod named twice counts once.
ModPlan PlanMods(const std::vector<FoundMod>& found, std::string_view enabled, std::string_view game,
                 std::string_view disc_pin);

// The ids in a `mods` list, in order, without empty ones and spaces.
std::vector<std::string> ModListIds(std::string_view enabled);
bool ModListHas(std::string_view enabled, std::string_view id);
// The list with `id` switched on (added last if it wasn't there) or off (removed).
std::string ModListWith(std::string_view enabled, std::string_view id, bool on);

}  // namespace skylanders::portal
