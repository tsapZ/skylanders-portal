#pragma once
// Rules for a run, off by default (Core of Light #71 and #52). The plain logic is
// portal/run_logic.h (unit-tested); this adds the settings, the current level, the run file and the
// messages.
// - Soul Gem run (run_soul_gems), as the owner described it (2026-11-13) and the idea's author
//   settled on #71: the game's starters at first; a Soul Gem found frees its Skylander; the
//   Skylanders the game has no Soul Gem for come with the levels finished for the first time,
//   spread over the story (all free by the last story level) and picked at random; a level played
//   again brings nothing. One run for all six games: soul_gem_run.txt in the figures folder they
//   share keeps who is free and which levels are done, so the Skylanders freed in one game stay
//   free in the next. Sidekicks, Minis, traps, vehicles and items aren't locked. A Soul Gem found
//   in a hub doesn't count (the gems are in levels; a hub could show a Soul Gem's movie for other
//   reasons).
// - Team size (run_team_size, 0 = off, 1 to 6): in a level, each player puts on at most that many
//   different Skylanders (a placement is one member, a swapper's two halves too); the teams start
//   empty when the level changes (dev/where.h), and the hubs don't count.
// Not yet: real toys on a real portal (they reach the game without the menu), a pool per player in
// co-op. UI thread, but SetStoryLevels.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>

#include "portal/figure_catalog.h"

REXCVAR_DECLARE(bool, run_soul_gems);
REXCVAR_DECLARE(int32_t, run_team_size);

namespace skylanders::run {

// This game's story levels as dev/where.h names them (from the game's own level table), set once
// before the game runs: a first finish spreads the locked Skylanders over the ones not done yet.
// Without them, a first finish frees one.
void SetStoryLevels(std::vector<std::string> levels);

// Whether a figure is locked in this Soul Gem run (a Skylander that is neither a starter nor freed
// yet). Items, companions and figures the catalog can't name a character for are never locked.
// Cheap enough for every card in every frame (the run file is looked at once a second at most).
bool Locked(const portal::SkylanderInfo& sky);

// Locked, with the player's message (English or German as the menu).
bool LockedMessage(const portal::SkylanderInfo& sky, bool german, std::string& why);

// Whether the player is in a level now (not a hub, not before the first level).
bool InPlay();

// Whether a Skylander out of health stays off the portal until the level ends: with a team size per
// level, and in a Soul Gem run (the owner's yes, 2026-11-13, after the idea's video on #71: a
// knocked-out Skylander rests until it can be summoned again).
bool KeepsTiredOut();

// Whether `user`'s team for this level is full for a placement led by `lead` (its first figure);
// `why` gets the message.
bool TeamRefuses(uint32_t user, const portal::SkylanderInfo& lead, bool german, std::string& why);

// A placement led by `lead` went on for `user`: one member of this level's team.
void Placed(uint32_t user, const portal::SkylanderInfo& lead);

// A Skylander's Soul Gem was found (its catalog or game name): true if that frees it now (not a
// starter, not free yet, not in a hub). Writes the run file.
bool SoulGemFound(std::string_view skylander);

// A level was finished (the game's own signal): the Skylanders its first finish in this run frees,
// picked at random; empty for a hub, a level finished before, or when none is left. Writes the run
// file.
std::vector<std::string> LevelFinished(std::string_view level);

// Starts the run over, for all six games: only the starters. Writes the run file.
void NewRun();

// The Skylanders freed in this run so far (starters not counted).
size_t FoundCount();

}  // namespace skylanders::run
