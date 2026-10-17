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
REXCVAR_DEFINE_INT32(portal_menu_hold_ms, 600, "Portal",
                     "How long a hold_<button> entry of portal_menu_open_buttons must be held")
    .range(100, 5000);
REXCVAR_DEFINE_INT32(portal_menu_chord_ms, 150, "Portal",
                     "How long the first button of an open chord is kept from the game while "
                     "waiting for the others")
    .range(0, 1000);
REXCVAR_DEFINE_INT32(keyboard_player, 1, "Input",
                     "Which player the keyboard plays as (1-4); 2 tries co-op with one controller")
    .range(1, 4);

namespace skylanders {
namespace {

using rex::X_RESULT;
using rex::X_STATUS;
using rex::input::DeviceId;
using rex::input::DeviceInfo;
using rex::input::InputDriver;
using rex::input::X_INPUT_GAMEPAD;
using rex::input::X_INPUT_STATE;
using Clock = std::chrono::steady_clock;

class MenuInputDriver;

struct DeviceRecord {
  MenuInputDriver* driver = nullptr;
  uint32_t ordinal = 0;
  bool synthetic = false;
  bool keyboard = false;  // the keyboard/mouse emulation, plays as keyboard_player
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
};

std::mutex g_mutex;
std::unordered_map<uint64_t, DeviceRecord> g_devices;  // by DeviceId
std::function<void(DeviceId, uint32_t)> g_open_handler;
std::atomic<bool> g_menu_open{false};
std::optional<DeviceId> g_keyboard;

struct OpenEntry {
  uint16_t buttons = 0;
  bool hold = false;
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

// Parses portal_menu_open_buttons. Unknown names are skipped with a warning once per value.
std::vector<OpenEntry> OpenEntries() {
  static std::string cached_value;
  static std::vector<OpenEntry> cached;
  const std::string value = REXCVAR_GET(portal_menu_open_buttons);
  if (value == cached_value && !cached.empty()) {
    return cached;
  }
  std::vector<OpenEntry> entries;
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
      REXLOG_WARN("Portal menu: ignoring unknown open button '{}'", token);
    }
  }
  cached_value = value;
  cached = entries;
  return entries;
}

bool AnyButtonDown(const X_INPUT_GAMEPAD& pad) {
  return uint16_t(pad.buttons) != 0 || pad.left_trigger > 30 || pad.right_trigger > 30;
}

// How long each button is kept from the game after it goes down, because it may be the start of
// an open entry: a chord's buttons for portal_menu_chord_ms, a hold entry's for the hold time.
// That way pressing Back to open the menu doesn't also open the game's pause menu. Released
// sooner, the game gets a short tap instead; still down afterwards, it sees the button from then
// on. Zero for buttons that start no such entry.
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
      if (entry.buttons & (1u << bit)) {
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

// Updates the open-button state of one device from its latest raw state. Returns true when the
// open button fired, with the entry's buttons in `fired_buttons`. Called with g_mutex held.
bool ObserveLocked(DeviceRecord& record, const X_INPUT_GAMEPAD& pad, uint16_t* fired_buttons) {
  const uint16_t buttons = pad.buttons;
  const auto now = Clock::now();
  bool fired = false;
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
    if (entry_fired) {
      fired = true;
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

uint32_t UserOfLocked(const DeviceRecord& record) {
  if (record.forced_user) {
    return *record.forced_user;
  }
  if (record.keyboard) {
    return uint32_t(std::clamp(REXCVAR_GET(keyboard_player), 1, int(rex::input::kMaxGuestUsers)) - 1);
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
    if (hide) {
      std::memset(&state.gamepad, 0, sizeof(state.gamepad));
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

  X_RESULT SetDeviceVibration(DeviceId id, rex::input::X_INPUT_VIBRATION* vibration) override {
    return inner_->SetDeviceVibration(id, vibration);
  }

  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags,
                              rex::input::X_INPUT_KEYSTROKE* out_keystroke) override {
    if (g_menu_open.load() || !is_active()) {
      return X_ERROR_EMPTY;
    }
    return inner_->GetDeviceKeystroke(id, flags, out_keystroke);
  }

 private:
  // Updates the open-button state of a device; calls the open handler and returns true when it
  // fired. `hide` is set while the device must stay hidden after the menu closed, `visible` to
  // the buttons the game may see.
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
      fire = ObserveLocked(record, pad, &fired_buttons) && !g_menu_open.load();
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
      if (fire) handler = g_open_handler;
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
  std::optional<X_INPUT_GAMEPAD> Poll(DeviceId id) {
    if (!pollable_) {
      return std::nullopt;
    }
    X_INPUT_STATE state{};
    if (inner_->GetDeviceState(id, &state) != X_ERROR_SUCCESS) {
      return std::nullopt;
    }
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
    for (const DeviceInfo& info : devices) {
      DeviceRecord& record = g_devices[uint64_t(info.id)];
      record.ordinal = info.ordinal;
      record.synthetic = info.synthetic;
      devices_.push_back(info.id);
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

void SetOpenHandler(std::function<void(DeviceId, uint32_t)> handler) {
  std::lock_guard lock(g_mutex);
  g_open_handler = std::move(handler);
}

void SetMenuOpen(bool open) {
  if (!open) {
    std::lock_guard lock(g_mutex);
    for (auto& [id, record] : g_devices) {
      record.hidden_until_release = true;
      // The closing press must not count as a new open press.
      record.chord_fired = true;
    }
  }
  g_menu_open.store(open);
}

bool MenuOpen() { return g_menu_open.load(); }

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

void SetDevicePlayer(DeviceId device, std::optional<uint32_t> user) {
  std::lock_guard lock(g_mutex);
  if (user && *user >= rex::input::kMaxGuestUsers) {
    return;
  }
  g_devices[uint64_t(device)].forced_user = user;
}

bool IsOpenChord(uint16_t buttons) {
  for (const OpenEntry& entry : OpenEntries()) {
    if ((buttons & entry.buttons) == entry.buttons) {
      return true;
    }
  }
  return false;
}

}  // namespace menu_input
}  // namespace skylanders
