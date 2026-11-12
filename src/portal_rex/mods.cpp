#include "portal_rex/mods.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "portal/mod_list.h"
#include "portal_rex/portal_rex.h"
#include "portal_rex/utf8_path.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

REXCVAR_DEFINE_STRING(mods, "", "Game",
                      "The mods switched on, by their folder names in the game's mods folder, in "
                      "order (a later one wins on the same file), separated by ; (F4 > Mods)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace skylanders {

namespace {

namespace fs = std::filesystem;

fs::path ModsFolder() {
  const fs::path& root = SharedUserDataRoot();
  return root.empty() ? fs::path() : root / "mods";
}

}  // namespace

void ApplyMods(std::string_view game, std::string_view disc_pin) {
  const fs::path folder = ModsFolder();
  const std::string enabled = REXCVAR_GET(mods);
  std::vector<fs::path> overlay_roots;
  if (!folder.empty()) {
    const std::vector<portal::FoundMod> found = portal::FindMods(folder);
    if (!found.empty() || !enabled.empty()) {
      const portal::ModPlan plan = portal::PlanMods(found, enabled, game, disc_pin);
      REXLOG_INFO("Mods: {} in {}, {} switched on", found.size(), Utf8Path(folder),
                  portal::ModListIds(enabled).size());
      for (const std::string& line : plan.lines) REXLOG_INFO("Mods: {}", line);
      overlay_roots = plan.overlay_roots;
    }
  }
  // By name: an SDK without the overlay (older than feat/vfs-overlay) says so instead of failing
  // to link.
  if (!rex::cvar::GetFlagInfo("vfs_overlay_roots")) {
    if (!overlay_roots.empty()) {
      REXLOG_WARN("Mods: this build can't lay files over the disc (no vfs_overlay_roots); left out");
    }
    return;
  }
  // | between the folders: Windows doesn't allow it in a name, ; it does (the code review
  // 2026-11-12: a data folder with ; in its path split into wrong folders).
  std::string roots;
  for (const fs::path& root : overlay_roots) {
    if (!roots.empty()) roots += '|';
    roots += Utf8Path(root);
  }
  // The overlay list belongs to the mods: set at every start, empty when none is on, so a value
  // the settings file kept (the SDK's Save to config writes every changed setting) can't keep mods
  // on that were switched off, and a value from the command line is replaced, with a line saying so
  // (the code review 2026-11-12).
  const std::string before = rex::cvar::GetFlagByName("vfs_overlay_roots");
  if (!before.empty() && before != roots) {
    REXLOG_INFO("Mods: vfs_overlay_roots from the settings file or command line replaced by the mods' list");
  }
  rex::cvar::SetFlagByName("vfs_overlay_roots", roots);
}

SettingsPage ModsPage(std::string_view game) {
  SettingsPage page{"Mods", "Mods", {}};
  const fs::path folder = ModsFolder();
  const std::string game_key(game);
  for (const portal::FoundMod& mod : portal::FindMods(folder)) {
    const portal::ModManifest& m = mod.manifest;
    Setting s;
    s.en = m.name;
    s.de = m.name;
    std::string about;
    if (!m.version.empty()) about += "Version " + m.version + "\n";
    if (!m.author.empty()) about += "By " + m.author + "\n";
    if (!m.description.empty()) about += "\n" + m.description + "\n";
    std::string about_de;
    if (!m.version.empty()) about_de += "Version " + m.version + "\n";
    if (!m.author.empty()) about_de += "Von " + m.author + "\n";
    if (!m.description.empty()) about_de += "\n" + m.description + "\n";
    std::string problem = mod.problem;
    if (problem.empty() && m.game != game_key) problem = "made for another game (" + m.game + ")";
    if (!problem.empty()) {
      // Shown, so its maker sees why; it can still be switched (the log says it's left out).
      s.help_en = about + "\nCan't be used: " + problem + ".";
      s.help_de = about_de + "\nGeht nicht: " + problem + ".";
    } else {
      s.help_en = about + "\nTakes effect at the next start.";
      s.help_de = about_de + "\nWirkt ab dem n\xC3\xA4" "chsten Start.";
    }
    s.options = {{"off", "Off", "Aus"}, {"on", "On", "An"}};
    s.restart = true;
    const std::string id = m.id;
    // The menu applies a change to the setting at once (saved with Y, put back if not), so the
    // list read here has the other rows' changes.
    s.current = [id] {
      return std::string(portal::ModListHas(rex::cvar::GetFlagByName("mods"), id) ? "on" : "off");
    };
    s.preset = [id](const std::string& value) {
      return std::vector<std::pair<std::string, std::string>>{
          {"mods", portal::ModListWith(rex::cvar::GetFlagByName("mods"), id, value == "on")}};
    };
    page.settings.push_back(std::move(s));
  }
  Setting open;
  open.en = "Open the mods folder";
  open.de = "Mods-Ordner \xC3\xB6" "ffnen";
  open.help_en = "A mod is a folder in here with a mod.toml and a files folder whose files take\n"
                 "the place of the disc's at the same path (as under rom).\n"
                 "\n"
                 "Mods come from players, not from this project: use the ones you trust.";
  open.help_de = "Ein Mod ist ein Ordner hier drin mit einer mod.toml und einem Ordner files,\n"
                 "dessen Dateien die der Disc am selben Pfad ersetzen (wie unter rom).\n"
                 "\n"
                 "Mods kommen von Spielern, nicht von diesem Projekt: nimm die, denen du traust.";
  open.action = [folder] {
    if (folder.empty()) return;
    std::error_code ec;
    fs::create_directories(folder, ec);
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
  };
  page.settings.push_back(std::move(open));
  return page;
}

}  // namespace skylanders
