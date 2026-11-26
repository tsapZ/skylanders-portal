#pragma once

// A game whose disc can be another build ships that build's exe next to its own (XexCheckResult::sibling_exe,
// "sibling=<exe>@<pin>" in its disc list; 2026-11-26: SuperChargers' French / Dutch and Scandinavian discs, Core of
// Light #135, #118). The game's start check, finding such a disc, runs that exe instead: the same command line, the
// same working folder, and it ends with that exe's exit code, so Core of Light, a shortcut or a script that started
// the game sees one game run.

#include <optional>
#include <string_view>

namespace skylanders::xex {

// Starts `exe_name` from this exe's folder with this process's arguments and waits for it. Windows: returns its exit
// code (the started game is in a job object that closes with this process, so ending this one ends it too); nullopt
// when the file isn't there, is this exe itself, or doesn't start. Linux: replaces this process (execv) and only
// returns, nullopt, when that fails (or for the same reasons).
std::optional<int> RunSiblingBuild(std::string_view exe_name);

}  // namespace skylanders::xex
