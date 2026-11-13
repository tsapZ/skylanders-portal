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
// in any spelling: "Spyro's Adventure" Spyro, Gill Grunt, Trigger Happy. An empty `game` (not known)
// takes every game's starters.
bool IsStarter(std::string_view game, std::string_view character);

// The starter pack's names of `game` (every game's for an empty `game`).
std::vector<std::string_view> StartersOf(std::string_view game);

// A list of names separated by ';', empty parts left out.
std::vector<std::string> SplitList(std::string_view list);
// Whether the list has `name` (in any spelling).
bool ListHas(std::string_view list, std::string_view name);
// The list with `name` added at the end.
std::string ListAdd(std::string_view list, std::string_view name);

// A Soul Gem run as the owner described it (2026-11-13): the starters at first, a Soul Gem found
// brings its Skylander, and the Skylanders the game has no Soul Gem for come one per level
// finished, not always in the same order; a level played again brings nothing.

// Whether finishing `level` of `game` brings a Skylander: a level (not a hub) not in `done`, the
// levels finished so far in this run (RunState::done, LevelKey entries).
bool FirstFinish(std::string_view game, std::string_view level, std::string_view done);

// The Skylander a first finish brings: one of `candidates` (the game's Skylanders without a Soul
// Gem in it, no starters) that `open` (the run's list: the finds and the earlier rewards) doesn't
// have yet, chosen by `random` (any number: the game draws one, the tests fix it); "" when none is
// left.
std::string PickReward(const std::vector<std::string>& candidates, std::string_view open, uint32_t random);

// How many Skylanders a first finish brings: the `left` still locked spread over the `levels_left`
// story levels not finished yet (this one included), rounded up, so all are free by the last
// level ("Spread over the story", darkmattereli-hue on #71, 2026-11-13). No level left: all.
size_t RewardCount(size_t left, size_t levels_left);

// The run's state, one for all six games (the Skylanders freed in one game stay free in the next:
// darkmattereli-hue on #71), kept as text: "open=<names>" and "done=<game>/<level> ..." lines,
// each a ';' list; other lines are left out.
struct RunState {
  std::string open;  // the Skylanders freed so far: Soul Gems found and level rewards
  std::string done;  // the levels finished so far, "<game>/<level>"
};
RunState ParseRunState(std::string_view text);
std::string FormatRunState(const RunState& state);
// The key a finished level has in RunState::done.
std::string LevelKey(std::string_view game, std::string_view level);

// Soul Gems by game, from the discs' level archives (2026-11-13): each game has one for each
// Skylander it brought (Giants its 16, Trap Team its 36 with the starters, Swap Force its swappers
// and cores, SuperChargers its SuperChargers, Imaginators its Senseis), Spyro's Adventure for 28 of
// its 33 (Bash, Zap, Drobot, Boomer and Dark Spyro have none). `character` is a name as
// CharacterName gives it.
bool HasSoulGem(std::string_view game, std::string_view character);
// The Skylanders a first finish can bring in `game`: every character the game can use (the ones of
// this game and the games before it; no sidekicks, Minis, vehicles, traps, items or creation
// crystals) without a Soul Gem in it and not a starter, each once, in the catalog's order.
std::vector<std::string> RewardCandidates(std::string_view game);

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
