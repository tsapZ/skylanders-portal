#include "portal_rex/soul_gems.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <thread>

#include <rex/logging.h>

#include "portal_rex/file_watch.h"

namespace skylanders {
namespace {

using SkylanderOf = std::string (*)(std::string_view);

std::atomic<SkylanderOf> g_skylander_of{nullptr};
std::atomic<void (*)(const std::string&, bool)> g_found{nullptr};
std::mutex g_mutex;
std::vector<std::string> g_seen;

// A Skylander's Soul Gem, from the movie or from a save: one log line per Skylander, the callback every
// time (a new run, or a run switched on later in the same session, must see a gem picked up again; the
// run counts each find once itself).
void Found(const std::string& name, std::string_view source) {
  bool first = false;
  {
    std::lock_guard lock(g_mutex);
    if (std::find(g_seen.begin(), g_seen.end(), name) == g_seen.end()) {
      g_seen.push_back(name);
      first = true;
    }
  }
  if (first) REXLOG_INFO("Soul Gem {}: {}", source, name);
  if (const auto found = g_found.load(std::memory_order_acquire)) found(name, source == "save");
}

void OnOpened(std::string_view path) {
  const SkylanderOf skylander_of = g_skylander_of.load(std::memory_order_acquire);
  if (!skylander_of) return;
  std::string name = skylander_of(path);
  if (name.empty()) return;
  // A movie can be opened more than once (its header, then the stream): Found keeps one line.
  Found(name, "movie (" + std::string(path) + ")");
}

std::vector<uint8_t> ReadAll(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

void WatchSoulGems(std::string (*skylander_of)(std::string_view path)) {
  g_skylander_of.store(skylander_of, std::memory_order_release);
  AddFileWatcher(&OnOpened);
}

void WatchSoulGemSaves(std::vector<std::filesystem::path> (*slots)(),
                       std::vector<std::string> (*finds)(std::span<const uint8_t> before,
                                                         std::span<const uint8_t> after)) {
  if (!slots || !finds) return;
  std::thread([slots, finds] {
    struct Slot {
      std::filesystem::file_time_type time{};     // of the bytes in `data`
      std::filesystem::file_time_type changed{};  // a newer time seen last tick, read when it holds
      bool pending = false;
      std::vector<uint8_t> data;
    };
    std::map<std::filesystem::path, Slot> known;
    bool first = true;
    for (;;) {
      for (const auto& path : slots()) {
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(path, ec);
        if (ec) continue;
        auto it = known.find(path);
        if (it == known.end()) {
          // At the start: what is there is the baseline. A slot that appears later is a new game:
          // compared with nothing.
          Slot s;
          if (first) {
            s.time = time;
            s.data = ReadAll(path);
          } else {
            s.pending = true;
            s.changed = time;
          }
          known.emplace(path, std::move(s));
          continue;
        }
        Slot& s = it->second;
        if (time == s.time) continue;
        if (!s.pending || time != s.changed) {  // still being written: wait a tick
          s.pending = true;
          s.changed = time;
          continue;
        }
        std::vector<uint8_t> now = ReadAll(path);
        if (now.empty()) continue;
        for (const std::string& name : finds(s.data, now)) Found(name, "save");
        s.data = std::move(now);
        s.time = time;
        s.pending = false;
      }
      first = false;
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }).detach();
}

void OnSoulGem(void (*found)(const std::string& skylander, bool from_save)) {
  g_found.store(found, std::memory_order_release);
}

std::vector<std::string> SoulGemsSeen() {
  std::lock_guard lock(g_mutex);
  return g_seen;
}

}  // namespace skylanders
