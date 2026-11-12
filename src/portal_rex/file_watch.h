#pragma once
// Every file the game opens, for more than one watcher: the SDK's open observer
// (rex::filesystem::SetOpenObserver) has a single slot, so the watchers here share it (the level
// archives of dev/where.h, the Soul Gem movies of soul_gems.h). A watcher runs on the thread that
// opened the file, for every file: keep it short, no system calls.

#include <string_view>

namespace skylanders {

using FileWatcher = void (*)(std::string_view path);

// Adds a watcher; at most four, set before the game runs (the same one twice counts once).
void AddFileWatcher(FileWatcher watcher);

}  // namespace skylanders
