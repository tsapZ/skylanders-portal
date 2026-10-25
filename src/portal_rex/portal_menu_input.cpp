#include "portal_rex/portal_menu_input.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rex/input/device_assignment.h>
#include <rex/input/flags.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/input/nop/nop_input_driver.h>
#include <rex/input/sdl/sdl_input_driver.h>
#if REX_PLATFORM_WIN32
#include <rex/input/xinput/xinput_input_driver.h>
#endif
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(portal_menu_open_buttons, "guide,back+start,hold_back", "Portal",
                      "Controller buttons that open the portal menu, comma-separated: a button "
                      "(guide, back, start, a, b, x, y, lb, rb, ls, rs, up, down, left, right), a "
                      "chord joined with + (back+start), or hold_<button> to hold it");
REXCVAR_DEFINE_STRING(portal_item_off_buttons, "back+y", "Portal",
                      "Controller buttons that take the item (magic item, adventure pack) off the "
                      "portal without opening the menu; same format as portal_menu_open_buttons, "
                      "empty for none");
REXCVAR_DEFINE_STRING(player_menu_buttons, "ls+rs", "Input",
                      "Controller buttons that open the player menu (which controller plays as "
                      "which player); same format as portal_menu_open_buttons, empty for none");
REXCVAR_DEFINE_STRING(settings_menu_buttons, "back+rb", "Input",
                      "Controller buttons that open the settings menu; same format as "
                      "portal_menu_open_buttons, empty for none");
// A controller button doing another button's job (F4 > Controller; SwapForceRecomp #71).
REXCVAR_DEFINE_STRING(pad_map_a, "", "Input",
                      "What the A button does in the game: a, b, x, y, lb, rb, lt, rt, ls, rs, or off; empty: what it says on it");
REXCVAR_DEFINE_STRING(pad_map_b, "", "Input",
                      "What the B button does in the game: a, b, x, y, lb, rb, lt, rt, ls, rs, or off; empty: what it says on it");
REXCVAR_DEFINE_STRING(pad_map_x, "", "Input",
                      "What the X button does in the game: a, b, x, y, lb, rb, lt, rt, ls, rs, or off; empty: what it says on it");
REXCVAR_DEFINE_STRING(pad_map_y, "", "Input",
                      "What the Y button does in the game: a, b, x, y, lb, rb, lt, rt, ls, rs, or off; empty: what it says on it");
REXCVAR_DEFINE_STRING(pad_map_lb, "", "Input",
                      "What the left shoulder button does in the game: a, b, x, y, lb, rb, lt, rt, ls, rs, or off; empty: what it says on it");
REXCVAR_DEFINE_STRING(pad_map_rb, "", "Input",
                      "What the right shoulder button does in the game: a, b, x, y, lb, rb, lt, rt, ls, rs, or off; empty: what it says on it");
REXCVAR_DEFINE_STRING(pad_map_lt, "", "Input",
                      "What the left trigger does in the game: a, b, x, y, lb, rb, lt, rt, ls, rs, or off; empty: what it says on it");
REXCVAR_DEFINE_STRING(pad_map_rt, "", "Input",
                      "What the right trigger does in the game: a, b, x, y, lb, rb, lt, rt, ls, rs, or off; empty: what it says on it");
REXCVAR_DEFINE_STRING(pad_map_ls, "", "Input",
                      "What the left stick press does in the game: a, b, x, y, lb, rb, lt, rt, ls, rs, or off; empty: what it says on it");
REXCVAR_DEFINE_STRING(pad_map_rs, "", "Input",
                      "What the right stick press does in the game: a, b, x, y, lb, rb, lt, rt, ls, rs, or off; empty: what it says on it");
// How strongly each player's controller vibrates (F4 > Controller; SwapForceRecomp #73).
REXCVAR_DEFINE_INT32(pad_rumble_p1, 100, "Input", "How strongly player 1's controller vibrates, 0-100 %")
    .range(0, 100);
REXCVAR_DEFINE_INT32(pad_rumble_p2, 100, "Input", "How strongly player 2's controller vibrates, 0-100 %")
    .range(0, 100);
REXCVAR_DEFINE_INT32(portal_menu_hold_ms, 600, "Portal",
                     "How long a hold_<button> entry of the *_buttons settings must be held")
    .range(100, 5000);
REXCVAR_DEFINE_INT32(portal_menu_chord_ms, 150, "Portal",
                     "How long the first button of an open chord is kept from the game while "
                     "waiting for the others")
    .range(0, 1000);
REXCVAR_DEFINE_INT32(keyboard_player, 1, "Input",
                     "Which player the keyboard plays as (1-2, the games have two); 2 tries co-op "
                     "with one controller")
    .range(1, 2);

namespace skylanders {
namespace {

using rex::X_RESULT;
using rex::X_STATUS;
using rex::input::DeviceId;
using rex::input::DeviceInfo;
using rex::input::InputDriver;
using rex::input::X_INPUT_GAMEPAD;
using rex::input::X_INPUT_STATE;
using skylanders::menu_input::ButtonAction;
using Clock = std::chrono::steady_clock;

constexpr size_t kActionCount = 4;

class MenuInputDriver;

struct DeviceRecord {
  MenuInputDriver* driver = nullptr;
  uint32_t ordinal = 0;
  bool synthetic = false;
  bool keyboard = false;  // the keyboard/mouse emulation, plays as keyboard_player
  std::string name;
  std::optional<uint32_t> forced_user;  // set by SetDevicePlayer
  // Open-button tracking.
  uint16_t last_buttons = 0;
  bool chord_fired = false;  // an open chord fired and its buttons are still down
  Clock::time_point hold_since{};
  uint16_t hold_buttons = 0;
  // Buttons that may start an open entry are kept from the game for a moment (see GuardTimes).
  uint16_t withheld = 0;   // down, not shown to the game yet
  uint16_t swallowed = 0;  // part of an entry that fired; hidden until released
  uint16_t replay = 0;     // released while withheld; shown to the game as a short tap
  Clock::time_point replay_until{};
  std::array<Clock::time_point, 16> pressed_at{};
  // After the menu closes, the device stays hidden until all its buttons are up.
  bool hidden_until_release = false;
  // A press the menu makes on the game's behalf (TapButton).
  uint16_t tap = 0;
  Clock::time_point tap_from{}, tap_until{};
};

std::mutex g_mutex;
std::unordered_map<uint64_t, DeviceRecord> g_devices;  // by DeviceId
std::array<std::function<void(DeviceId, uint32_t)>, kActionCount> g_handlers;
std::atomic<bool> g_menu_open{false};
// The controller that pressed a button last (a DeviceId), for the hints' button pictures.
std::atomic<uint64_t> g_last_pad{0};
std::function<void()> g_menu_closer;  // UI thread only
std::atomic<bool> g_menu_closable{false};
std::optional<DeviceId> g_keyboard;
std::vector<DeviceId> g_connected;  // the devices the input system has now

struct OpenEntry {
  uint16_t buttons = 0;
  bool hold = false;
  ButtonAction action = ButtonAction::kPortalMenu;
};

uint16_t ButtonBit(const std::string& name) {
  static const std::pair<const char*, uint16_t> kButtons[] = {
      {"guide", rex::input::X_INPUT_GAMEPAD_GUIDE},
      {"back", rex::input::X_INPUT_GAMEPAD_BACK},
      {"start", rex::input::X_INPUT_GAMEPAD_START},
      {"a", rex::input::X_INPUT_GAMEPAD_A},
      {"b", rex::input::X_INPUT_GAMEPAD_B},
      {"x", rex::input::X_INPUT_GAMEPAD_X},
      {"y", rex::input::X_INPUT_GAMEPAD_Y},
      {"lb", rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER},
      {"rb", rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER},
      {"ls", rex::input::X_INPUT_GAMEPAD_LEFT_THUMB},
      {"rs", rex::input::X_INPUT_GAMEPAD_RIGHT_THUMB},
      {"up", rex::input::X_INPUT_GAMEPAD_DPAD_UP},
      {"down", rex::input::X_INPUT_GAMEPAD_DPAD_DOWN},
      {"left", rex::input::X_INPUT_GAMEPAD_DPAD_LEFT},
      {"right", rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT},
  };
  for (const auto& [n, bit] : kButtons) {
    if (name == n) {
      return bit;
    }
  }
  return 0;
}

// Parses one *_buttons setting into `entries`. Unknown names are skipped with a warning.
void ParseEntries(const std::string& value, ButtonAction action, std::vector<OpenEntry>& entries) {
  std::stringstream list(value);
  std::string token;
  while (std::getline(list, token, ',')) {
    token.erase(std::remove(token.begin(), token.end(), ' '), token.end());
    std::transform(token.begin(), token.end(), token.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (token.empty()) {
      continue;
    }
    OpenEntry entry;
    entry.action = action;
    if (token.rfind("hold_", 0) == 0) {
      entry.hold = true;
      token = token.substr(5);
    }
    std::stringstream chord(token);
    std::string part;
    bool valid = true;
    while (std::getline(chord, part, '+')) {
      uint16_t bit = ButtonBit(part);
      valid = valid && bit != 0;
      entry.buttons |= bit;
    }
    if (valid && entry.buttons) {
      entries.push_back(entry);
    } else {
      REXLOG_WARN("Portal menu: ignoring unknown button entry '{}'", token);
    }
  }
}

// Every button entry: portal_menu_open_buttons, portal_item_off_buttons, player_menu_buttons,
// settings_menu_buttons. Parsed again only when one of them changes, so the warnings come once
// per value.
std::vector<OpenEntry> OpenEntries() {
  static std::optional<std::string> cached_value;
  static std::vector<OpenEntry> cached;
  const std::string open = REXCVAR_GET(portal_menu_open_buttons);
  const std::string item_off = REXCVAR_GET(portal_item_off_buttons);
  const std::string players = REXCVAR_GET(player_menu_buttons);
  const std::string settings = REXCVAR_GET(settings_menu_buttons);
  const std::string value = open + "|" + item_off + "|" + players + "|" + settings;
  if (value == cached_value) {
    return cached;
  }
  std::vector<OpenEntry> entries;
  ParseEntries(open, ButtonAction::kPortalMenu, entries);
  ParseEntries(item_off, ButtonAction::kItemOff, entries);
  ParseEntries(players, ButtonAction::kPlayerMenu, entries);
  ParseEntries(settings, ButtonAction::kSettingsMenu, entries);
  cached_value = value;
  cached = entries;
  return entries;
}

bool AnyButtonDown(const X_INPUT_GAMEPAD& pad) {
  return uint16_t(pad.buttons) != 0 || pad.left_trigger > 30 || pad.right_trigger > 30;
}

// Buttons the game plays with (A, B, X, Y, the shoulders, the d-pad). They are never kept back,
// so they never lag: a chord with one of them (back+y) fires when its other buttons are already
// down, and the gameplay button is hidden from that moment.
constexpr uint16_t kGameplayButtons =
    rex::input::X_INPUT_GAMEPAD_A | rex::input::X_INPUT_GAMEPAD_B | rex::input::X_INPUT_GAMEPAD_X |
    rex::input::X_INPUT_GAMEPAD_Y | rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER |
    rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER | rex::input::X_INPUT_GAMEPAD_DPAD_UP |
    rex::input::X_INPUT_GAMEPAD_DPAD_DOWN | rex::input::X_INPUT_GAMEPAD_DPAD_LEFT |
    rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT;

// How long each button is kept from the game after it goes down, because it may be the start of
// an entry: a chord's buttons for portal_menu_chord_ms, a hold entry's for the hold time. That way
// pressing Back to open the menu doesn't also open the game's pause menu. Released sooner, the
// game gets a short tap instead; still down afterwards, it sees the button from then on. Zero for
// buttons that start no such entry and for gameplay buttons.
std::array<Clock::duration, 16> GuardTimes() {
  std::array<Clock::duration, 16> times{};
  for (const OpenEntry& entry : OpenEntries()) {
    const bool chord = (entry.buttons & (entry.buttons - 1)) != 0;
    if (!entry.hold && !chord) {
      continue;
    }
    const Clock::duration time = std::chrono::milliseconds(
        entry.hold ? REXCVAR_GET(portal_menu_hold_ms) : REXCVAR_GET(portal_menu_chord_ms));
    for (int bit = 0; bit < 16; ++bit) {
      if ((entry.buttons & (1u << bit)) && !(kGameplayButtons & (1u << bit))) {
        times[bit] = std::max(times[bit], time);
      }
    }
  }
  return times;
}

// The buttons the game gets to see, given the raw ones and the previous raw ones. Called with
// g_mutex held, after ObserveLocked.
uint16_t VisibleButtonsLocked(DeviceRecord& record, uint16_t buttons, uint16_t previous,
                              Clock::time_point now) {
  const auto guard = GuardTimes();
  record.swallowed &= buttons;
  for (int bit = 0; bit < 16; ++bit) {
    const uint16_t mask = uint16_t(1u << bit);
    const bool down = buttons & mask;
    if (guard[bit] == Clock::duration::zero()) {
      record.withheld &= uint16_t(~mask);
    } else if (down && !(previous & mask)) {
      record.withheld |= mask;
      record.pressed_at[bit] = now;
    } else if (!down && (record.withheld & mask)) {
      record.withheld &= uint16_t(~mask);
      record.replay |= mask;
      record.replay_until = now + std::chrono::milliseconds(100);
    } else if (down && (record.withheld & mask) && now - record.pressed_at[bit] >= guard[bit]) {
      record.withheld &= uint16_t(~mask);
    }
  }
  if (now >= record.replay_until) {
    record.replay = 0;
  }
  return uint16_t((buttons & ~record.withheld & ~record.swallowed) | record.replay);
}

// Updates the button-entry state of one device from its latest raw state. Returns the action whose
// entry fired, with the entry's buttons in `fired_buttons`. Called with g_mutex held.
std::optional<ButtonAction> ObserveLocked(DeviceRecord& record, const X_INPUT_GAMEPAD& pad,
                                          uint16_t* fired_buttons) {
  const uint16_t buttons = pad.buttons;
  const auto now = Clock::now();
  std::optional<ButtonAction> fired;
  bool any_chord_down = false;
  for (const OpenEntry& entry : OpenEntries()) {
    const bool down = (buttons & entry.buttons) == entry.buttons;
    if (!down) {
      continue;
    }
    any_chord_down = true;
    if (record.chord_fired) {
      continue;
    }
    bool entry_fired = false;
    if (!entry.hold) {
      const bool was_down = (record.last_buttons & entry.buttons) == entry.buttons;
      entry_fired = !was_down;
    } else {
      if (record.hold_buttons != entry.buttons) {
        record.hold_buttons = entry.buttons;
        record.hold_since = now;
      } else if (now - record.hold_since >=
                 std::chrono::milliseconds(REXCVAR_GET(portal_menu_hold_ms))) {
        entry_fired = true;
      }
    }
    if (entry_fired && !fired) {
      fired = entry.action;
      *fired_buttons |= entry.buttons;
    }
  }
  if (!any_chord_down) {
    record.chord_fired = false;
    record.hold_buttons = 0;
  }
  if (fired) {
    record.chord_fired = true;
  }
  record.last_buttons = buttons;
  return fired;
}

// The buttons pad_map_* can give another job, and what each one stands for.
struct PadSource {
  const char* name;
  uint16_t bit;  // 0 for a trigger
  int trigger;   // 1 left, 2 right
};
constexpr PadSource kPadSources[] = {
    {"a", rex::input::X_INPUT_GAMEPAD_A, 0},
    {"b", rex::input::X_INPUT_GAMEPAD_B, 0},
    {"x", rex::input::X_INPUT_GAMEPAD_X, 0},
    {"y", rex::input::X_INPUT_GAMEPAD_Y, 0},
    {"lb", rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER, 0},
    {"rb", rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER, 0},
    {"lt", 0, 1},
    {"rt", 0, 2},
    {"ls", rex::input::X_INPUT_GAMEPAD_LEFT_THUMB, 0},
    {"rs", rex::input::X_INPUT_GAMEPAD_RIGHT_THUMB, 0},
};

const PadSource* FindPadSource(const std::string& name) {
  for (const PadSource& s : kPadSources) {
    if (name == s.name) return &s;
  }
  return nullptr;
}

// Gives the buttons the jobs pad_map_* says, in the state the game gets: each pressed button
// sends its mapped button instead of its own (so two can do one job, and "off" none). Untouched
// when nothing is mapped, so the triggers keep their exact values.
void RemapPad(X_INPUT_GAMEPAD& pad) {
  const PadSource* target[std::size(kPadSources)] = {};
  bool any = false;
  for (size_t i = 0; i < std::size(kPadSources); ++i) {
    std::string value = rex::cvar::GetFlagByName(std::string("pad_map_") + kPadSources[i].name);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    target[i] = &kPadSources[i];
    if (value.empty() || value == kPadSources[i].name) continue;
    any = true;
    target[i] = value == "off" ? nullptr : FindPadSource(value);
  }
  if (!any) return;
  uint16_t buttons = uint16_t(pad.buttons);
  uint8_t left = 0, right = 0;
  for (const PadSource& s : kPadSources) buttons = uint16_t(buttons & ~s.bit);
  for (size_t i = 0; i < std::size(kPadSources); ++i) {
    const PadSource& s = kPadSources[i];
    const uint8_t analog = s.trigger == 1 ? pad.left_trigger : s.trigger == 2 ? pad.right_trigger : 0;
    const bool down = s.trigger ? analog > 30 : (uint16_t(pad.buttons) & s.bit) != 0;
    if (!down || !target[i]) continue;
    const uint8_t strength = s.trigger ? analog : 255;  // a button on a trigger pulls it fully
    if (target[i]->trigger == 1) left = std::max(left, strength);
    else if (target[i]->trigger == 2) right = std::max(right, strength);
    else buttons = uint16_t(buttons | target[i]->bit);
  }
  pad.buttons = buttons;
  pad.left_trigger = left;
  pad.right_trigger = right;
}

uint32_t UserOfLocked(const DeviceRecord& record) {
  if (record.forced_user) {
    return *record.forced_user;
  }
  if (record.keyboard) {
    // The games have two players (player_menu.cpp's kPlayers).
    return uint32_t(std::clamp(REXCVAR_GET(keyboard_player), 1, 2) - 1);
  }
  if (record.synthetic) {
    return 0;
  }
  return record.ordinal < rex::input::kMaxGuestUsers ? record.ordinal : 0;
}

class MenuInputDriver final : public InputDriver {
 public:
  // `keyboard`: the inner driver is the keyboard/mouse emulation, whose device plays as player 0.
  MenuInputDriver(std::unique_ptr<InputDriver> inner, bool pollable, bool keyboard = false)
      : InputDriver(nullptr, 0), inner_(std::move(inner)), pollable_(pollable), keyboard_(keyboard) {}

  // The inner driver was set up by whoever created it.
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }

  void OnWindowAvailable(rex::ui::Window* window) override { inner_->OnWindowAvailable(window); }

  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    const size_t first = out.size();
    inner_->EnumerateDevices(out);
    std::lock_guard lock(g_mutex);
    devices_.clear();
    for (size_t i = first; i < out.size(); ++i) {
      DeviceRecord& record = g_devices[uint64_t(out[i].id)];
      record.driver = this;
      record.synthetic = out[i].synthetic;
      record.keyboard = keyboard_;
      devices_.push_back(out[i].id);
      if (keyboard_) {
        g_keyboard = out[i].id;
      }
    }
  }

  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override {
    X_INPUT_STATE state{};
    X_RESULT result = inner_->GetDeviceState(id, &state);
    if (result != X_ERROR_SUCCESS) {
      return result;
    }
    bool hide = !is_active() || g_menu_open.load();
    if (!keyboard_ && state.gamepad.buttons) g_last_pad.store(uint64_t(id));
    uint16_t visible = state.gamepad.buttons;
    if (Observe(id, state.gamepad, &hide, &visible)) {
      hide = true;
    }
    // The game only polls the players it knows about; a second player who hasn't joined yet
    // must still be able to open the menu (and join by placing a figure). So every game poll
    // also checks the open button on this driver's other controllers.
    WatchOtherDevices(id);
    state.gamepad.buttons =
        uint16_t(visible & uint16_t(~uint16_t(rex::input::X_INPUT_GAMEPAD_GUIDE)));
    // The jobs F4 > Controller gave the buttons; the keyboard has its own keys page.
    if (!keyboard_) RemapPad(state.gamepad);
    if (hide) {
      std::memset(&state.gamepad, 0, sizeof(state.gamepad));
    }
    if (!g_menu_open.load()) {
      std::lock_guard lock(g_mutex);
      DeviceRecord& record = g_devices[uint64_t(id)];
      const auto now = Clock::now();
      if (record.tap && now >= record.tap_until) {
        record.tap = 0;
      } else if (record.tap && now >= record.tap_from) {
        state.gamepad.buttons = uint16_t(uint16_t(state.gamepad.buttons) | record.tap);
      }
    }
    if (out_state) {
      *out_state = state;
    }
    return result;
  }

  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                 rex::input::X_INPUT_CAPABILITIES* out_caps) override {
    return inner_->GetDeviceCapabilities(id, flags, out_caps);
  }

  // The game's vibration, at the strength the player chose (pad_rumble_p*).
  X_RESULT SetDeviceVibration(DeviceId id, rex::input::X_INPUT_VIBRATION* vibration) override {
    if (!vibration) return inner_->SetDeviceVibration(id, vibration);
    uint32_t user = 0;
    {
      std::lock_guard lock(g_mutex);
      user = UserOfLocked(g_devices[uint64_t(id)]);
    }
    const int percent = std::clamp(user == 0 ? REXCVAR_GET(pad_rumble_p1) : REXCVAR_GET(pad_rumble_p2), 0, 100);
    rex::input::X_INPUT_VIBRATION scaled = *vibration;
    scaled.left_motor_speed = uint16_t(uint32_t(vibration->left_motor_speed) * uint32_t(percent) / 100);
    scaled.right_motor_speed = uint16_t(uint32_t(vibration->right_motor_speed) * uint32_t(percent) / 100);
    return inner_->SetDeviceVibration(id, &scaled);
  }

  // A vibration as given, without the player's strength (RumbleTest).
  X_RESULT SetVibrationAsIs(DeviceId id, rex::input::X_INPUT_VIBRATION* vibration) {
    return inner_->SetDeviceVibration(id, vibration);
  }

  bool keyboard() const { return keyboard_; }

  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags,
                              rex::input::X_INPUT_KEYSTROKE* out_keystroke) override {
    if (g_menu_open.load() || !is_active()) {
      return X_ERROR_EMPTY;
    }
    return inner_->GetDeviceKeystroke(id, flags, out_keystroke);
  }

 private:
  // Updates the button-entry state of a device; calls the action's handler and returns true when
  // an entry fired. `hide` is set while the device must stay hidden after the menu closed,
  // `visible` to the buttons the game may see.
  bool Observe(DeviceId id, const X_INPUT_GAMEPAD& pad, bool* hide, uint16_t* visible) {
    bool fire = false;
    uint32_t user = 0;
    std::function<void(DeviceId, uint32_t)> handler;
    {
      std::lock_guard lock(g_mutex);
      DeviceRecord& record = g_devices[uint64_t(id)];
      record.driver = this;
      const uint16_t previous = record.last_buttons;
      uint16_t fired_buttons = 0;
      const std::optional<ButtonAction> action = ObserveLocked(record, pad, &fired_buttons);
      // With a menu open only the settings menu's entry fires, and only over a menu that can
      // make way for it (SetMenuCloser).
      fire = action && (!g_menu_open.load() ||
                        (*action == ButtonAction::kSettingsMenu && g_menu_closable.load()));
      record.swallowed |= fired_buttons;
      record.withheld &= uint16_t(~fired_buttons);
      const uint16_t shown = VisibleButtonsLocked(record, pad.buttons, previous, Clock::now());
      if (visible) *visible = shown;
      if (record.hidden_until_release && hide) {
        if (AnyButtonDown(pad)) {
          *hide = true;
        } else {
          record.hidden_until_release = false;
        }
      }
      user = UserOfLocked(record);
      if (fire) handler = g_handlers[size_t(*action)];
    }
    if (fire && handler) {
      handler(id, user);
    }
    return fire;
  }

  void WatchOtherDevices(DeviceId polled) {
    if (!pollable_) {
      return;
    }
    const auto now = Clock::now();
    std::vector<DeviceId> others;
    {
      std::lock_guard lock(g_mutex);
      if (now - last_watch_ < std::chrono::milliseconds(15)) {
        return;
      }
      last_watch_ = now;
      for (DeviceId other : devices_) {
        if (other != polled) others.push_back(other);
      }
    }
    for (DeviceId other : others) {
      X_INPUT_STATE state{};
      if (inner_->GetDeviceState(other, &state) == X_ERROR_SUCCESS) {
        Observe(other, state.gamepad, nullptr, nullptr);
      }
    }
  }

 public:
  bool pollable() const { return pollable_; }

  std::optional<X_INPUT_GAMEPAD> Poll(DeviceId id) {
    if (!pollable_) {
      return std::nullopt;
    }
    X_INPUT_STATE state{};
    if (inner_->GetDeviceState(id, &state) != X_ERROR_SUCCESS) {
      return std::nullopt;
    }
    if (!keyboard_ && state.gamepad.buttons) g_last_pad.store(uint64_t(id));
    return state.gamepad;
  }

 private:
  std::unique_ptr<InputDriver> inner_;
  bool pollable_;
  bool keyboard_;
  std::vector<DeviceId> devices_;  // guarded by g_mutex
  Clock::time_point last_watch_{};  // guarded by g_mutex
};

// Like the SDK's SlotAssignment (a controller in connection slot N plays as player N, synthetic
// devices as player 1), except that the keyboard plays as keyboard_player. Keeps each device's
// slot for the menu.
class TrackingAssignment final : public rex::input::DeviceAssignment {
 public:
  void OnDevicesChanged(const std::vector<DeviceInfo>& devices) override {
    std::lock_guard lock(g_mutex);
    devices_.clear();
    g_connected.clear();
    for (const DeviceInfo& info : devices) {
      DeviceRecord& record = g_devices[uint64_t(info.id)];
      record.ordinal = info.ordinal;
      record.synthetic = info.synthetic;
      record.name = info.name;
      devices_.push_back(info.id);
      g_connected.push_back(info.id);
    }
  }

  void DevicesForUser(uint32_t user_index, std::vector<DeviceId>& out) const override {
    out.clear();
    std::lock_guard lock(g_mutex);
    for (DeviceId id : devices_) {
      auto it = g_devices.find(uint64_t(id));
      if (it == g_devices.end()) {
        continue;
      }
      const DeviceRecord& record = it->second;
      const bool in_slot = record.synthetic || record.forced_user ||
                           record.ordinal < rex::input::kMaxGuestUsers;
      if (in_slot && UserOfLocked(record) == user_index) {
        out.push_back(id);
      }
    }
  }

 private:
  std::vector<DeviceId> devices_;  // guarded by g_mutex
};

template <typename Driver>
void AddWrapped(rex::input::InputSystem& input, bool pollable, bool keyboard = false) {
  auto driver = std::make_unique<Driver>(nullptr, 0);
  if (driver->Setup() != X_STATUS_SUCCESS) {
    return;
  }
  input.AddDriver(std::make_unique<MenuInputDriver>(std::move(driver), pollable, keyboard));
}

}  // namespace

std::unique_ptr<rex::system::IInputSystem> CreatePortalMenuInputSystem(bool tool_mode) {
  auto input = std::make_unique<rex::input::InputSystem>(nullptr);
  if (!tool_mode) {
#if REX_PLATFORM_WIN32
    if (REXCVAR_GET(input_backend) == "xinput") {
      AddWrapped<rex::input::xinput::XinputInputDriver>(*input, true);
    }
#endif
    if (REXCVAR_GET(input_backend) == "sdl") {
      AddWrapped<rex::input::sdl::SDLInputDriver>(*input, true);
    }
    // Keyboard/mouse emulation: not pollable from the UI thread (it consumes mouse deltas); the
    // menu reads the keyboard through ImGui instead.
    AddWrapped<rex::input::mnk::MnkInputDriver>(*input, false, true);
  }
  uint8_t nop_index = tool_mode ? 0 : 1;
  input->AddDriver(std::make_unique<rex::input::nop::NopInputDriver>(nullptr, nop_index));
  input->SetDeviceAssignment(std::make_unique<TrackingAssignment>());
  return input;
}

std::unique_ptr<InputDriver> WrapForPortalMenu(std::unique_ptr<InputDriver> driver,
                                               bool pollable) {
  return std::make_unique<MenuInputDriver>(std::move(driver), pollable);
}

namespace menu_input {

void SetButtonHandler(ButtonAction action, std::function<void(DeviceId, uint32_t)> handler) {
  std::lock_guard lock(g_mutex);
  g_handlers[size_t(action)] = std::move(handler);
}

void SetOpenHandler(std::function<void(DeviceId, uint32_t)> handler) {
  SetButtonHandler(ButtonAction::kPortalMenu, std::move(handler));
}

void SetMenuOpen(bool open) {
  if (!open) {
    std::lock_guard lock(g_mutex);
    for (auto& [id, record] : g_devices) {
      record.hidden_until_release = true;
      // The closing press must not count as a new open press.
      record.chord_fired = true;
    }
    g_menu_closer = nullptr;
    g_menu_closable.store(false);
  }
  g_menu_open.store(open);
}

bool MenuOpen() { return g_menu_open.load(); }

void SetMenuCloser(std::function<void()> closer) {
  g_menu_closer = std::move(closer);
  g_menu_closable.store(bool(g_menu_closer));
}

bool CloseOpenMenu() {
  if (!g_menu_open.load() || !g_menu_closer) return false;
  const std::function<void()> closer = g_menu_closer;
  closer();
  return true;
}

std::optional<X_INPUT_GAMEPAD> ReadPad(DeviceId device) {
  MenuInputDriver* driver = nullptr;
  {
    std::lock_guard lock(g_mutex);
    auto it = g_devices.find(uint64_t(device));
    if (it == g_devices.end()) {
      return std::nullopt;
    }
    driver = it->second.driver;
  }
  return driver ? driver->Poll(device) : std::nullopt;
}

uint32_t UserOf(DeviceId device) {
  std::lock_guard lock(g_mutex);
  auto it = g_devices.find(uint64_t(device));
  return it == g_devices.end() ? 0 : UserOfLocked(it->second);
}

std::optional<DeviceId> KeyboardDevice() {
  std::lock_guard lock(g_mutex);
  return g_keyboard;
}

std::optional<PlayerDevice> DeviceForUser(uint32_t user) {
  std::lock_guard lock(g_mutex);
  std::optional<PlayerDevice> keyboard;
  for (DeviceId id : g_connected) {
    auto it = g_devices.find(uint64_t(id));
    if (it == g_devices.end() || !it->second.driver || UserOfLocked(it->second) != user) {
      continue;
    }
    if (it->second.keyboard) {
      keyboard = PlayerDevice{id, true};
    } else if (it->second.driver->pollable()) {
      return PlayerDevice{id, false};
    }
  }
  return keyboard;
}

void RumbleTest(uint32_t user, int percent) {
  // A short buzz at the strength just chosen, on that player's controllers.
  std::vector<std::pair<MenuInputDriver*, DeviceId>> targets;
  {
    std::lock_guard lock(g_mutex);
    for (auto& [key, record] : g_devices) {
      if (!record.driver || record.driver->keyboard() || UserOfLocked(record) != user) continue;
      targets.emplace_back(record.driver, DeviceId(key));
    }
  }
  if (targets.empty()) return;
  const uint16_t speed = uint16_t(65535u * uint32_t(std::clamp(percent, 0, 100)) / 100u);
  for (auto& [driver, id] : targets) {
    rex::input::X_INPUT_VIBRATION v{};
    v.left_motor_speed = speed;
    v.right_motor_speed = speed;
    driver->SetVibrationAsIs(id, &v);
  }
  std::thread([targets] {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    for (auto& [driver, id] : targets) {
      rex::input::X_INPUT_VIBRATION off{};
      driver->SetVibrationAsIs(id, &off);
    }
  }).detach();
}

std::vector<DeviceId> PadsOfUser(uint32_t user) {
  std::lock_guard lock(g_mutex);
  std::vector<DeviceId> pads;
  for (DeviceId id : g_connected) {
    auto it = g_devices.find(uint64_t(id));
    if (it != g_devices.end() && it->second.driver && !it->second.keyboard &&
        it->second.driver->pollable() && UserOfLocked(it->second) == user) {
      pads.push_back(id);
    }
  }
  return pads;
}

void SetDevicePlayer(DeviceId device, std::optional<uint32_t> user) {
  std::lock_guard lock(g_mutex);
  if (user && *user >= rex::input::kMaxGuestUsers) {
    return;
  }
  g_devices[uint64_t(device)].forced_user = user;
}

void TapButton(uint32_t user, uint16_t button) {
  std::lock_guard lock(g_mutex);
  const auto now = Clock::now();
  for (DeviceId id : g_connected) {
    auto it = g_devices.find(uint64_t(id));
    if (it == g_devices.end() || UserOfLocked(it->second) != user) {
      continue;
    }
    it->second.tap = button;
    it->second.tap_from = now + std::chrono::milliseconds(100);
    it->second.tap_until = now + std::chrono::milliseconds(260);
  }
}

uint16_t ButtonByName(const std::string& name) { return ButtonBit(name); }

bool IsActionChord(ButtonAction action, uint16_t buttons) {
  for (const OpenEntry& entry : OpenEntries()) {
    if (entry.action == action && (buttons & entry.buttons) == entry.buttons) {
      return true;
    }
  }
  return false;
}

bool IsOpenChord(uint16_t buttons) { return IsActionChord(ButtonAction::kPortalMenu, buttons); }

std::string LastPadName() {
  const uint64_t id = g_last_pad.load();
  if (!id) return {};
  std::lock_guard lock(g_mutex);
  auto it = g_devices.find(id);
  return it == g_devices.end() ? std::string() : it->second.name;
}

std::vector<ConnectedDevice> ConnectedDevices() {
  std::lock_guard lock(g_mutex);
  std::vector<ConnectedDevice> devices;
  for (DeviceId id : g_connected) {
    auto it = g_devices.find(uint64_t(id));
    if (it == g_devices.end() || !it->second.driver) {
      continue;
    }
    const DeviceRecord& record = it->second;
    DeviceRecord unmoved = record;
    unmoved.forced_user.reset();
    ConnectedDevice device;
    device.id = id;
    device.name = record.name;
    device.slot = record.ordinal;
    device.user = UserOfLocked(record);
    device.natural_user = UserOfLocked(unmoved);
    device.keyboard = record.keyboard;
    device.pollable = !record.keyboard && record.driver->pollable();
    device.reassigned = record.forced_user.has_value();
    devices.push_back(std::move(device));
  }
  return devices;
}

}  // namespace menu_input
}  // namespace skylanders
