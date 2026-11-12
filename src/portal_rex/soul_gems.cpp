#include "portal_rex/soul_gems.h"

#include <algorithm>
#include <atomic>
#include <mutex>

#include <rex/logging.h>

#include "portal_rex/file_watch.h"

namespace skylanders {
namespace {

using SkylanderOf = std::string (*)(std::string_view);

std::atomic<SkylanderOf> g_skylander_of{nullptr};
std::atomic<void (*)(const std::string&)> g_found{nullptr};
std::mutex g_mutex;
std::vector<std::string> g_seen;

void OnOpened(std::string_view path) {
  const SkylanderOf skylander_of = g_skylander_of.load(std::memory_order_acquire);
  if (!skylander_of) return;
  std::string name = skylander_of(path);
  if (name.empty()) return;
  {
    // A movie can be opened more than once (its header, then the stream): one line per Skylander.
    std::lock_guard lock(g_mutex);
    if (std::find(g_seen.begin(), g_seen.end(), name) != g_seen.end()) return;
    g_seen.push_back(name);
  }
  REXLOG_INFO("Soul Gem movie: {} ({})", name, path);
  if (const auto found = g_found.load(std::memory_order_acquire)) found(name);
}

}  // namespace

void WatchSoulGems(std::string (*skylander_of)(std::string_view path)) {
  g_skylander_of.store(skylander_of, std::memory_order_release);
  AddFileWatcher(&OnOpened);
}

void OnSoulGem(void (*found)(const std::string& skylander)) { g_found.store(found, std::memory_order_release); }

std::vector<std::string> SoulGemsSeen() {
  std::lock_guard lock(g_mutex);
  return g_seen;
}

}  // namespace skylanders
