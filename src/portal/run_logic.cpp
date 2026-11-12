#include "portal/run_logic.h"

#include <algorithm>
#include <array>
#include <cctype>

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
