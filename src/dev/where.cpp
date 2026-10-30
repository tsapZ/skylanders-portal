#include "dev/where.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <rex/filesystem/vfs.h>
#include <rex/logging.h>

namespace skylanders::dev {
namespace {

using Clock = std::chrono::steady_clock;

// Between two lines while the player stays in one level, and how far the player must have moved.
constexpr auto kEvery = std::chrono::minutes(5);
constexpr float kMoved = 2.0f;
// A level that has no player this long (a menu, a level that never gets one) is logged without.
constexpr auto kNoPlayer = std::chrono::seconds(20);
// A player counts a second after it came: its first frames can stand at a placeholder
// (Imaginators' is at 0 0 -50000 until the level puts it in place).
constexpr auto kSettle = std::chrono::seconds(1);

// The place as text, for other threads (the crash report, the problem note): two buffers, the
// reporter writes the one not in use and then switches, so a reader never sees half a line.
char g_text[2][160];
std::atomic<int> g_current{-1};

// The reporter's own (one thread).
std::string g_level;
bool g_level_logged = false;
bool g_logged_player = false;  // the level's last line had a player
Clock::time_point g_level_since, g_last_line;
float g_line_x = 0, g_line_y = 0, g_line_z = 0;

// The level alone, for CurrentLevel on other threads (written when it changes).
std::mutex g_level_mutex;
std::string g_level_shared;

// The level archives (WatchLevelArchives): the game's name for a path, the last one opened.
std::string (*g_name_of)(std::string_view) = nullptr;
std::mutex g_archive_mutex;
std::string g_archive_level;

void OnOpened(std::string_view path) {
  if (!g_name_of) return;
  std::string name = g_name_of(path);
  if (name.empty()) return;
  std::lock_guard lock(g_archive_mutex);
  g_archive_level = std::move(name);
}

}  // namespace

void ReportPlace(const std::string& level, bool has_player, float x, float y, float z) {
  const Clock::time_point now = Clock::now();
  if (level != g_level) {
    g_level = level;
    g_level_logged = false;
    g_level_since = now;
    // Without ReportPlayerFrame's notes ("(level unknown)", " (on something; ...)").
    std::lock_guard lock(g_level_mutex);
    g_level_shared = level.substr(0, level.find('('));
    while (!g_level_shared.empty() && g_level_shared.back() == ' ') g_level_shared.pop_back();
  }
  const int next = g_current.load(std::memory_order_relaxed) == 0 ? 1 : 0;
  if (has_player) {
    std::snprintf(g_text[next], sizeof(g_text[next]), "%s at %.2f %.2f %.2f", level.c_str(), x, y, z);
  } else {
    std::snprintf(g_text[next], sizeof(g_text[next]), "%s (no player)", level.c_str());
  }
  g_current.store(next, std::memory_order_release);
  bool line = false;
  if (!g_level_logged) {
    line = has_player || now - g_level_since >= kNoPlayer;
  } else if (has_player && !g_logged_player) {
    line = true;  // the player came after the line without one (a Skylander put on the portal late)
  } else if (has_player && now - g_last_line >= kEvery) {
    line = std::fabs(x - g_line_x) + std::fabs(y - g_line_y) + std::fabs(z - g_line_z) >= kMoved;
  }
  if (line) {
    REXLOG_INFO("Where: {}", g_text[next]);
    g_level_logged = true;
    g_logged_player = has_player;
    g_last_line = now;
    g_line_x = x;
    g_line_y = y;
    g_line_z = z;
  }
}

std::string PlaceText() {
  const int current = g_current.load(std::memory_order_acquire);
  return current < 0 ? std::string() : std::string(g_text[current]);
}

std::string CurrentLevel() {
  std::lock_guard lock(g_level_mutex);
  return g_level_shared;
}

void WatchLevelArchives(std::string (*name_of)(std::string_view path)) {
  g_name_of = name_of;
  rex::filesystem::SetOpenObserver(&OnOpened);
}

std::string ArchiveLevel() {
  std::lock_guard lock(g_archive_mutex);
  return g_archive_level;
}

void ReportPlayerFrame(uint32_t who, float x, float y, float z, bool relative) {
  // The players seen lately, in the order they came (a player back after two seconds away, as in
  // a new level, comes anew). One thread, the game's.
  struct Seen {
    uint32_t who = 0;
    Clock::time_point first{}, last{};
  };
  static Seen seen[4];
  const Clock::time_point now = Clock::now();
  Seen* mine = nullptr;
  for (Seen& s : seen) {
    if (s.who == who) mine = &s;
  }
  if (!mine) {
    mine = &seen[0];
    for (Seen& s : seen) {
      if (s.last < mine->last) mine = &s;
    }
    *mine = {who, now, now};
  }
  if (now - mine->last > std::chrono::seconds(2)) mine->first = now;
  mine->last = now;
  if (now - mine->first < kSettle) return;
  for (const Seen& s : seen) {
    if (&s != mine && s.who && now - s.last < std::chrono::milliseconds(500) && s.first < mine->first) return;
  }
  std::string level = ArchiveLevel();
  if (level.empty()) level = "(level unknown)";
  if (relative) level += " (on something; the position is relative to it)";
  ReportPlace(level, true, x, y, z);
}

void LogPlace(const char* why) {
  const std::string text = PlaceText();
  if (!text.empty()) REXLOG_INFO("Where ({}): {}", why, text);
}

}  // namespace skylanders::dev
