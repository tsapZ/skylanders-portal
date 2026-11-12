#pragma once
// The plain rules of a run (Core of Light #71, the Soul Gem run; #52, a team size per level), without
// the menu, the settings or ReXGlue, so they are unit-tested (tests/run_logic_test.cpp);
// portal_rex/run_rules.cpp puts them to work.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace skylanders::portal::run {

// A name's letters and digits in lower case: "Jet-Vac" -> "jetvac".
std::string NameKey(std::string_view name);

// Whether a level name as dev/where.h gives it is a hub, or unknown (empty): Level_Hub (Spyro's
// Adventure), Level_Hub_2_Stage_n (Giants), town_stateN (Swap Force's Woodburrow), Level_Hub_2014_01
// (Trap Team), Academy (SuperChargers), Hub_2016 and Overland_Map (Imaginators).
bool IsHubLevel(std::string_view level);

// Whether `character` belongs to `game`'s starter pack (the Skylanders the game came with), by name
// in any spelling: "Spyro's Adventure" Spyro, Gill Grunt, Trigger Happy.
bool IsStarter(std::string_view game, std::string_view character);

// A list of names separated by ';', empty parts left out.
std::vector<std::string> SplitList(std::string_view list);
// Whether the list has `name` (in any spelling).
bool ListHas(std::string_view list, std::string_view name);
// The list with `name` added at the end.
std::string ListAdd(std::string_view list, std::string_view name);

// Each player's team in the current level, as character keys (NameKey).
class Teams {
 public:
  static constexpr uint32_t kPlayers = 4;

  // The level now; another level than before starts every team empty.
  void Follow(std::string_view level);
  // Whether a team of `size` (0: no limit) is full for `key` joining `user`'s team: false for a key
  // already in it, in a hub and for a player number out of range.
  bool Refuses(uint32_t user, std::string_view key, size_t size) const;
  // Adds `key` to `user`'s team: true if it wasn't in it (never in a hub).
  bool Add(uint32_t user, std::string_view key);
  size_t Count(uint32_t user) const;
  const std::string& level() const { return level_; }

 private:
  bool InPlay() const;

  std::string level_;
  bool followed_ = false;
  std::vector<std::string> teams_[kPlayers];
};

}  // namespace skylanders::portal::run
