#include "portal_rex/run_rules.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>
#include <vector>

#include <rex/logging.h>

#include "dev/where.h"
#include "portal/figure_info.h"
#include "portal_rex/settings_file.h"

REXCVAR_DEFINE_BOOL(run_soul_gems, false, "Portal",
                    "Soul Gem run: the portal menu puts on only the game's starter Skylanders and "
                    "those whose Soul Gem was found in this run (run_found)");
REXCVAR_DEFINE_INT32(run_team_size, 0, "Portal",
                     "In a level, each player puts on at most this many different Skylanders through "
                     "the portal menu (0: no limit; the hubs don't count)")
    .range(0, 6);
REXCVAR_DEFINE_STRING(run_found, "", "Portal",
                      "The Skylanders whose Soul Gem was found in this Soul Gem run, separated by "
                      "';' (the portal menu writes it; empty: a new run)");
REXCVAR_DECLARE(std::string, portal_menu_games);

namespace skylanders::run {
namespace {

// Each game's starter pack: the Skylanders it came with (a Soul Gem run starts with them).
struct Starters {
  std::string_view game;
  std::array<std::string_view, 3> names;
};
constexpr Starters kStarters[] = {
    {"Spyro's Adventure", {"Spyro", "Gill Grunt", "Trigger Happy"}},
    {"Giants", {"Tree Rex", "Jet-Vac", "Cynder"}},
    {"Swap Force", {"Wash Buckler", "Blast Zone", "Stealth Elf"}},
    {"Trap Team", {"Snap Shot", "Food Fight", ""}},
    {"SuperChargers", {"Spitfire", "Hot Streak", ""}},
    {"Imaginators", {"King Pen", "Golden Queen", ""}},
};

constexpr size_t kPlayers = 4;

std::mutex g_mutex;
std::filesystem::path g_config_path;
std::string g_team_level;               // the level the teams belong to
std::vector<std::string> g_team[kPlayers];  // character keys, per player

// A name's letters and digits in lower case: "Jet-Vac" -> "jetvac".
std::string Key(std::string_view name) {
  std::string key;
  for (const char c : name) {
    const auto u = static_cast<unsigned char>(c);
    if (std::isalnum(u)) key.push_back(static_cast<char>(std::tolower(u)));
  }
  return key;
}

// The character a figure belongs to ("Chop Chop" for "Legendary Chop Chop"); empty for items and
// figures the catalog doesn't know.
std::string_view CharacterOf(const portal::SkylanderInfo& sky) { return portal::CharacterName(sky); }

std::vector<std::string> Split(const std::string& list) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= list.size()) {
    const size_t end = list.find(';', start);
    std::string part = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!part.empty()) out.push_back(std::move(part));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return out;
}

// This game: the last of the portal menu's games (each game lists the ones before it and itself).
std::string ThisGame() {
  const std::vector<std::string> games = Split(REXCVAR_GET(portal_menu_games));
  return games.empty() ? std::string() : games.back();
}

bool IsStarter(const std::string& key) {
  const std::string game = ThisGame();
  for (const Starters& s : kStarters) {
    if (s.game != game) continue;
    for (const std::string_view name : s.names) {
      if (!name.empty() && Key(name) == key) return true;
    }
  }
  return false;
}

bool IsFound(const std::string& key) {
  for (const std::string& name : Split(REXCVAR_GET(run_found))) {
    if (Key(name) == key) return true;
  }
  return false;
}

// The hubs, as dev/where.h names them: Level_Hub (Spyro's Adventure), Level_Hub_2_Stage_n
// (Giants), town_stateN (Swap Force's Woodburrow), Level_Hub_2014_01 (Trap Team), Academy
// (SuperChargers), Hub_2016 and Overland_Map (Imaginators).
bool IsHub(const std::string& level) {
  const std::string key = Key(level);
  return key.empty() || key.find("hub") != std::string::npos || key.rfind("townstate", 0) == 0 ||
         key == "academy" || key.find("overland") != std::string::npos;
}

// The teams belong to the level they were made in.
void FollowLevel() {
  const std::string level = dev::CurrentLevel();
  if (level == g_team_level) return;
  g_team_level = level;
  for (auto& team : g_team) team.clear();
}

}  // namespace

void SetConfigPath(std::filesystem::path path) {
  std::lock_guard lock(g_mutex);
  g_config_path = std::move(path);
}

bool Locked(const portal::SkylanderInfo& sky) {
  if (!REXCVAR_GET(run_soul_gems)) return false;
  const std::string key = Key(CharacterOf(sky));
  return !key.empty() && !IsStarter(key) && !IsFound(key);
}

bool Refuses(uint32_t user, const portal::SkylanderInfo& sky, bool german, std::string& why) {
  const std::string name(CharacterOf(sky));
  if (Locked(sky)) {
    why = german ? fmt::format("{} ist in diesem Durchlauf noch gesperrt: Finde zuerst den Seelenstein.", name)
                 : fmt::format("{} is locked in this run: find the Soul Gem first.", name);
    return true;
  }
  const int32_t size = REXCVAR_GET(run_team_size);
  const std::string key = Key(name);
  if (size <= 0 || key.empty() || user >= kPlayers) return false;
  std::lock_guard lock(g_mutex);
  FollowLevel();
  if (IsHub(g_team_level)) return false;
  const std::vector<std::string>& team = g_team[user];
  if (team.size() < size_t(size) || std::find(team.begin(), team.end(), key) != team.end()) return false;
  why = german ? fmt::format("Dein Team f\xC3\xBCr dieses Level ist voll ({}): Spiel einen von ihnen.", size)
               : fmt::format("Your team for this level is full ({}): play one of them.", size);
  return true;
}

void Placed(uint32_t user, const portal::SkylanderInfo& sky) {
  const int32_t size = REXCVAR_GET(run_team_size);
  const std::string key = Key(CharacterOf(sky));
  if (size <= 0 || key.empty() || user >= kPlayers) return;
  std::lock_guard lock(g_mutex);
  FollowLevel();
  if (IsHub(g_team_level)) return;
  std::vector<std::string>& team = g_team[user];
  if (std::find(team.begin(), team.end(), key) != team.end()) return;
  team.push_back(key);
  REXLOG_INFO("Run: player {}'s team in {}: {} of {}", user + 1, g_team_level, team.size(), size);
}

bool SoulGemFound(std::string_view skylander) {
  if (!REXCVAR_GET(run_soul_gems) || skylander.empty()) return false;
  const std::string key = Key(skylander);
  if (key.empty() || IsStarter(key) || IsFound(key)) return false;
  std::string list = REXCVAR_GET(run_found);
  list += (list.empty() ? "" : ";") + std::string(skylander);
  std::filesystem::path path;
  {
    std::lock_guard lock(g_mutex);
    path = g_config_path;
  }
  if (!StoreSetting(path, "run_found", list)) REXLOG_WARN("Run: could not keep the Soul Gem of {}", skylander);
  REXLOG_INFO("Run: {}'s Soul Gem found, {} found in this run", skylander, FoundCount());
  return true;
}

void NewRun() {
  std::filesystem::path path;
  {
    std::lock_guard lock(g_mutex);
    path = g_config_path;
  }
  StoreSetting(path, "run_found", "");
  REXLOG_INFO("Run: a new run, only the starters ({})", ThisGame());
}

size_t FoundCount() { return Split(REXCVAR_GET(run_found)).size(); }

}  // namespace skylanders::run
