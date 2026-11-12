#include "portal_rex/run_rules.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rex/logging.h>

#include "dev/where.h"
#include "portal/figure_info.h"
#include "portal/run_logic.h"
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

namespace logic = portal::run;

std::mutex g_mutex;
std::filesystem::path g_config_path;
logic::Teams g_teams;  // this level's teams

// This game: the last of the portal menu's games (each game lists the ones before it and itself);
// "" when the list is empty, and then every game's starters count (run_logic.h).
std::string ThisGame() {
  const std::vector<std::string> games = logic::SplitList(REXCVAR_GET(portal_menu_games));
  return games.empty() ? std::string() : games.back();
}

// The cards ask for every figure in every frame: each figure's character key once, and the keys the
// run lets on (starters, finds) again only when run_found or portal_menu_games changed.
struct Cache {
  std::unordered_map<const portal::SkylanderInfo*, std::string> key_of;
  std::string found, games;
  bool built = false;
  std::unordered_set<std::string> open_keys;  // the starters' and the finds' character keys
};
Cache g_cache;  // UI thread

const std::string& KeyOf(const portal::SkylanderInfo& sky) {
  auto it = g_cache.key_of.find(&sky);
  if (it == g_cache.key_of.end()) it = g_cache.key_of.emplace(&sky, logic::NameKey(portal::CharacterName(sky))).first;
  return it->second;
}

void RefreshOpen() {
  std::string found = REXCVAR_GET(run_found);
  std::string games = REXCVAR_GET(portal_menu_games);
  if (g_cache.built && found == g_cache.found && games == g_cache.games) return;
  g_cache.open_keys.clear();
  for (const std::string_view name : logic::StartersOf(ThisGame())) g_cache.open_keys.insert(logic::NameKey(name));
  for (const std::string& name : logic::SplitList(found)) g_cache.open_keys.insert(logic::NameKey(name));
  g_cache.found = std::move(found);
  g_cache.games = std::move(games);
  g_cache.built = true;
}

}  // namespace

void SetConfigPath(std::filesystem::path path) {
  std::lock_guard lock(g_mutex);
  g_config_path = std::move(path);
}

bool Locked(const portal::SkylanderInfo& sky) {
  if (!REXCVAR_GET(run_soul_gems)) return false;
  const std::string& key = KeyOf(sky);
  if (key.empty()) return false;
  RefreshOpen();
  return !g_cache.open_keys.contains(key);
}

bool LockedMessage(const portal::SkylanderInfo& sky, bool german, std::string& why) {
  if (!Locked(sky)) return false;
  const std::string name(portal::CharacterName(sky));
  why = german ? fmt::format("{} ist in diesem Durchlauf noch gesperrt: Finde zuerst den Seelenstein.", name)
               : fmt::format("{} is locked in this run: find the Soul Gem first.", name);
  return true;
}

bool InPlay() { return !logic::IsHubLevel(dev::CurrentLevel()); }

bool TeamRefuses(uint32_t user, const portal::SkylanderInfo& lead, bool german, std::string& why) {
  const int32_t size = REXCVAR_GET(run_team_size);
  if (size <= 0) return false;
  std::lock_guard lock(g_mutex);
  g_teams.Follow(dev::CurrentLevel());
  if (!g_teams.Refuses(user, KeyOf(lead), size_t(size))) return false;
  why = german ? fmt::format("Dein Team f\xC3\xBCr dieses Level ist voll ({}): Spiel einen von ihnen.", size)
               : fmt::format("Your team for this level is full ({}): play one of them.", size);
  return true;
}

void Placed(uint32_t user, const portal::SkylanderInfo& lead) {
  const int32_t size = REXCVAR_GET(run_team_size);
  if (size <= 0) return;
  std::lock_guard lock(g_mutex);
  g_teams.Follow(dev::CurrentLevel());
  if (g_teams.Add(user, KeyOf(lead))) {
    REXLOG_INFO("Run: player {}'s team in {}: {} of {}", user + 1, g_teams.level(), g_teams.Count(user), size);
  }
}

bool SoulGemFound(std::string_view skylander) {
  if (!REXCVAR_GET(run_soul_gems) || logic::NameKey(skylander).empty()) return false;
  const std::string found = REXCVAR_GET(run_found);
  if (logic::IsStarter(ThisGame(), skylander) || logic::ListHas(found, skylander)) return false;
  if (const std::string level = dev::CurrentLevel(); logic::IsHubLevel(level)) {
    REXLOG_INFO("Run: {}'s Soul Gem movie in {} not counted (a hub, the gems are found in levels)", skylander,
                level.empty() ? std::string("no level yet") : level);
    return false;
  }
  std::filesystem::path path;
  {
    std::lock_guard lock(g_mutex);
    path = g_config_path;
  }
  if (!StoreSetting(path, "run_found", logic::ListAdd(found, skylander))) {
    REXLOG_WARN("Run: could not keep the Soul Gem of {}", skylander);
  }
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
  REXLOG_INFO("Run: a new run, only the starters ({})", ThisGame().empty() ? "every game's" : ThisGame());
}

size_t FoundCount() { return logic::SplitList(REXCVAR_GET(run_found)).size(); }

}  // namespace skylanders::run
