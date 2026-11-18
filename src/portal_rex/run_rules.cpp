#include "portal_rex/run_rules.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rex/logging.h>

#include "dev/where.h"
#include "portal/figure_info.h"
#include "portal/run_logic.h"

REXCVAR_DEFINE_BOOL(run_soul_gems, false, "Portal",
                    "Soul Gem run: the portal menu puts on only the starters and the Skylanders freed in this run "
                    "(Soul Gems found, levels finished), one run for all six games (soul_gem_run.txt in the "
                    "figures folder)");
REXCVAR_DEFINE_INT32(run_team_size, 0, "Portal",
                     "In a level, each player puts on at most this many different Skylanders through "
                     "the portal menu (0: no limit; the hubs don't count)")
    .range(0, 6);
REXCVAR_DECLARE(std::string, portal_menu_games);
REXCVAR_DECLARE(std::string, portal_figures_dir);

namespace skylanders::run {
namespace {

namespace logic = portal::run;
using Clock = std::chrono::steady_clock;

std::mutex g_mutex;  // the teams, the story levels and the run state below
logic::Teams g_teams;              // this level's teams
std::vector<std::string> g_story;  // this game's story levels

// The run file as last read or written, looked at again once a second at most (the cards ask
// every frame; another game may have written it before this one started).
struct RunFile {
  std::filesystem::path path;
  logic::RunState state;
  std::filesystem::file_time_type time{};
  Clock::time_point checked{};
  bool read = false;
};
RunFile g_file;

// This game: the last of the portal menu's games (each game lists the ones before it and itself);
// "" when the list is empty, and then every game's starters count (run_logic.h).
std::string ThisGame() {
  const std::vector<std::string> games = logic::SplitList(REXCVAR_GET(portal_menu_games));
  return games.empty() ? std::string() : games.back();
}

std::filesystem::path RunFilePath() {
  const std::string dir = REXCVAR_GET(portal_figures_dir);
  if (dir.empty()) return {};
  return std::filesystem::path(std::u8string(dir.begin(), dir.end())) / "soul_gem_run.txt";
}

// g_file up to date (g_mutex held).
const logic::RunState& State() {
  const std::filesystem::path path = RunFilePath();
  const Clock::time_point now = Clock::now();
  if (g_file.read && path == g_file.path && now - g_file.checked < std::chrono::seconds(1)) return g_file.state;
  g_file.checked = now;
  std::error_code ec;
  const auto time = path.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(path, ec);
  if (g_file.read && path == g_file.path && (ec ? std::filesystem::file_time_type{} : time) == g_file.time) {
    return g_file.state;
  }
  g_file.path = path;
  g_file.time = ec ? std::filesystem::file_time_type{} : time;
  g_file.read = true;
  g_file.state = {};
  if (!ec && !path.empty()) {
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    g_file.state = logic::ParseRunState(text);
  }
  return g_file.state;
}

// Writes `state` as the run file, through a temporary file (g_mutex held). Kept for the session
// even when there's no folder or the write fails.
void Write(const logic::RunState& state) {
  g_file.state = state;
  g_file.read = true;
  g_file.path = RunFilePath();
  if (g_file.path.empty()) return;
  std::error_code ec;
  const std::filesystem::path temp = std::filesystem::path(g_file.path) += ".new";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << logic::FormatRunState(state);
    if (!out) {
      REXLOG_WARN("Run: could not write the run file next to the figures");
      return;
    }
  }
  std::filesystem::rename(temp, g_file.path, ec);
  if (ec) REXLOG_WARN("Run: could not keep the run file ({})", ec.message());
  g_file.time = std::filesystem::last_write_time(g_file.path, ec);
  g_file.checked = Clock::now();
}

// The cards ask for every figure in every frame: each figure's character key once (empty for one
// the run doesn't lock), and the keys the run lets on (starters, freed) again only when the run's
// list or portal_menu_games changed.
struct Cache {
  std::unordered_map<const portal::SkylanderInfo*, std::string> key_of;
  std::string open, games;
  bool built = false;
  std::unordered_set<std::string> open_keys;
};
Cache g_cache;  // UI thread

const std::string& KeyOf(const portal::SkylanderInfo& sky) {
  auto it = g_cache.key_of.find(&sky);
  if (it == g_cache.key_of.end()) {
    const std::string_view character = portal::CharacterName(sky);
    it = g_cache.key_of.emplace(&sky, logic::IsRunCharacter(sky) ? logic::NameKey(character) : std::string()).first;
  }
  return it->second;
}

void RefreshOpen(const std::string& open) {
  std::string games = REXCVAR_GET(portal_menu_games);
  if (g_cache.built && open == g_cache.open && games == g_cache.games) return;
  g_cache.open_keys.clear();
  for (const std::string_view name : logic::StartersOf(ThisGame())) g_cache.open_keys.insert(logic::NameKey(name));
  for (const std::string& name : logic::SplitList(open)) g_cache.open_keys.insert(logic::NameKey(name));
  g_cache.open = open;
  g_cache.games = std::move(games);
  g_cache.built = true;
}

}  // namespace

void SetStoryLevels(std::vector<std::string> levels) {
  std::lock_guard lock(g_mutex);
  g_story = std::move(levels);
}

bool Locked(const portal::SkylanderInfo& sky) {
  if (!REXCVAR_GET(run_soul_gems)) return false;
  const std::string& key = KeyOf(sky);
  if (key.empty()) return false;
  std::string open;
  {
    std::lock_guard lock(g_mutex);
    open = State().open;
  }
  RefreshOpen(open);
  return !g_cache.open_keys.contains(key);
}

bool LockedMessage(const portal::SkylanderInfo& sky, bool german, std::string& why) {
  if (!Locked(sky)) return false;
  const std::string name(portal::CharacterName(sky));
  why = german ? fmt::format("{} ist in diesem Durchlauf noch gesperrt: Finde den Seelenstein oder schlie\xC3\x9F"
                             "e Level ab.",
                             name)
               : fmt::format("{} is locked in this run: find the Soul Gem, or finish levels.", name);
  return true;
}

bool InPlay() { return !logic::IsHubLevel(dev::CurrentLevel()); }

bool KeepsTiredOut() { return REXCVAR_GET(run_team_size) > 0 || REXCVAR_GET(run_soul_gems); }

bool TeamRefuses(uint32_t user, const portal::SkylanderInfo& lead, bool german, std::string& why) {
  const int32_t size = REXCVAR_GET(run_team_size);
  if (size <= 0) return false;
  const std::string key = logic::NameKey(portal::CharacterName(lead));
  std::lock_guard lock(g_mutex);
  g_teams.Follow(dev::CurrentLevel());
  if (!g_teams.Refuses(user, key, size_t(size))) return false;
  why = german ? fmt::format("Dein Team f\xC3\xBCr dieses Level ist voll ({}): Spiel einen von ihnen.", size)
               : fmt::format("Your team for this level is full ({}): play one of them.", size);
  return true;
}

void Placed(uint32_t user, const portal::SkylanderInfo& lead) {
  const int32_t size = REXCVAR_GET(run_team_size);
  if (size <= 0) return;
  const std::string key = logic::NameKey(portal::CharacterName(lead));
  std::lock_guard lock(g_mutex);
  g_teams.Follow(dev::CurrentLevel());
  if (g_teams.Add(user, key)) {
    REXLOG_INFO("Run: player {}'s team in {}: {} of {}", user + 1, g_teams.level(), g_teams.Count(user), size);
  }
}

bool SoulGemFound(std::string_view skylander, bool from_save) {
  if (!REXCVAR_GET(run_soul_gems) || logic::NameKey(skylander).empty()) return false;
  // A movie in a hub can be one watched again; a gem's bit in the save comes only from a pickup (Swap
  // Force has two gems in its hub, Woodburrow).
  if (const std::string level = dev::CurrentLevel(); !from_save && logic::IsHubLevel(level)) {
    REXLOG_INFO("Run: {}'s Soul Gem in {} not counted (a hub, the gems are found in levels)", skylander,
                level.empty() ? std::string("no level yet") : level);
    return false;
  }
  std::lock_guard lock(g_mutex);
  logic::RunState state = State();
  if (logic::IsStarter(ThisGame(), skylander) || logic::ListHas(state.open, skylander)) return false;
  state.open = logic::ListAdd(state.open, skylander);
  Write(state);
  REXLOG_INFO("Run: {}'s Soul Gem found, {} free in this run", skylander, logic::SplitList(state.open).size());
  return true;
}

std::vector<std::string> LevelFinished(std::string_view level) {
  std::vector<std::string> freed;
  if (!REXCVAR_GET(run_soul_gems)) return freed;
  const std::string game = ThisGame();
  std::lock_guard lock(g_mutex);
  logic::RunState state = State();
  if (game.empty() || !logic::FirstFinish(game, level, state.done)) return freed;
  // The ones still locked, spread over the story levels not done yet (this one counted).
  const std::vector<std::string> candidates = logic::RewardCandidates(game);
  size_t left = 0;
  for (const std::string& name : candidates) left += logic::ListHas(state.open, name) ? 0 : 1;
  size_t levels_left = 0;
  for (const std::string& story : g_story) {
    levels_left += logic::ListHas(state.done, logic::LevelKey(game, story)) ? 0 : 1;
  }
  const size_t count = g_story.empty() ? std::min<size_t>(left, 1) : logic::RewardCount(left, std::max<size_t>(levels_left, 1));
  std::random_device random;
  for (size_t i = 0; i < count; ++i) {
    const std::string name = logic::PickReward(candidates, state.open, random());
    if (name.empty()) break;
    state.open = logic::ListAdd(state.open, name);
    freed.push_back(name);
  }
  state.done = logic::ListAdd(state.done, logic::LevelKey(game, level));
  Write(state);
  REXLOG_INFO("Run: {} finished for the first time: {} freed ({} still locked, {} story levels to go)", level,
              freed.size(), left - freed.size(), levels_left > 0 ? levels_left - 1 : 0);
  return freed;
}

void NewRun() {
  std::lock_guard lock(g_mutex);
  Write({});
  REXLOG_INFO("Run: a new run for all six games, only the starters ({})",
              ThisGame().empty() ? "every game's" : ThisGame());
}

size_t FoundCount() {
  std::lock_guard lock(g_mutex);
  return logic::SplitList(State().open).size();
}

}  // namespace skylanders::run
