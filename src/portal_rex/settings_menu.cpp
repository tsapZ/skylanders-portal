#include "portal_rex/settings_menu.h"

#include <algorithm>
#include <chrono>
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
  bool accept = false, back = false, prev_page = false, next_page = false;
};

// Edges and key repeat for one controller.
class PadReader {
 public:
  Actions Read(const rex::input::X_INPUT_GAMEPAD& pad) {
    Actions a;
    const uint16_t buttons = pad.buttons;
    auto pressed = [&](uint16_t bit) { return (buttons & bit) && !(prev_ & bit); };
    a.accept = pressed(rex::input::X_INPUT_GAMEPAD_A);
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
  a.prev_page = once(ImGuiKey_Q) || once(ImGuiKey_1);
  a.next_page = once(ImGuiKey_E) || once(ImGuiKey_3);
  return a;
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
}

// Shared by every menu instance: the pages and what was changed this session.
struct SettingsState {
  std::vector<SettingsPage> pages;
  std::filesystem::path config_path;
  std::unordered_map<std::string, std::string> startup;  // cvar -> value at start-up
  std::unordered_map<std::string, std::string> stored;   // cvar -> value written this session
};
SettingsState g_state;  // UI thread only

const std::vector<SettingOption>& BoolOptions() {
  static const std::vector<SettingOption> options = {{"false", "Off", "Aus"}, {"true", "On", "An"}};
  return options;
}

const std::vector<SettingOption>& OptionsOf(const Setting& s) {
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

std::string CurrentValue(const Setting& s) {
  if (s.current) return s.current();
  if (auto it = g_state.stored.find(s.cvar); it != g_state.stored.end()) return it->second;
  return rex::cvar::GetFlagByName(s.cvar);
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

// Changed this session and only applied at the next start.
bool Pending(const Setting& s) {
  if (!ApplyLater(s) || s.cvar.empty()) return false;
  auto stored = g_state.stored.find(s.cvar);
  auto startup = g_state.startup.find(s.cvar);
  return stored != g_state.stored.end() && startup != g_state.startup.end() &&
         !SameValue(stored->second, startup->second);
}

class SettingsMenuDialog final : public rex::ui::ImGuiDialog {
 public:
  SettingsMenuDialog(rex::ui::ImGuiDrawer* drawer, std::function<void()> on_done,
                     rex::ui::WindowedAppContext* app_context)
      : ImGuiDialog(drawer), on_done_(std::move(on_done)), app_context_(app_context) {
    g_german = rex::cvar::GetFlagByName("language") == "german";
    menu_input::SetMenuOpen(true);
    REXLOG_INFO("Settings menu: opened");
  }

  ~SettingsMenuDialog() override {
    if (!done_) menu_input::SetMenuOpen(false);
  }

  void Close() {
    if (done_) return;
    done_ = true;
    menu_input::SetMenuOpen(false);
    REXLOG_INFO("Settings menu: closed");
    if (on_done_) on_done_();
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (done_) return;
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
      if (!waiting_for_release_) Merge(actions, read);
    }
    if (waiting_for_release_) {
      if (released && !ImGui::IsKeyDown(ImGuiKey_F4)) waiting_for_release_ = false;
      return {};
    }
    Merge(actions, ReadKeyboard());
    if (ImGui::IsKeyPressed(ImGuiKey_F4, false)) actions.back = true;
    return actions;
  }

  void HandleInput(const Actions& a) {
    if (a.back) {
      Close();
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
    if (s.preset) {
      for (const auto& [cvar, v] : s.preset(value)) {
        if (StoreSetting(g_state.config_path, cvar, v)) g_state.stored[cvar] = v;
      }
    } else if (StoreSetting(g_state.config_path, s.cvar, value)) {
      g_state.stored[s.cvar] = value;
    }
  }

  // Actions run with the menu closed, after this frame (one may open another overlay).
  void RunAction(const Setting& s) {
    std::function<void()> action = s.action;
    Close();
    if (app_context_) app_context_->CallInUIThreadDeferred(std::move(action));
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
      Label(dl, BoldFont(), S(22), {t0.x + S(20), t0.y + S(10)}, active ? kInk : kDim, title);
      if (ImGui::IsMouseHoveringRect(t0, t1) && ImGui::IsMouseClicked(0)) {
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
      if (ImGui::IsMouseHoveringRect(r0, r1) &&
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
        if (ImGui::IsMouseHoveringRect(r0, r1) && ImGui::IsMouseClicked(0)) {
          RunAction(s);
          return;
        }
        continue;
      }
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
      if (ImGui::IsMouseHoveringRect(v0, v1) && ImGui::IsMouseClicked(0)) {
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
    const std::string& help = Tr(s->help_en, s->help_de);
    if (!help.empty()) {
      Label(dl, BoldFont(), S(21), {a.x + S(24), y}, kDim, help, wrap);
      y += TextSize(BoldFont(), S(21), help, wrap).y + S(22);
    }
    // Every choice, the current one in gold.
    if (!s->action) {
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
    }
  }

  void DrawFooter(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
    float x = p0.x + S(38);
    const float y = p1.y - S(54);
    const Setting* s = Selected();
    if (s && s->action) {
      x += Hint(dl, {x, y}, "A", kAcceptGreen, Tr("Open", "\xC3\x96" "ffnen"), true);
    } else {
      x += Hint(dl, {x, y}, "< / >", kHintGrey, Tr("Change", "\xC3\x84ndern"), false);
    }
    x += Hint(dl, {x, y}, "LB / RB", kHintGrey, Tr("Page", "Seite"), false);
    x += Hint(dl, {x, y}, "B", kBackRed, Tr("Close", "Schlie\xC3\x9F" "en"), true);
    bool pending = false;
    for (const SettingsPage& page : g_state.pages) {
      for (const Setting& setting : page.settings) pending = pending || Pending(setting);
    }
    if (pending) {
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
  size_t page_ = 0;
  size_t row_ = 0;
  size_t scroll_ = 0;
  Clock::time_point open_time_ = Clock::now();
  std::unordered_map<uint64_t, PadReader> pads_;  // by device
};

// UI thread only.
std::unique_ptr<SettingsMenuDialog> g_menu;
rex::ui::ImGuiDrawer* g_drawer = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;

void OpenSettingsMenu() {
  if (g_menu || !g_drawer || menu_input::MenuOpen()) return;
  g_menu = std::make_unique<SettingsMenuDialog>(
      g_drawer,
      [] {
        // The dialog closes inside its own OnDraw; destroy it afterwards.
        g_app_context->CallInUIThreadDeferred([] { g_menu.reset(); });
      },
      g_app_context);
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
  menu_input::SetButtonHandler(menu_input::ButtonAction::kSettingsMenu, [](DeviceId, uint32_t) {
    if (g_app_context) g_app_context->CallInUIThread(OpenSettingsMenu);
  });
  rex::ui::RegisterBind("bind_settings_menu", "F4", "Open or close the settings menu", [] {
    if (g_menu) {
      g_menu->Close();
    } else {
      OpenSettingsMenu();
    }
  });
}

void ShutdownSettingsMenu() { g_menu.reset(); }

}  // namespace skylanders
