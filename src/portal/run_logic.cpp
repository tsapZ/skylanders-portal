#include "portal/run_logic.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <utility>

#include "portal/figure_catalog.h"
#include "portal/figure_info.h"

namespace skylanders::portal::run {
namespace {

// Each game's starter pack.
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

}  // namespace

std::string NameKey(std::string_view name) {
  std::string key;
  for (const char c : name) {
    const auto u = static_cast<unsigned char>(c);
    if (std::isalnum(u)) key.push_back(static_cast<char>(std::tolower(u)));
  }
  return key;
}

bool IsHubLevel(std::string_view level) {
  const std::string key = NameKey(level);
  return key.empty() || key.find("hub") != std::string::npos || key.starts_with("townstate") ||
         key == "academy" || key.find("overland") != std::string::npos;
}

std::vector<std::string_view> StartersOf(std::string_view game) {
  std::vector<std::string_view> out;
  for (const Starters& s : kStarters) {
    // No game known (portal_menu_games empty): every game's starters, so a run never starts with none.
    if (!game.empty() && s.game != game) continue;
    for (const std::string_view name : s.names) {
      if (!name.empty()) out.push_back(name);
    }
  }
  return out;
}

bool IsStarter(std::string_view game, std::string_view character) {
  const std::string key = NameKey(character);
  if (key.empty()) return false;
  for (const std::string_view name : StartersOf(game)) {
    if (NameKey(name) == key) return true;
  }
  return false;
}

std::vector<std::string> SplitList(std::string_view list) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= list.size()) {
    const size_t end = list.find(';', start);
    const std::string_view part = list.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    if (!part.empty()) out.emplace_back(part);
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return out;
}

bool ListHas(std::string_view list, std::string_view name) {
  const std::string key = NameKey(name);
  if (key.empty()) return false;
  for (const std::string& item : SplitList(list)) {
    if (NameKey(item) == key) return true;
  }
  return false;
}

std::string ListAdd(std::string_view list, std::string_view name) {
  std::string out(list);
  if (!out.empty()) out += ';';
  out += name;
  return out;
}

bool FirstFinish(std::string_view game, std::string_view level, std::string_view done) {
  return !IsHubLevel(level) && !ListHas(done, LevelKey(game, level));
}

std::string PickReward(const std::vector<std::string>& candidates, std::string_view open, uint32_t random) {
  std::vector<const std::string*> left;
  for (const std::string& name : candidates) {
    if (NameKey(name).empty() || ListHas(open, name)) continue;
    // Each character once, also when the list has it twice (two spellings of one name).
    if (std::any_of(left.begin(), left.end(), [&](const std::string* n) { return NameKey(*n) == NameKey(name); })) {
      continue;
    }
    left.push_back(&name);
  }
  return left.empty() ? std::string() : *left[random % left.size()];
}

size_t RewardCount(size_t left, size_t levels_left) {
  if (levels_left == 0) return left;
  return (left + levels_left - 1) / levels_left;
}

RunState ParseRunState(std::string_view text) {
  RunState state;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.starts_with("open=")) state.open = std::string(line.substr(5));
    if (line.starts_with("done=")) state.done = std::string(line.substr(5));
    start = end + 1;
  }
  return state;
}

std::string FormatRunState(const RunState& state) {
  return "open=" + state.open + "\ndone=" + state.done + "\n";
}

std::string LevelKey(std::string_view game, std::string_view level) {
  return std::string(game) + "/" + std::string(level);
}

namespace {

constexpr std::string_view kGameOrder[] = {"Spyro's Adventure", "Giants",        "Swap Force",
                                           "Trap Team",         "SuperChargers", "Imaginators"};

int GameRank(std::string_view game) {
  for (size_t i = 0; i < std::size(kGameOrder); ++i) {
    if (kGameOrder[i] == game) return int(i);
  }
  return -1;
}

// The catalog's kinds that are the run's Skylanders: the characters and their other versions.
bool IsRunCategory(std::string_view category) {
  for (const std::string_view c : {"Cores", "Giants", "Swappers", "Trap Masters", "SuperChargers", "Senseis",
                                   "Villain Senseis", "LightCores", "Eon's Elite", "Variants", "Chase Variants",
                                   "Sensei Variants", "Villain Variants"}) {
    if (c == category) return true;
  }
  return false;
}

// Each run character's first game (its earliest entry's), by NameKey; built once. Left out: the
// sidekicks, Minis and vehicles (some come back as a later game's variants: Gnarly Barkley, a gold
// Tomb Buggy), and the two Nintendo-only SuperChargers, which the Xbox 360 games don't take.
const std::vector<std::pair<std::string, int>>& HomeGames() {
  static const std::vector<std::pair<std::string, int>> homes = [] {
    std::vector<std::string> not_run = {NameKey("Hammer Slam Bowser"), NameKey("Turbo Charge Donkey Kong")};
    for (const SkylanderInfo& sky : AllSkylanders()) {
      for (const std::string_view kind : {"Sidekicks", "Minis", "Vehicles", "Vehicle Variants"}) {
        if (sky.category == kind) not_run.push_back(NameKey(CharacterName(sky)));
      }
    }
    std::vector<std::pair<std::string, int>> out;
    for (const SkylanderInfo& sky : AllSkylanders()) {
      if (!IsRunCategory(sky.category)) continue;
      const std::string key = NameKey(CharacterName(sky));
      const int rank = GameRank(sky.game);
      if (key.empty() || rank < 0 || std::find(not_run.begin(), not_run.end(), key) != not_run.end()) continue;
      auto it = std::find_if(out.begin(), out.end(), [&](const auto& h) { return h.first == key; });
      if (it == out.end()) out.emplace_back(key, rank);
      else it->second = std::min(it->second, rank);
    }
    return out;
  }();
  return homes;
}

int HomeRank(std::string_view character) {
  const std::string key = NameKey(character);
  for (const auto& [k, rank] : HomeGames()) {
    if (k == key) return rank;
  }
  return -1;
}

}  // namespace

bool HasSoulGem(std::string_view game, std::string_view character) {
  const int rank = GameRank(game);
  if (rank < 0 || HomeRank(character) != rank) return false;
  if (rank == 0) {
    const std::string key = NameKey(character);
    for (const std::string_view none : {"bash", "zap", "drobot", "boomer", "darkspyro"}) {
      if (key == none) return false;
    }
  }
  return true;
}

bool IsRunCharacter(const SkylanderInfo& sky) {
  return IsRunCategory(sky.category) && HomeRank(CharacterName(sky)) >= 0;
}

std::vector<std::string> RewardCandidates(std::string_view game) {
  std::vector<std::string> out;
  const int rank = GameRank(game);
  if (rank < 0) return out;
  for (const SkylanderInfo& sky : AllSkylanders()) {
    if (!IsRunCategory(sky.category)) continue;
    const std::string_view name = CharacterName(sky);
    const int home = HomeRank(name);
    if (name.empty() || home < 0 || home > rank || HasSoulGem(game, name) || IsStarter(game, name)) continue;
    const std::string key = NameKey(name);
    if (std::none_of(out.begin(), out.end(), [&](const std::string& n) { return NameKey(n) == key; })) {
      out.emplace_back(name);
    }
  }
  return out;
}

void Teams::Follow(std::string_view level) {
  if (followed_ && level == level_) return;
  followed_ = true;
  level_ = level;
  for (auto& team : teams_) team.clear();
}

bool Teams::InPlay() const { return !IsHubLevel(level_); }

bool Teams::Refuses(uint32_t user, std::string_view key, size_t size) const {
  if (size == 0 || user >= kPlayers || key.empty() || !InPlay()) return false;
  const std::vector<std::string>& team = teams_[user];
  if (std::find(team.begin(), team.end(), key) != team.end()) return false;
  return team.size() >= size;
}

bool Teams::Add(uint32_t user, std::string_view key) {
  if (user >= kPlayers || key.empty() || !InPlay()) return false;
  std::vector<std::string>& team = teams_[user];
  if (std::find(team.begin(), team.end(), key) != team.end()) return false;
  team.emplace_back(key);
  return true;
}

size_t Teams::Count(uint32_t user) const { return user < kPlayers ? teams_[user].size() : 0; }

}  // namespace skylanders::portal::run
