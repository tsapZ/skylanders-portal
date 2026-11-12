#pragma once
// Rules for a run, off by default (Core of Light #71 and #52; the owner's go and answers of
// 2026-11-12). The plain logic is portal/run_logic.h (unit-tested); this adds the settings, the
// current level and the messages.
// - Soul Gem run (run_soul_gems): the portal menu puts on only the game's starter Skylanders and
//   those whose Soul Gem was found in this run (soul_gems.h tells), a character with all its
//   versions. Companions (sidekicks, traps, SuperChargers' vehicles) aren't locked. The finds are
//   kept in the game's settings file (run_found), so a run goes on after a restart; the portal
//   menu's options start a new run. A Soul Gem movie opened in a hub doesn't count: the gems are
//   found in levels, and a hub could show such a movie for other reasons.
// - Team size (run_team_size, 0 = off, 1 to 6): in a level, each player puts on at most that many
//   different Skylanders (a placement is one member, a swapper's two halves too); the teams start
//   empty when the level changes (dev/where.h), and the hubs don't count.
// Not yet: Skylanders the game has no Soul Gem for joining by finished levels, real toys on a real
// portal (they reach the game without the menu), a run per save slot, a pool per player in co-op.
// UI thread, but SetConfigPath.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <rex/cvar.h>

#include "portal/figure_catalog.h"

REXCVAR_DECLARE(bool, run_soul_gems);
REXCVAR_DECLARE(int32_t, run_team_size);
REXCVAR_DECLARE(std::string, run_found);

namespace skylanders::run {

// Where the finds are written (the game's settings file; empty: kept for the session only).
void SetConfigPath(std::filesystem::path path);

// Whether a figure is locked in this Soul Gem run (not a starter, its Soul Gem not found yet).
// Items and figures the catalog can't name a character for are never locked. Cheap enough for
// every card in every frame.
bool Locked(const portal::SkylanderInfo& sky);

// Locked, with the player's message (English or German as the menu).
bool LockedMessage(const portal::SkylanderInfo& sky, bool german, std::string& why);

// Whether the player is in a level now (not a hub, not before the first level).
bool InPlay();

// Whether `user`'s team for this level is full for a placement led by `lead` (its first figure);
// `why` gets the message.
bool TeamRefuses(uint32_t user, const portal::SkylanderInfo& lead, bool german, std::string& why);

// A placement led by `lead` went on for `user`: one member of this level's team.
void Placed(uint32_t user, const portal::SkylanderInfo& lead);

// A Skylander's Soul Gem movie opened (its catalog or game name): true if that is a new find for the
// run (not in a hub). Writes the settings file.
bool SoulGemFound(std::string_view skylander);

// Starts the run over: only the starters. Writes the settings file.
void NewRun();

// The run's finds (starters not counted).
size_t FoundCount();

}  // namespace skylanders::run
