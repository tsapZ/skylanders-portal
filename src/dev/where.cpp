#include "dev/where.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <rex/logging.h>

namespace skylanders::dev {
namespace {

using Clock = std::chrono::steady_clock;

// Between two lines while the player stays in one level, and how far the player must have moved.
constexpr auto kEvery = std::chrono::minutes(5);
constexpr float kMoved = 2.0f;
// A level that has no player this long (a menu, a level that never gets one) is logged without.
constexpr auto kNoPlayer = std::chrono::seconds(20);

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

}  // namespace

void ReportPlace(const std::string& level, bool has_player, float x, float y, float z) {
  const Clock::time_point now = Clock::now();
  if (level != g_level) {
    g_level = level;
    g_level_logged = false;
    g_level_since = now;
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

void LogPlace(const char* why) {
  const std::string text = PlaceText();
  if (!text.empty()) REXLOG_INFO("Where ({}): {}", why, text);
}

}  // namespace skylanders::dev
