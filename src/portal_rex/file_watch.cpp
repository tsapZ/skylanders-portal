#include "portal_rex/file_watch.h"

#include <array>
#include <atomic>

#include <rex/filesystem/vfs.h>
#include <rex/logging.h>

namespace skylanders {
namespace {

std::array<std::atomic<FileWatcher>, 4> g_watchers{};

void OnOpened(std::string_view path) {
  for (auto& slot : g_watchers) {
    if (const FileWatcher watcher = slot.load(std::memory_order_acquire)) watcher(path);
  }
}

}  // namespace

void AddFileWatcher(FileWatcher watcher) {
  if (!watcher) return;
  for (auto& slot : g_watchers) {
    FileWatcher current = slot.load();
    if (current == watcher) return;
    if (!current && slot.compare_exchange_strong(current, watcher)) {
      rex::filesystem::SetOpenObserver(&OnOpened);
      return;
    }
  }
  REXLOG_WARN("File watch: no room for another watcher");
}

}  // namespace skylanders
