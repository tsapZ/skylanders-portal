#include "portal_rex/soul_gems.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <unordered_map>

#include <rex/logging.h>

#include "portal/figure_catalog.h"
#include "portal_rex/file_watch.h"

namespace skylanders {
namespace {

using SkylanderOf = std::string (*)(std::string_view);

std::atomic<SkylanderOf> g_skylander_of{nullptr};
std::mutex g_mutex;
std::vector<std::string> g_seen;

// A name's letters and digits in lower case: "Ka-Boom" -> "kaboom".
std::string Key(std::string_view name) {
  std::string key;
  for (const char c : name) {
    const auto u = static_cast<unsigned char>(c);
    if (std::isalnum(u)) key.push_back(static_cast<char>(std::tolower(u)));
  }
  return key;
}

bool SameNoCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

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
}

}  // namespace

void WatchSoulGems(std::string (*skylander_of)(std::string_view path)) {
  g_skylander_of.store(skylander_of, std::memory_order_release);
  AddFileWatcher(&OnOpened);
}

std::vector<std::string> SoulGemsSeen() {
  std::lock_guard lock(g_mutex);
  return g_seen;
}

std::string_view FileStem(std::string_view path, std::string_view extension) {
  const size_t cut = path.find_last_of("\\/:");
  const std::string_view file = cut == std::string_view::npos ? path : path.substr(cut + 1);
  if (file.size() <= extension.size() || !SameNoCase(file.substr(file.size() - extension.size()), extension)) {
    return {};
  }
  return file.substr(0, file.size() - extension.size());
}

bool SameName(std::string_view a, std::string_view b) { return SameNoCase(a, b); }

bool InFolder(std::string_view path, std::string_view folder) {
  const size_t file = path.find_last_of("\\/");
  if (file == std::string_view::npos) return false;
  const std::string_view dir = path.substr(0, file);
  const size_t cut = dir.find_last_of("\\/:");
  return SameNoCase(cut == std::string_view::npos ? dir : dir.substr(cut + 1), folder);
}

std::string_view AfterPrefix(std::string_view text, std::string_view prefix) {
  if (text.size() <= prefix.size() || !SameNoCase(text.substr(0, prefix.size()), prefix)) return {};
  return text.substr(prefix.size());
}

std::string SkylanderNamed(std::string_view name) {
  // The catalog is in release order, so the first entry of a name stays (Giants' Bash is
  // Spyro's Adventure's).
  static const std::unordered_map<std::string, std::string_view> by_key = [] {
    std::unordered_map<std::string, std::string_view> map;
    for (const auto& s : portal::AllSkylanders()) map.emplace(Key(s.name), s.name);
    return map;
  }();
  const auto it = by_key.find(Key(name));
  return it == by_key.end() ? std::string() : std::string(it->second);
}

std::string SkylanderWithId(uint16_t id) {
  for (const auto& s : portal::AllSkylanders()) {
    if (s.id == id) return std::string(s.name);
  }
  return {};
}

}  // namespace skylanders
