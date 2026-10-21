#include "portal_rex/settings_menu.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/input/device.h>
#include <rex/input/input.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/windowed_app_context.h>

#include "portal_rex/input_pictures.h"
#include "portal_rex/menu_draw.h"
#include "portal_rex/portal_menu_input.h"
#include "portal_rex/settings_file.h"

namespace skylanders {
namespace {

using namespace menu_draw;
using rex::input::DeviceId;
using Clock = std::chrono::steady_clock;

bool g_german = false;

const std::string& Tr(const std::string& en, const std::string& de) {
  return g_german && !de.empty() ? de : en;
}
const char* Tr(const char* en, const char* de) { return g_german ? de : en; }

// What the menu does with one frame of input.
struct Actions {
  bool up = false, down = false, left = false, right = false;
  bool accept = false, back = false, prev_page = false, next_page = false, save = false;
};

// Edges and key repeat for one controller.
class PadReader {
 public:
  Actions Read(const rex::input::X_INPUT_GAMEPAD& pad) {
    Actions a;
    const uint16_t buttons = pad.buttons;
    auto pressed = [&](uint16_t bit) { return (buttons & bit) && !(prev_ & bit); };
    a.accept = pressed(rex::input::X_INPUT_GAMEPAD_A);
    a.save = pressed(rex::input::X_INPUT_GAMEPAD_Y);
    a.back = pressed(rex::input::X_INPUT_GAMEPAD_B) || pressed(rex::input::X_INPUT_GAMEPAD_START) ||
             (menu_input::IsActionChord(menu_input::ButtonAction::kSettingsMenu, buttons) &&
              !menu_input::IsActionChord(menu_input::ButtonAction::kSettingsMenu, prev_));
    const bool lt = pad.left_trigger > 128, rt = pad.right_trigger > 128;
    a.prev_page = pressed(rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER) || (lt && !prev_lt_);
    a.next_page = pressed(rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER) || (rt && !prev_rt_);
    prev_lt_ = lt;
    prev_rt_ = rt;
    const float lx = float(int16_t(pad.thumb_lx)) / 32767.0f;
    const float ly = float(int16_t(pad.thumb_ly)) / 32767.0f;
    const bool dirs[4] = {
        (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_UP) || ly > 0.55f,
        (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_DOWN) || ly < -0.55f,
        (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_LEFT) || lx < -0.55f,
        (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT) || lx > 0.55f,
    };
    bool* outs[4] = {&a.up, &a.down, &a.left, &a.right};
    const auto now = Clock::now();
    for (int i = 0; i < 4; ++i) {
      if (dirs[i] && !held_[i]) {
        *outs[i] = true;
        next_repeat_[i] = now + std::chrono::milliseconds(350);
      } else if (dirs[i] && now >= next_repeat_[i]) {
        *outs[i] = true;
        next_repeat_[i] = now + std::chrono::milliseconds(110);
      }
      held_[i] = dirs[i];
    }
    prev_ = buttons;
    return a;
  }

  static bool Released(const rex::input::X_INPUT_GAMEPAD& pad) {
    return uint16_t(pad.buttons) == 0 && pad.left_trigger < 30 && pad.right_trigger < 30;
  }

 private:
  uint16_t prev_ = 0;
  bool prev_lt_ = false, prev_rt_ = false;
  bool held_[4] = {};
  Clock::time_point next_repeat_[4]{};
};

Actions ReadKeyboard() {
  Actions a;
  auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
  auto once = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
  a.up = key(ImGuiKey_UpArrow) || key(ImGuiKey_W);
  a.down = key(ImGuiKey_DownArrow) || key(ImGuiKey_S);
  a.left = key(ImGuiKey_LeftArrow) || key(ImGuiKey_A);
  a.right = key(ImGuiKey_RightArrow) || key(ImGuiKey_D);
  a.accept = once(ImGuiKey_Enter) || once(ImGuiKey_KeypadEnter) || once(ImGuiKey_Space);
  a.back = once(ImGuiKey_Escape) || once(ImGuiKey_Backspace);
  a.save = once(ImGuiKey_Y);
  a.prev_page = once(ImGuiKey_Q) || once(ImGuiKey_1);
  a.next_page = once(ImGuiKey_E) || once(ImGuiKey_3);
  return a;
}

bool Any(const Actions& a) {
  return a.up || a.down || a.left || a.right || a.accept || a.back || a.prev_page || a.next_page ||
         a.save;
}

void Merge(Actions& into, const Actions& from) {
  into.up |= from.up;
  into.down |= from.down;
  into.left |= from.left;
  into.right |= from.right;
  into.accept |= from.accept;
  into.back |= from.back;
  into.prev_page |= from.prev_page;
  into.next_page |= from.next_page;
  into.save |= from.save;
}

// Shared by every menu instance: the pages and what was changed this session.
struct SettingsState {
  std::vector<SettingsPage> pages;
  std::filesystem::path config_path;
  std::unordered_map<std::string, std::string> startup;  // cvar -> value at start-up
  std::unordered_map<std::string, std::string> stored;   // cvar -> value written this session
  // The open menu's changes, applied but not saved yet, and each changed cvar's value before.
  std::unordered_map<std::string, std::string> edits;
  std::unordered_map<std::string, std::string> before;
};
SettingsState g_state;  // UI thread only
ControllerPicture g_picture;  // UI thread only

// What the menu was opened with: the keyboard (F4) or a controller's buttons. UI thread only.
bool g_opened_by_keyboard = false;
std::optional<DeviceId> g_opened_by_device;

// The kind of controller the controls page draws and names buttons for (the one last used in
// the menu). UI thread only.
input_pictures::PadKind g_pad_kind = input_pictures::PadKind::kXbox;
// One button entry of a *_buttons value: "back+start" or "hold_back".
struct ButtonEntry {
  std::vector<std::string> buttons;
  bool hold = false;
};

std::vector<ButtonEntry> ParseButtonEntries(const std::string& value) {
  std::vector<ButtonEntry> out;
  size_t start = 0;
  while (start <= value.size()) {
    const size_t comma = std::min(value.find(',', start), value.size());
    std::string token;
    for (char c : value.substr(start, comma - start)) {
      if (c != ' ') token.push_back(char(std::tolower(static_cast<unsigned char>(c))));
    }
    start = comma + 1;
    if (token.empty()) continue;
    ButtonEntry entry;
    if (token.rfind("hold_", 0) == 0) {
      entry.hold = true;
      token.erase(0, 5);
    }
    size_t from = 0;
    while (from <= token.size()) {
      const size_t plus = std::min(token.find('+', from), token.size());
      if (plus > from) entry.buttons.push_back(token.substr(from, plus - from));
      from = plus + 1;
    }
    if (!entry.buttons.empty()) out.push_back(std::move(entry));
  }
  return out;
}

std::string ButtonName(const std::string& button) {
  using input_pictures::PadKind;
  if (button == "guide") {
    switch (g_pad_kind) {
      case PadKind::kXbox: return Tr("Xbox button", "Xbox-Taste");
      case PadKind::kPlayStation: return Tr("PS button", "PS-Taste");
      case PadKind::kNintendo: return Tr("Home button", "Home-Taste");
      case PadKind::kGeneric: return Tr("Guide button", "Guide-Taste");
    }
  }
  if (const char* name = input_pictures::PadButtonName(g_pad_kind, button, g_german)) return name;
  if (button == "up") return Tr("D-pad up", "Steuerkreuz hoch");
  if (button == "down") return Tr("D-pad down", "Steuerkreuz runter");
  if (button == "left") return Tr("D-pad left", "Steuerkreuz links");
  if (button == "right") return Tr("D-pad right", "Steuerkreuz rechts");
  std::string upper = button;
  for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
  return upper;
}

// "Back + Start", "hold Back" / "Back halten".
std::string EntryName(const ButtonEntry& entry) {
  std::string names;
  for (const std::string& button : entry.buttons) {
    names += (names.empty() ? "" : " + ") + ButtonName(button);
  }
  if (!entry.hold) return names;
  return g_german ? names + " halten" : "hold " + names;
}

// The keyboard bind that does the same as a *_buttons setting (they are registered next to it).
const char* KeyboardBindOf(const std::string& cvar) {
  if (cvar == "portal_menu_open_buttons") return "bind_portal_menu";
  if (cvar == "portal_item_off_buttons") return "bind_portal_item_off";
  if (cvar == "player_menu_buttons") return "bind_player_menu";
  if (cvar == "settings_menu_buttons") return "bind_settings_menu";
  return nullptr;
}

// "Semicolon,Space" -> {"Semicolon", "Space"}.
std::vector<std::string> SplitKeys(const std::string& value) {
  std::vector<std::string> keys;
  size_t start = 0;
  while (start <= value.size()) {
    const size_t comma = std::min(value.find(',', start), value.size());
    std::string key;
    for (char c : value.substr(start, comma - start)) {
      if (c != ' ') key.push_back(c);
    }
    if (!key.empty()) keys.push_back(std::move(key));
    start = comma + 1;
  }
  return keys;
}

// The keyboard's controller emulation (the SDK's keybind_* settings, mnk_mode) and the button
// each one stands for, as printed on the key.
struct GameKey {
  const char* cvar;
  const char* en;
  const char* de;
};
constexpr GameKey kGameKeys[] = {
    {"keybind_a", "A", "A"},
    {"keybind_b", "B", "B"},
    {"keybind_x", "X", "X"},
    {"keybind_y", "Y", "Y"},
    {"keybind_left_trigger", "LT", "LT"},
    {"keybind_right_trigger", "RT", "RT"},
    {"keybind_left_shoulder", "LB", "LB"},
    {"keybind_right_shoulder", "RB", "RB"},
    {"keybind_lstick_up", "Move", "Laufen"},
    {"keybind_lstick_down", "Move", "Laufen"},
    {"keybind_lstick_left", "Move", "Laufen"},
    {"keybind_lstick_right", "Move", "Laufen"},
    {"keybind_lstick_press", "L3", "L3"},
    {"keybind_rstick_up", "Camera", "Kamera"},
    {"keybind_rstick_down", "Camera", "Kamera"},
    {"keybind_rstick_left", "Camera", "Kamera"},
    {"keybind_rstick_right", "Camera", "Kamera"},
    {"keybind_rstick_press", "R3", "R3"},
    {"keybind_dpad_up", "D-pad", "Kreuz"},
    {"keybind_dpad_down", "D-pad", "Kreuz"},
    {"keybind_dpad_left", "D-pad", "Kreuz"},
    {"keybind_dpad_right", "D-pad", "Kreuz"},
    {"keybind_back", "Back", "Back"},
    {"keybind_start", "Start", "Start"},
    {"keybind_guide", "Guide", "Guide"},
};
constexpr ImU32 kGameKeyColour = IM_COL32(178, 188, 224, 255);

// One colour per button setting (or entry) on the controls page's pictures.
constexpr ImU32 kColours[] = {IM_COL32(244, 196, 84, 255), IM_COL32(90, 200, 255, 255),
                              IM_COL32(120, 220, 130, 255), IM_COL32(240, 120, 200, 255)};

bool CvarOn(const char* name) {
  const std::string v = rex::cvar::GetFlagByName(name);
  return v == "true" || v == "1";
}

const std::vector<SettingOption>& BoolOptions() {
  static const std::vector<SettingOption> options = {{"false", "Off", "Aus"}, {"true", "On", "An"}};
  return options;
}

// A button row without a setting of its own: shows the page's button settings together.
bool IsOverview(const Setting& s) { return s.buttons && s.cvar.empty() && !s.action; }

const std::vector<SettingOption>& OptionsOf(const Setting& s) {
  static const std::vector<SettingOption> kNone;
  if (IsOverview(s)) return kNone;
  return s.options.empty() && !s.action ? BoolOptions() : s.options;
}

bool SameValue(const std::string& a, const std::string& b) {
  if (a == b) return true;
  char* end_a = nullptr;
  char* end_b = nullptr;
  const double x = std::strtod(a.c_str(), &end_a);
  const double y = std::strtod(b.c_str(), &end_b);
  return !a.empty() && !b.empty() && *end_a == 0 && *end_b == 0 && x == y;
}

// The value as saved: written this session, else the running one.
std::string SavedValue(const std::string& cvar) {
  if (auto it = g_state.stored.find(cvar); it != g_state.stored.end()) return it->second;
  return rex::cvar::GetFlagByName(cvar);
}

// The value the menu shows: an unsaved change first.
std::string ValueOf(const std::string& cvar) {
  if (auto it = g_state.edits.find(cvar); it != g_state.edits.end()) return it->second;
  return SavedValue(cvar);
}

std::string CurrentValue(const Setting& s) {
  if (s.current) return s.current();
  return ValueOf(s.cvar);
}

bool HasUnsaved() {
  for (const auto& [cvar, value] : g_state.edits) {
    auto before = g_state.before.find(cvar);
    if (before == g_state.before.end() || !SameValue(value, before->second)) return true;
  }
  return false;
}

int IndexOf(const Setting& s) {
  const std::string value = CurrentValue(s);
  const auto& options = OptionsOf(s);
  for (size_t i = 0; i < options.size(); ++i) {
    if (SameValue(options[i].value, value)) return int(i);
  }
  return -1;
}

bool ApplyLater(const Setting& s) {
  if (s.restart) return true;
  if (s.cvar.empty()) return false;
  const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(s.cvar);
  return info && info->lifecycle != rex::cvar::Lifecycle::kHotReload;
}

// Changed this session (saved or not) and only applied at the next start.
bool Pending(const Setting& s) {
  if (!ApplyLater(s) || s.cvar.empty()) return false;
  auto startup = g_state.startup.find(s.cvar);
  return startup != g_state.startup.end() && !SameValue(ValueOf(s.cvar), startup->second);
}

class SettingsMenuDialog final : public rex::ui::ImGuiDialog {
 public:
  // `wait_for_menu`: another menu was asked to close (CloseOpenMenu); come up once it has.
  SettingsMenuDialog(rex::ui::ImGuiDrawer* drawer, std::function<void()> on_done,
                     rex::ui::WindowedAppContext* app_context, bool wait_for_menu)
      : ImGuiDialog(drawer),
        on_done_(std::move(on_done)),
        app_context_(app_context),
        waiting_for_menu_(wait_for_menu) {
    g_german = menu_draw::GermanText();
    if (!waiting_for_menu_) Start();
  }

  ~SettingsMenuDialog() override {
    if (!done_ && !waiting_for_menu_) menu_input::SetMenuOpen(false);
  }

  void Close() {
    if (done_) return;
    done_ = true;
    if (!waiting_for_menu_) {
      menu_input::SetMenuOpen(false);
      REXLOG_INFO("Settings menu: closed");
    }
    if (on_done_) on_done_();
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (done_) return;
    if (waiting_for_menu_) {
      // The portal menu takes a moment to put the figure back; give up if it doesn't close.
      if (menu_input::MenuOpen()) {
        if (Clock::now() - created_ > std::chrono::seconds(3)) Close();
        return;
      }
      waiting_for_menu_ = false;
      Start();
    }
    SetScale(io.DisplaySize.y / 1080.0f);
    HandleInput(ReadActions());
    if (done_) return;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    if (first_frame_) {
      ImGui::SetNextWindowFocus();
      first_frame_ = false;
    }
    ImGui::Begin("##settings_menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int first_vertex = dl->VtxBuffer.Size;
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    dl->AddRectFilled({0, 0}, {W, H}, Rgba(6, 10, 30, 120));
    const ImVec2 p0{W * 0.06f, H * 0.07f}, p1{W * 0.94f, H * 0.93f};
    DrawPanel(dl, p0, p1);
    OutlinedLabel(dl, TitleFont(), S(46), {p0.x + S(38), p0.y + S(20)}, kGold,
                  Tr("SETTINGS", "EINSTELLUNGEN"), S(2.5f));
    DrawTabs(dl, {p0.x + S(38), p0.y + S(100)}, p1.x - S(38));
    const float list_right = p0.x + (p1.x - p0.x) * 0.60f;
    DrawRows(dl, {p0.x + S(38), p0.y + S(168)}, {list_right, p1.y - S(86)});
    DrawInfo(dl, {list_right + S(30), p0.y + S(168)}, {p1.x - S(38), p1.y - S(86)});
    DrawFooter(dl, p0, p1);
    if (confirm_) DrawConfirm(dl, p0, p1);
    FadeVertices(dl, first_vertex,
                 std::chrono::duration<float>(Clock::now() - open_time_).count() / 0.15f);
    ImGui::End();
  }

 private:
  const SettingsPage* Page() const {
    if (g_state.pages.empty()) return nullptr;
    return &g_state.pages[std::min(page_, g_state.pages.size() - 1)];
  }

  const Setting* Selected() const {
    const SettingsPage* page = Page();
    if (!page || page->settings.empty()) return nullptr;
    return &page->settings[std::min(row_, page->settings.size() - 1)];
  }

  Actions ReadActions() {
    Actions actions;
    bool released = true;
    for (const menu_input::ConnectedDevice& device : menu_input::ConnectedDevices()) {
      if (!device.pollable) continue;
      const auto pad = menu_input::ReadPad(device.id);
      if (!pad) continue;
      released = released && PadReader::Released(*pad);
      const Actions read = pads_[uint64_t(device.id)].Read(*pad);
      if (waiting_for_release_) continue;
      Merge(actions, read);
      // The controls page shows the device that was used last.
      if (Any(read)) UseDevice(device);
    }
    if (waiting_for_release_) {
      if (released && !ImGui::IsKeyDown(ImGuiKey_F4)) waiting_for_release_ = false;
      return {};
    }
    const Actions keys = ReadKeyboard();
    if (Any(keys)) keyboard_view_ = true;
    Merge(actions, keys);
    if (ImGui::IsKeyPressed(ImGuiKey_F4, false)) actions.back = true;
    return actions;
  }

  void UseDevice(const menu_input::ConnectedDevice& device) {
    keyboard_view_ = device.keyboard;
    if (!device.keyboard) g_pad_kind = input_pictures::PadKindOf(device.name);
  }

  void Start() {
    g_state.edits.clear();
    g_state.before.clear();
    // Start with the picture of what opened the menu.
    keyboard_view_ = g_opened_by_keyboard;
    g_pad_kind = input_pictures::PadKind::kXbox;
    for (const menu_input::ConnectedDevice& device : menu_input::ConnectedDevices()) {
      if (g_opened_by_device && device.id == *g_opened_by_device) UseDevice(device);
    }
    menu_input::SetMenuOpen(true);
    open_time_ = Clock::now();
    REXLOG_INFO("Settings menu: opened");
  }

  // Back, F4, Start: leaves at once, or asks first when there are unsaved changes.
  void RequestClose() {
    if (HasUnsaved()) {
      confirm_ = true;
      confirm_choice_ = 0;
      return;
    }
    Leave();
  }

  // Closes and then runs the action that was waiting for it, if any.
  void Leave() {
    confirm_ = false;
    std::function<void()> action = std::move(pending_action_);
    pending_action_ = nullptr;
    Close();
    if (action && app_context_) app_context_->CallInUIThreadDeferred(std::move(action));
  }

  // Writes the changes into the settings file.
  void Save() {
    bool wrote = false;
    for (const auto& [cvar, value] : g_state.edits) {
      auto before = g_state.before.find(cvar);
      if (before != g_state.before.end() && SameValue(value, before->second)) continue;
      if (StoreSetting(g_state.config_path, cvar, value)) {
        g_state.stored[cvar] = value;
        wrote = true;
      }
    }
    g_state.edits.clear();
    g_state.before.clear();
    if (wrote) {
      saved_at_ = Clock::now();
      REXLOG_INFO("Settings menu: changes saved");
    }
  }

  // Puts the values from before the changes back.
  void Discard() {
    for (const auto& [cvar, value] : g_state.before) StoreSetting({}, cvar, value);
    g_state.edits.clear();
    g_state.before.clear();
    REXLOG_INFO("Settings menu: changes discarded");
  }

  // 0 save, 1 don't save, 2 back to the menu.
  void ChooseConfirm(int choice) {
    if (choice == 0) {
      Save();
      Leave();
    } else if (choice == 1) {
      Discard();
      Leave();
    } else {
      confirm_ = false;
      pending_action_ = nullptr;
    }
  }

  void HandleInput(const Actions& a) {
    if (confirm_) {
      if (a.up || a.left) confirm_choice_ = (confirm_choice_ + 2) % 3;
      if (a.down || a.right) confirm_choice_ = (confirm_choice_ + 1) % 3;
      if (a.back) ChooseConfirm(2);
      else if (a.save) ChooseConfirm(0);
      else if (a.accept) ChooseConfirm(confirm_choice_);
      return;
    }
    if (a.save) {
      Save();
      return;
    }
    if (a.back) {
      RequestClose();
      return;
    }
    const size_t pages = g_state.pages.size();
    if (!pages) return;
    if (a.prev_page || a.next_page) {
      page_ = (page_ + pages + (a.next_page ? 1 : -1)) % pages;
      row_ = scroll_ = 0;
    }
    const SettingsPage* page = Page();
    const size_t rows = page ? page->settings.size() : 0;
    if (!rows) return;
    if (a.up) row_ = (row_ + rows - 1) % rows;
    if (a.down) row_ = (row_ + 1) % rows;
    const Setting& s = page->settings[std::min(row_, rows - 1)];
    if (s.action) {
      if (a.accept) RunAction(s);
      return;
    }
    if (a.left) Change(s, -1);
    if (a.right || a.accept) Change(s, +1);
  }

  void Change(const Setting& s, int delta) {
    const auto& options = OptionsOf(s);
    if (options.empty()) return;
    const int n = int(options.size());
    const int index = IndexOf(s);
    const int next = index < 0 ? 0 : (index + delta + n) % n;
    const std::string& value = options[size_t(next)].value;
    // Applied now (where the setting allows it) but saved only with Y.
    auto edit = [](const std::string& cvar, const std::string& v) {
      if (!g_state.before.contains(cvar)) g_state.before[cvar] = SavedValue(cvar);
      if (StoreSetting({}, cvar, v)) g_state.edits[cvar] = v;
    };
    if (s.preset) {
      for (const auto& [cvar, v] : s.preset(value)) edit(cvar, v);
    } else {
      edit(s.cvar, value);
    }
  }

  // Actions run with the menu closed, after this frame (one may open another overlay); unsaved
  // changes are asked about first.
  void RunAction(const Setting& s) {
    pending_action_ = s.action;
    RequestClose();
  }

  // Unsaved changes when leaving: save, don't save, or back to the menu.
  void DrawConfirm(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
    dl->AddRectFilled(p0, p1, Rgba(4, 6, 20, 170), S(28));
    const float w = std::min(S(760), p1.x - p0.x - S(40)), h = S(420);
    const ImVec2 c{(p0.x + p1.x) / 2, (p0.y + p1.y) / 2};
    const ImVec2 a{c.x - w / 2, c.y - h / 2}, b{c.x + w / 2, c.y + h / 2};
    DrawPanel(dl, a, b);
    OutlinedLabel(dl, TitleFont(), S(36), {a.x + S(36), a.y + S(28)}, kGold,
                  Tr("Save changes?", "\xC3\x84nderungen speichern?"), S(2));
    Label(dl, BoldFont(), S(21), {a.x + S(36), a.y + S(88)}, kDim,
          Tr("You changed settings that are not saved yet.",
             "Du hast Einstellungen ge\xC3\xA4ndert, die noch nicht gespeichert sind."),
          w - S(72));
    const char* options[3] = {Tr("Save", "Speichern"), Tr("Don't save", "Nicht speichern"),
                              Tr("Back to the settings", "Zur\xC3\xBC" "ck zu den Einstellungen")};
    float y = a.y + S(150);
    for (int i = 0; i < 3; ++i) {
      const ImVec2 r0{a.x + S(36), y}, r1{b.x - S(36), y + S(60)};
      if (ImGui::IsMouseHoveringRect(r0, r1)) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) confirm_choice_ = i;
        if (ImGui::IsMouseClicked(0)) {
          ChooseConfirm(i);
          return;
        }
      }
      const bool active = i == confirm_choice_;
      dl->AddRectFilled(r0, r1, active ? Rgba(255, 255, 255, 52) : Rgba(255, 255, 255, 14), S(14));
      if (active) dl->AddRect(r0, r1, kGold, S(14), 0, S(3));
      Label(dl, BoldFont(), S(23), {r0.x + S(22), r0.y + S(15)}, active ? kGold : kWhite,
            options[i]);
      y += S(72);
    }
  }

  void DrawTabs(ImDrawList* dl, ImVec2 a, float right) {
    float x = a.x;
    const float h = S(46);
    for (size_t i = 0; i < g_state.pages.size(); ++i) {
      const std::string& title = Tr(g_state.pages[i].en, g_state.pages[i].de);
      const ImVec2 ts = TextSize(BoldFont(), S(22), title);
      const ImVec2 t0{x, a.y}, t1{x + ts.x + S(40), a.y + h};
      if (t1.x > right) break;
      const bool active = i == page_;
      dl->AddRectFilled(t0, t1, active ? kGold : Rgba(255, 255, 255, 26), S(23));
      if (active) dl->AddRect(t0, t1, Rgba(255, 240, 190), S(23), 0, S(2));
      Label(dl, BoldFont(), S(22), {t0.x + S(20), CapCentredY(t0.y + h / 2, S(22))},
            active ? kInk : kDim, title);
      if (!confirm_ && ImGui::IsMouseHoveringRect(t0, t1) && ImGui::IsMouseClicked(0)) {
        page_ = i;
        row_ = scroll_ = 0;
      }
      x = t1.x + S(12);
    }
  }

  void DrawRows(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    const SettingsPage* page = Page();
    if (!page) return;
    const float row_h = S(64), gap = S(8);
    const size_t visible = std::max<size_t>(1, size_t((b.y - a.y + gap) / (row_h + gap)));
    if (row_ < scroll_) scroll_ = row_;
    if (row_ >= scroll_ + visible) scroll_ = row_ - visible + 1;
    for (size_t i = scroll_; i < page->settings.size() && i < scroll_ + visible; ++i) {
      const Setting& s = page->settings[i];
      const ImVec2 r0{a.x, a.y + (i - scroll_) * (row_h + gap)}, r1{b.x, r0.y + row_h};
      const bool active = i == row_;
      if (!confirm_ && ImGui::IsMouseHoveringRect(r0, r1) &&
          (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f)) {
        row_ = i;
      }
      dl->AddRectFilled(r0, r1, active ? Rgba(255, 255, 255, 52) : Rgba(255, 255, 255, 14), S(14));
      if (active) dl->AddRect(r0, r1, kGold, S(14), 0, S(3));
      const std::string& label = Tr(s.en, s.de);
      Label(dl, BoldFont(), S(23), {r0.x + S(22), r0.y + S(17)}, kWhite, label);
      if (Pending(s)) {
        const float lx = r0.x + S(30) + TextSize(BoldFont(), S(23), label).x;
        dl->AddCircleFilled({lx, r0.y + row_h / 2}, S(6), kGold);
      }
      if (s.action) {
        const char* go = Tr("Open", "\xC3\x96" "ffnen");
        const ImVec2 ts = TextSize(BoldFont(), S(21), go);
        Label(dl, BoldFont(), S(21), {r1.x - S(24) - ts.x, r0.y + S(19)}, kGold, go);
        if (!confirm_ && ImGui::IsMouseHoveringRect(r0, r1) && ImGui::IsMouseClicked(0)) {
          RunAction(s);
          return;
        }
        continue;
      }
      if (IsOverview(s)) continue;
      // The value between arrows, right-aligned.
      const auto& options = OptionsOf(s);
      const int index = IndexOf(s);
      const std::string value =
          index >= 0 ? Tr(options[size_t(index)].en, options[size_t(index)].de)
          : s.current ? std::string(Tr("Custom", "Eigene"))
                      : CurrentValue(s);
      const float box_w = S(270);
      const ImVec2 v0{r1.x - S(18) - box_w, r0.y + S(12)}, v1{r1.x - S(18), r1.y - S(12)};
      dl->AddRectFilled(v0, v1, active ? Rgba(16, 22, 60, 230) : Rgba(16, 22, 60, 150), S(20));
      // Long values get a smaller font so they stay between the arrows.
      float size = S(21);
      ImVec2 vs = TextSize(BoldFont(), size, value);
      while (vs.x > box_w - S(80) && size > S(13)) {
        size -= S(1);
        vs = TextSize(BoldFont(), size, value);
      }
      Label(dl, BoldFont(), size, {(v0.x + v1.x - vs.x) / 2, (v0.y + v1.y - vs.y) / 2},
            active ? kGold : kWhite, value);
      const ImU32 arrow = active ? kGold : Rgba(255, 255, 255, 90);
      Label(dl, BoldFont(), S(24), {v0.x + S(12), v0.y + S(5)}, arrow, "<");
      Label(dl, BoldFont(), S(24), {v1.x - S(26), v0.y + S(5)}, arrow, ">");
      if (!confirm_ && ImGui::IsMouseHoveringRect(v0, v1) && ImGui::IsMouseClicked(0)) {
        row_ = i;
        Change(s, ImGui::GetIO().MousePos.x < (v0.x + v1.x) / 2 ? -1 : +1);
      }
    }
    const size_t total = page->settings.size();
    if (total > visible) {
      const float track_h = b.y - a.y;
      const float thumb_h = track_h * float(visible) / float(total);
      const float thumb_y = a.y + track_h * float(scroll_) / float(total);
      dl->AddRectFilled({b.x + S(8), thumb_y}, {b.x + S(14), thumb_y + thumb_h},
                        Rgba(255, 255, 255, 90), S(3));
    }
  }

  void DrawInfo(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    const Setting* s = Selected();
    GradientRect(dl, a, b, Rgba(40, 52, 120, 235), Rgba(20, 26, 66, 235), S(20));
    dl->AddRect(a, b, Rgba(255, 255, 255, 40), S(20), 0, S(1.5f));
    if (!s) return;
    const float wrap = b.x - a.x - S(48);
    float y = a.y + S(24);
    const std::string& title = Tr(s->en, s->de);
    OutlinedLabel(dl, TitleFont(), S(34), {a.x + S(24), y}, kWhite, title, S(2));
    y += TextSize(TitleFont(), S(34), title).y + S(16);
    // The overview's text is about the controller; with the keyboard in use it says what the
    // keyboard picture shows instead.
    static const std::string kKeyboardHelpEn =
        "Every menu key on the keyboard, and the keys you play with: each shows the controller "
        "button it stands for (settings keybind_* in the settings file).";
    static const std::string kKeyboardHelpDe =
        "Alle Men\xC3\xBC-Tasten auf der Tastatur und die Tasten, mit denen du spielst: Auf jeder "
        "steht die Controller-Taste, f\xC3\xBCr die sie steht (Einstellungen keybind_* in der "
        "Einstellungsdatei).";
    const bool keyboard_help = keyboard_view_ && IsOverview(*s);
    const std::string& help = keyboard_help ? Tr(kKeyboardHelpEn, kKeyboardHelpDe)
                                            : Tr(s->help_en, s->help_de);
    if (!help.empty()) {
      Label(dl, BoldFont(), S(21), {a.x + S(24), y}, kDim, help, wrap);
      y += TextSize(BoldFont(), S(21), help, wrap).y + S(22);
    }
    // Every choice, the current one in gold.
    if (!s->action && !IsOverview(*s)) {
      const auto& options = OptionsOf(*s);
      const int index = IndexOf(*s);
      float x = a.x + S(24);
      for (size_t i = 0; i < options.size(); ++i) {
        const std::string& text = Tr(options[i].en, options[i].de);
        const ImVec2 ts = TextSize(BoldFont(), S(18), text);
        if (x + ts.x + S(28) > b.x - S(24)) {
          x = a.x + S(24);
          y += S(42);
        }
        const bool current = int(i) == index;
        dl->AddRectFilled({x, y}, {x + ts.x + S(28), y + S(34)},
                          current ? kGold : Rgba(255, 255, 255, 30), S(17));
        Label(dl, BoldFont(), S(18), {x + S(14), y + S(6)}, current ? kInk : kWhite, text);
        x += ts.x + S(40);
      }
      y += S(54);
    }
    if (ApplyLater(*s)) {
      const char* text = Pending(*s) ? Tr("Changed: applies at the next start",
                                          "Ge\xC3\xA4ndert: gilt ab dem n\xC3\xA4" "chsten Start")
                                     : Tr("Applies at the next start",
                                          "Gilt ab dem n\xC3\xA4" "chsten Start");
      const ImVec2 ts = TextSize(BoldFont(), S(18), text);
      dl->AddRectFilled({a.x + S(24), y}, {a.x + S(48) + ts.x, y + S(32)},
                        Pending(*s) ? kGold : Rgba(255, 255, 255, 40), S(16));
      Label(dl, BoldFont(), S(18), {a.x + S(36), y + S(5)}, Pending(*s) ? kInk : kWhite, text);
      y += S(48);
    }
    if (s->buttons) DrawButtons(dl, *s, {a.x + S(24), y}, {b.x - S(24), b.y - S(20)});
  }

  // Rings on the controller picture and a line per colour under it. A button setting shows its
  // current choice, one colour per entry; an overview row every button setting of its page, one
  // colour per setting.
  // The keyboard instead of the controller (the menu is used with the keyboard): a button setting
  // marks its keyboard key; the overview marks every menu key of the page, one colour each, and
  // the keys of the keyboard's controller emulation with the button they stand for.
  void DrawKeys(ImDrawList* dl, const Setting& s, ImVec2 a, ImVec2 b) {
    std::vector<input_pictures::KeyMark> marks;
    std::vector<std::pair<ImU32, std::string>> lines;
    auto add_menu_key = [&](const Setting& setting) {
      const char* bind = KeyboardBindOf(setting.cvar);
      if (!bind) return;
      const ImU32 colour = kColours[lines.size() % std::size(kColours)];
      std::string text = Tr(setting.en, setting.de) + ": ";
      const std::vector<std::string> keys = SplitKeys(rex::cvar::GetFlagByName(bind));
      for (size_t i = 0; i < keys.size(); ++i) {
        text += (i ? " / " : "") + input_pictures::KeyLegend(keys[i], g_german);
        marks.push_back({input_pictures::MainKey(keys[i]), colour, {}});
      }
      if (keys.empty()) text += Tr("no key", "keine Taste");
      lines.emplace_back(colour, std::move(text));
    };
    const bool overview = IsOverview(s);
    if (overview) {
      for (const Setting& other : Page()->settings) {
        if (other.buttons && !other.cvar.empty()) add_menu_key(other);
      }
      if (CvarOn("mnk_mode")) {
        for (const GameKey& k : kGameKeys) {
          for (const std::string& key : SplitKeys(rex::cvar::GetFlagByName(k.cvar))) {
            marks.push_back({input_pictures::MainKey(key), kGameKeyColour, Tr(k.en, k.de)});
          }
        }
        lines.emplace_back(kGameKeyColour, Tr("The game: the controller button on each key",
                                              "Das Spiel: die Controller-Taste auf jeder Taste"));
      } else {
        lines.emplace_back(kGameKeyColour,
                           Tr("Playing with the keyboard is off (mnk_mode)",
                              "Spielen mit der Tastatur ist aus (mnk_mode)"));
      }
    } else {
      add_menu_key(s);
    }
    // The keyboard is wide and flat: the lines go right under it.
    const float line_h = S(34);
    const float lines_h = line_h * float(std::max<size_t>(1, lines.size()));
    const float keyboard_h =
        input_pictures::DrawKeyboard(dl, a, {b.x, b.y - lines_h - S(12)}, marks, g_german);
    float y = a.y + keyboard_h + S(24);
    for (const auto& [colour, text] : lines) {
      dl->AddCircleFilled({a.x + S(10), y + S(16)}, S(9), colour);
      float size = S(20);
      while (TextSize(BoldFont(), size, text).x > b.x - a.x - S(30) && size > S(14)) size -= S(1);
      Label(dl, BoldFont(), size, {a.x + S(30), y + S(4)}, kWhite, text);
      y += line_h;
    }
  }

  void DrawButtons(ImDrawList* dl, const Setting& s, ImVec2 a, ImVec2 b) {
    if (keyboard_view_) {
      DrawKeys(dl, s, a, b);
      return;
    }
    struct Group {
      std::vector<ButtonEntry> entries;
      std::string label;
    };
    std::vector<Group> groups;
    if (IsOverview(s)) {
      for (const Setting& other : Page()->settings) {
        if (!other.buttons || other.cvar.empty()) continue;
        Group group{ParseButtonEntries(ValueOf(other.cvar)), Tr(other.en, other.de) + ": "};
        for (size_t i = 0; i < group.entries.size(); ++i) {
          group.label += (i ? " / " : "") + EntryName(group.entries[i]);
        }
        if (group.entries.empty()) group.label += Tr("off", "aus");
        groups.push_back(std::move(group));
      }
    } else {
      const auto& options = OptionsOf(s);
      const int index = IndexOf(s);
      const std::string value = index >= 0 ? options[size_t(index)].value : CurrentValue(s);
      for (ButtonEntry& entry : ParseButtonEntries(value)) {
        std::string label = EntryName(entry);
        groups.push_back({{std::move(entry)}, std::move(label)});
      }
    }
    const float line_h = S(34);
    const float lines_top = b.y - line_h * float(std::max<size_t>(1, groups.size()));
    const ImTextureID texture =
        g_picture.art_key.empty() ? ImTextureID{} : GameArtTexture(g_picture.art_key);
    // The game's own picture is an Xbox controller; any other pad, or a game without a picture,
    // gets a drawn one in its layout.
    if (!texture || g_picture.width <= 0 || g_picture.height <= 0 ||
        g_pad_kind != input_pictures::PadKind::kXbox) {
      std::vector<input_pictures::PadMark> marks;
      std::unordered_map<std::string, int> rings;
      for (size_t i = 0; i < groups.size(); ++i) {
        for (const ButtonEntry& entry : groups[i].entries) {
          for (const std::string& button : entry.buttons) {
            marks.push_back({button, kColours[i % std::size(kColours)], rings[button]++});
          }
        }
      }
      input_pictures::DrawController(dl, a, {b.x, lines_top - S(12)}, g_pad_kind, marks);
    } else {
      const float scale = std::min((b.x - a.x) / g_picture.width,
                                   (lines_top - S(12) - a.y) / g_picture.height);
      if (scale > 0.15f) {
        const ImVec2 p0{(a.x + b.x - g_picture.width * scale) / 2, a.y};
        const ImVec2 p1{p0.x + g_picture.width * scale, p0.y + g_picture.height * scale};
        dl->AddImage(texture, p0, p1);
        const float pulse = 0.5f + 0.5f * std::sin(float(ImGui::GetTime()) * 4.0f);
        // A button in several entries gets one ring around the other.
        std::unordered_map<std::string, int> rings;
        for (size_t i = 0; i < groups.size(); ++i) {
          const ImU32 colour = kColours[i % std::size(kColours)];
          for (const ButtonEntry& entry : groups[i].entries) {
            for (const std::string& button : entry.buttons) {
              const int outer = rings[button]++;
              for (const ControllerSpot& spot : g_picture.spots) {
                if (spot.button != button) continue;
                const ImVec2 c{p0.x + spot.x * scale, p0.y + spot.y * scale};
                const float r = spot.radius * scale + S(5) + S(7) * float(outer);
                if (!outer) {
                  dl->AddCircleFilled(c, r,
                                      (colour & 0x00FFFFFF) | (ImU32(50 + 50 * pulse) << 24));
                }
                dl->AddCircle(c, r, colour, 0, S(3.5f));
              }
            }
          }
        }
      }
    }
    float y = lines_top;
    if (groups.empty()) {
      Label(dl, BoldFont(), S(20), {a.x, y + S(4)}, kDim,
            Tr("Off: no controller button", "Aus: keine Controller-Taste"));
    }
    for (size_t i = 0; i < groups.size(); ++i, y += line_h) {
      dl->AddCircleFilled({a.x + S(10), y + S(16)}, S(9), kColours[i % std::size(kColours)]);
      // Long lines get a smaller font so they fit.
      float size = S(20);
      while (TextSize(BoldFont(), size, groups[i].label).x > b.x - a.x - S(30) && size > S(14)) {
        size -= S(1);
      }
      Label(dl, BoldFont(), size, {a.x + S(30), y + S(4)}, kWhite, groups[i].label);
    }
  }

  void DrawFooter(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
    float x = p0.x + S(38);
    const float y = p1.y - S(54);
    if (confirm_) {
      x += Hint(dl, {x, y}, "A", kAcceptGreen, Tr("Choose", "Ausw\xC3\xA4hlen"), true);
      Hint(dl, {x, y}, "B", kBackRed, Tr("Back", "Zur\xC3\xBC" "ck"), true);
      return;
    }
    const Setting* s = Selected();
    if (s && s->action) {
      x += Hint(dl, {x, y}, "A", kAcceptGreen, Tr("Open", "\xC3\x96" "ffnen"), true);
    } else if (!s || !IsOverview(*s)) {
      x += Hint(dl, {x, y}, "< / >", kHintGrey, Tr("Change", "\xC3\x84ndern"), false);
    }
    x += Hint(dl, {x, y}, "LB / RB", kHintGrey, Tr("Page", "Seite"), false);
    x += Hint(dl, {x, y}, "Y", kYellow, Tr("Save", "Speichern"), true);
    x += Hint(dl, {x, y}, "B", kBackRed, Tr("Close", "Schlie\xC3\x9F" "en"), true);
    bool pending = false;
    for (const SettingsPage& page : g_state.pages) {
      for (const Setting& setting : page.settings) pending = pending || Pending(setting);
    }
    // What happened to the changes, else what still applies later.
    const char* status = nullptr;
    ImU32 status_col = kGold;
    if (Clock::now() - saved_at_ < std::chrono::milliseconds(2500)) {
      status = Tr("Saved", "Gespeichert");
      status_col = Rgba(120, 220, 130);
    } else if (HasUnsaved()) {
      status = Tr("Not saved yet: Y saves", "Noch nicht gespeichert: Y speichert");
    }
    if (status) {
      const ImVec2 ts = TextSize(BoldFont(), S(19), status);
      Label(dl, BoldFont(), S(19), {std::max(x, p1.x - S(38) - ts.x), y + S(4)}, status_col,
            status);
    } else if (pending) {
      const char* note = Tr("Changes marked with a dot apply at the next start",
                            "Mit Punkt markierte \xC3\x84nderungen gelten ab dem n\xC3\xA4"
                            "chsten Start");
      const ImVec2 ts = TextSize(BoldFont(), S(19), note);
      Label(dl, BoldFont(), S(19), {std::max(x, p1.x - S(38) - ts.x), y + S(4)}, kGold, note);
    }
  }

  std::function<void()> on_done_;
  rex::ui::WindowedAppContext* app_context_;
  bool done_ = false;
  bool first_frame_ = true;
  bool waiting_for_release_ = true;
  bool waiting_for_menu_ = false;  // another menu is still closing
  bool confirm_ = false;           // asking about unsaved changes
  int confirm_choice_ = 0;
  std::function<void()> pending_action_;  // an action row waiting for the menu to close
  Clock::time_point created_ = Clock::now();
  Clock::time_point saved_at_{};
  size_t page_ = 0;
  size_t row_ = 0;
  size_t scroll_ = 0;
  Clock::time_point open_time_ = Clock::now();
  std::unordered_map<uint64_t, PadReader> pads_;  // by device
  bool keyboard_view_ = false;  // the controls page shows the keyboard (last used) instead of a pad
};

// UI thread only.
std::unique_ptr<SettingsMenuDialog> g_menu;
rex::ui::ImGuiDrawer* g_drawer = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;

void OpenSettingsMenu() {
  if (g_menu || !g_drawer) return;
  // Over the portal or the player menu: that one closes like with B (the portal menu puts the
  // figure back), and the settings come up once it is gone.
  bool wait = false;
  if (menu_input::MenuOpen()) {
    if (!menu_input::CloseOpenMenu()) return;
    wait = true;
  }
  g_menu = std::make_unique<SettingsMenuDialog>(
      g_drawer,
      [] {
        // The dialog closes inside its own OnDraw; destroy it afterwards.
        g_app_context->CallInUIThreadDeferred([] { g_menu.reset(); });
      },
      g_app_context, wait);
}

}  // namespace

void RegisterSettingsMenu(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* app_context,
                          std::vector<SettingsPage> pages, std::filesystem::path config_path) {
  g_drawer = drawer;
  g_app_context = app_context;
  g_state.pages = std::move(pages);
  g_state.config_path = std::move(config_path);
  for (const SettingsPage& page : g_state.pages) {
    for (const Setting& s : page.settings) {
      if (!s.cvar.empty()) g_state.startup[s.cvar] = rex::cvar::GetFlagByName(s.cvar);
    }
  }
  menu_input::SetButtonHandler(menu_input::ButtonAction::kSettingsMenu,
                               [](DeviceId device, uint32_t) {
                                 if (!g_app_context) return;
                                 g_app_context->CallInUIThread([device] {
                                   g_opened_by_keyboard = false;
                                   g_opened_by_device = device;
                                   OpenSettingsMenu();
                                 });
                               });
  // With the menu open, F4 is read by the menu itself (like B, so unsaved changes are asked about).
  rex::ui::RegisterBind("bind_settings_menu", "F4", "Open or close the settings menu", [] {
    if (g_menu) return;
    g_opened_by_keyboard = true;
    g_opened_by_device.reset();
    OpenSettingsMenu();
  });
}

void SetControllerPicture(ControllerPicture picture) { g_picture = std::move(picture); }

void ShutdownSettingsMenu() { g_menu.reset(); }

}  // namespace skylanders
