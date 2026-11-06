#include "portal_rex/movie_skip.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <sstream>

#include <rex/logging.h>

#include "portal_rex/portal_menu_input.h"

// Start by default: it does nothing else while a movie plays, and A is easily pressed by accident
// on the way into one (a dialog's last line).
REXCVAR_DEFINE_STRING(movie_skip_buttons, "start", "Game",
                      "Controller buttons that skip the movie that is playing, comma-separated "
                      "(start, a, b, x, y, back); none for off");

namespace skylanders {
namespace {

std::mutex g_mutex;
uint32_t g_movie = 0;       // the HBINK seen last
uint32_t g_last_frame = 0;  // its frame then
uint32_t g_skipped = 0;     // the movie whose Frames was set, and its own Frames
uint32_t g_skipped_frames = 0;

uint32_t Load(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}

void Store(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24);
  p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);
  p[3] = uint8_t(v);
}

// The setting's buttons as X_INPUT_GAMEPAD bits, parsed again only when it changes.
uint16_t SkipMask() {
  static std::string cached_value = "\x01";
  static uint16_t cached = 0;
  const std::string value = REXCVAR_GET(movie_skip_buttons);
  if (value == cached_value) return cached;
  uint16_t mask = 0;
  std::stringstream list(value);
  std::string name;
  while (std::getline(list, name, ',')) {
    name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (name.empty() || name == "none" || name == "off") continue;
    const uint16_t bit = menu_input::ButtonByName(name);
    if (bit) {
      mask |= bit;
    } else {
      REXLOG_WARN("Movie skip: ignoring unknown button '{}'", name);
    }
  }
  cached_value = value;
  cached = mask;
  return mask;
}

}  // namespace

void MovieEndCheck(uint8_t* membase, uint32_t hbink) {
  if (!membase || !hbink) return;
  uint8_t* movie = membase + hbink;
  const uint32_t frame = Load(movie + 12);
  const uint32_t frames = Load(movie + 8);
  // Pressed since the last frame of any movie; taken every frame so that a press from before
  // this movie (or from the game) doesn't skip it at its start.
  const uint16_t pressed = menu_input::TakeNewPresses();
  std::lock_guard lock(g_mutex);
  const bool fresh = hbink != g_movie || frame < g_last_frame;
  if (fresh && g_skipped == hbink && frame < g_last_frame) {
    // A movie the game loops (a menu background) started over: its own length again.
    Store(movie + 8, g_skipped_frames);
  }
  if (fresh) g_skipped = 0;
  g_movie = hbink;
  g_last_frame = frame;
  if (fresh || !(pressed & SkipMask()) || menu_input::MenuOpen() || frame >= frames) return;
  g_skipped = hbink;
  g_skipped_frames = frames;
  Store(movie + 8, frame);
  REXLOG_INFO("Movie: skipped at frame {} of {}", frame, frames);
}

Setting MovieSkipSetting() {
  Setting s{"movie_skip_buttons", "Skip movies with a button", "Filme per Taste \xC3\xBC" "berspringen",
            "Ends the movie that is playing when you press this button, as if it had played out. "
            "Scenes the game draws itself are not movies and play as usual.",
            "Beendet den laufenden Film, wenn du diese Taste dr\xC3\xBC" "ckst, als w\xC3\xA4re er zu Ende "
            "gelaufen. Szenen, die das Spiel selbst zeichnet, sind keine Filme und laufen wie immer.",
            {{"none", "Off", "Aus"},
             {"start", "Start", "Start"},
             {"a", "A", "A"},
             {"start,a", "Start or A", "Start oder A"}}};
  return s;
}

}  // namespace skylanders
