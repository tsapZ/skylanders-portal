#include "portal_rex/player_menu.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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

namespace skylanders {
namespace {

using namespace menu_draw;
using rex::input::DeviceId;
using Clock = std::chrono::steady_clock;

constexpr uint32_t kPlayers = rex::input::kMaxGuestUsers;

bool g_german = false;

const char* Tr(const char* english, const char* german) { return g_german ? german : english; }

// Edges of one controller's buttons and stick.
struct PadEdges {
  bool left = false, right = false, close = false, reset = false;
};

class PadState {
 public:
  PadEdges Read(const rex::input::X_INPUT_GAMEPAD& pad) {
    PadEdges e;
    const uint16_t buttons = pad.buttons;
    auto pressed = [&](uint16_t bit) { return (buttons & bit) && !(prev_ & bit); };
    const float lx = float(int16_t(pad.thumb_lx)) / 32767.0f;
    const bool left = (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_LEFT) || lx < -0.55f;
    const bool right = (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT) || lx > 0.55f;
    e.left = left && !prev_left_;
    e.right = right && !prev_right_;
    e.close = pressed(rex::input::X_INPUT_GAMEPAD_A) || pressed(rex::input::X_INPUT_GAMEPAD_B) ||
              pressed(rex::input::X_INPUT_GAMEPAD_START) ||
              (menu_input::IsActionChord(menu_input::ButtonAction::kPlayerMenu, buttons) &&
               !menu_input::IsActionChord(menu_input::ButtonAction::kPlayerMenu, prev_));
    e.reset = pressed(rex::input::X_INPUT_GAMEPAD_Y);
    prev_ = buttons;
    prev_left_ = left;
    prev_right_ = right;
    return e;
  }

  // Nothing counts until every button of the opening press is released.
  static bool Released(const rex::input::X_INPUT_GAMEPAD& pad) {
    return uint16_t(pad.buttons) == 0 && pad.left_trigger < 30 && pad.right_trigger < 30;
  }

 private:
  uint16_t prev_ = 0;
  bool prev_left_ = false, prev_right_ = false;
};

void Move(const menu_input::ConnectedDevice& device, int delta) {
  const uint32_t user = uint32_t((int(device.user) + int(kPlayers) + delta) % int(kPlayers));
  // Back at its own slot, it follows the connection order again.
  menu_input::SetDevicePlayer(
      device.id, device.natural_user == user ? std::nullopt : std::optional<uint32_t>(user));
  REXLOG_INFO("Player menu: {} plays as player {}", device.keyboard ? "keyboard" : device.name,
              user + 1);
}

// A small controller (or keyboard) drawing for the device cards.
void DrawDeviceIcon(ImDrawList* dl, ImVec2 c, float w, bool keyboard, ImU32 col) {
  if (keyboard) {
    const ImVec2 a{c.x - w / 2, c.y - w * 0.28f}, b{c.x + w / 2, c.y + w * 0.28f};
    dl->AddRectFilled(a, b, col, w * 0.10f);
    const ImU32 key = IM_COL32(20, 24, 50, 200);
    for (int row = 0; row < 3; ++row) {
      for (int k = 0; k < 6; ++k) {
        const float kx = a.x + w * 0.08f + k * w * 0.145f, ky = a.y + w * 0.08f + row * w * 0.15f;
        dl->AddRectFilled({kx, ky}, {kx + w * 0.11f, ky + w * 0.10f}, key, w * 0.02f);
      }
    }
    return;
  }
  // Body with two grips, two sticks and four face buttons.
  const float h = w * 0.55f;
  dl->AddRectFilled({c.x - w * 0.36f, c.y - h / 2}, {c.x + w * 0.36f, c.y + h * 0.18f}, col,
                    h * 0.30f);
  dl->AddCircleFilled({c.x - w * 0.32f, c.y + h * 0.12f}, h * 0.36f, col);
  dl->AddCircleFilled({c.x + w * 0.32f, c.y + h * 0.12f}, h * 0.36f, col);
  const ImU32 detail = IM_COL32(20, 24, 50, 220);
  dl->AddCircleFilled({c.x - w * 0.22f, c.y - h * 0.10f}, h * 0.15f, detail);
  dl->AddCircleFilled({c.x + w * 0.10f, c.y + h * 0.16f}, h * 0.13f, detail);
  const ImVec2 f{c.x + w * 0.26f, c.y - h * 0.12f};
  const float d = h * 0.13f;
  dl->AddCircleFilled({f.x, f.y - d}, d * 0.55f, IM_COL32(230, 180, 40, 255));
  dl->AddCircleFilled({f.x - d, f.y}, d * 0.55f, IM_COL32(46, 134, 222, 255));
  dl->AddCircleFilled({f.x + d, f.y}, d * 0.55f, IM_COL32(232, 73, 43, 255));
  dl->AddCircleFilled({f.x, f.y + d}, d * 0.55f, IM_COL32(76, 175, 80, 255));
}

class PlayerMenuDialog final : public rex::ui::ImGuiDialog {
 public:
  PlayerMenuDialog(rex::ui::ImGuiDrawer* drawer, std::function<void()> on_done)
      : ImGuiDialog(drawer), on_done_(std::move(on_done)) {
    g_german = rex::cvar::GetFlagByName("language") == "german";
    menu_input::SetMenuOpen(true);
    REXLOG_INFO("Player menu: opened");
  }

  ~PlayerMenuDialog() override {
    if (!done_) menu_input::SetMenuOpen(false);
  }

  void Close() {
    if (done_) return;
    done_ = true;
    menu_input::SetMenuOpen(false);
    REXLOG_INFO("Player menu: closed");
    if (on_done_) on_done_();
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (done_) return;
    SetScale(io.DisplaySize.y / 1080.0f);
    const std::vector<menu_input::ConnectedDevice> devices = menu_input::ConnectedDevices();
    HandleInput(devices);
    if (done_) return;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    if (first_frame_) {
      ImGui::SetNextWindowFocus();
      first_frame_ = false;
    }
    ImGui::Begin("##player_menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int first_vertex = dl->VtxBuffer.Size;
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    dl->AddRectFilled({0, 0}, {W, H}, Rgba(6, 10, 30, 120));
    const float pw = std::min(W * 0.84f, S(1500));
    const ImVec2 p0{(W - pw) / 2, H * 0.14f}, p1{(W + pw) / 2, H * 0.86f};
    DrawPanel(dl, p0, p1);
    OutlinedLabel(dl, TitleFont(), S(46), {p0.x + S(38), p0.y + S(20)}, kGold,
                  Tr("PLAYERS", "SPIELER"), S(2.5f));
    Label(dl, BoldFont(), S(21), {p0.x + S(40), p0.y + S(84)}, kDim,
          Tr("Each controller picks its player with left / right.",
             "Jeder Controller w\xC3\xA4hlt mit links / rechts seinen Spieler."));
    DrawColumns(dl, {p0.x + S(38), p0.y + S(130)}, {p1.x - S(38), p1.y - S(86)}, devices);
    float x = p0.x + S(38);
    const float y = p1.y - S(54);
    x += Hint(dl, {x, y}, "< / >", kHintGrey, Tr("Change player", "Spieler wechseln"), false);
    x += Hint(dl, {x, y}, "Y", kYellow, Tr("Connection order", "Verbindungsreihenfolge"), true);
    Hint(dl, {x, y}, "A", kAcceptGreen, Tr("Done", "Fertig"), true);
    FadeVertices(dl, first_vertex,
                 std::chrono::duration<float>(Clock::now() - open_time_).count() / 0.15f);
    ImGui::End();
  }

 private:
  void HandleInput(const std::vector<menu_input::ConnectedDevice>& devices) {
    bool released = true;
    bool close = false, reset = false;
    std::vector<std::pair<const menu_input::ConnectedDevice*, int>> moves;
    for (const menu_input::ConnectedDevice& device : devices) {
      if (!device.pollable) continue;
      const auto pad = menu_input::ReadPad(device.id);
      if (!pad) continue;
      released = released && PadState::Released(*pad);
      const PadEdges e = pads_[uint64_t(device.id)].Read(*pad);
      if (waiting_for_release_) continue;
      if (e.left) moves.push_back({&device, -1});
      if (e.right) moves.push_back({&device, +1});
      close |= e.close;
      reset |= e.reset;
    }
    if (waiting_for_release_) {
      if (released && !ImGui::IsKeyDown(ImGuiKey_F9)) waiting_for_release_ = false;
      return;
    }
    // The keyboard moves itself with the arrow keys.
    for (const menu_input::ConnectedDevice& device : devices) {
      if (!device.keyboard) continue;
      if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) moves.push_back({&device, -1});
      if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) moves.push_back({&device, +1});
    }
    close |= ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
             ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_F9, false);
    reset |= ImGui::IsKeyPressed(ImGuiKey_R, false);
    for (const auto& [device, delta] : moves) Move(*device, delta);
    if (reset) {
      for (const menu_input::ConnectedDevice& device : devices) {
        menu_input::SetDevicePlayer(device.id, std::nullopt);
      }
      REXLOG_INFO("Player menu: every controller back in connection order");
    }
    if (close) Close();
  }

  void DrawColumns(ImDrawList* dl, ImVec2 a, ImVec2 b,
                   const std::vector<menu_input::ConnectedDevice>& devices) {
    const float gap = S(18);
    const float col_w = (b.x - a.x - gap * (kPlayers - 1)) / kPlayers;
    for (uint32_t user = 0; user < kPlayers; ++user) {
      const ImVec2 c0{a.x + user * (col_w + gap), a.y}, c1{c0.x + col_w, b.y};
      dl->AddRectFilled(c0, c1, Rgba(255, 255, 255, 14), S(18));
      // Header in the player's colour.
      const ImVec2 h1{c1.x, c0.y + S(56)};
      dl->AddRectFilled(c0, h1, PlayerColor(user), S(18), ImDrawFlags_RoundCornersTop);
      char title[32];
      std::snprintf(title, sizeof(title), Tr("Player %u", "Spieler %u"), user + 1);
      const ImVec2 ts = TextSize(BoldFont(), S(26), title);
      Label(dl, BoldFont(), S(26), {(c0.x + c1.x - ts.x) / 2, c0.y + S(12)}, kWhite, title);
      float y = h1.y + S(16);
      bool any = false;
      for (const menu_input::ConnectedDevice& device : devices) {
        if (device.user != user) continue;
        any = true;
        const ImVec2 d0{c0.x + S(14), y}, d1{c1.x - S(14), y + S(132)};
        GradientRect(dl, d0, d1, Rgba(52, 62, 120), Rgba(28, 34, 76), S(14));
        dl->AddRect(d0, d1, device.reassigned ? kGold : Rgba(255, 255, 255, 50), S(14), 0,
                    device.reassigned ? S(2.5f) : S(1.5f));
        DrawDeviceIcon(dl, {(d0.x + d1.x) / 2, d0.y + S(44)}, S(92), device.keyboard,
                       Rgba(225, 230, 245));
        std::string name = device.keyboard ? Tr("Keyboard", "Tastatur")
                           : device.name.empty() ? Tr("Controller", "Controller")
                                                 : device.name;
        const float wrap = d1.x - d0.x - S(20);
        dl->PushClipRect(d0, d1, true);
        const ImVec2 ns = TextSize(BoldFont(), S(18), name, wrap);
        Label(dl, BoldFont(), S(18), {ns.x < wrap ? (d0.x + d1.x - ns.x) / 2 : d0.x + S(10),
                                      d0.y + S(84)},
              kWhite, name, wrap);
        dl->PopClipRect();
        // Arrows: this card moves with left / right.
        Label(dl, BoldFont(), S(26), {d0.x + S(8), d0.y + S(30)}, Rgba(255, 255, 255, 110), "<");
        Label(dl, BoldFont(), S(26), {d1.x - S(22), d0.y + S(30)}, Rgba(255, 255, 255, 110), ">");
        y = d1.y + S(12);
        if (y > c1.y - S(140)) break;
      }
      if (!any) {
        const char* none = Tr("No controller", "Kein Controller");
        const ImVec2 ns = TextSize(BoldFont(), S(19), none);
        Label(dl, BoldFont(), S(19), {(c0.x + c1.x - ns.x) / 2, h1.y + S(28)},
              Rgba(205, 214, 238, 140), none);
      }
    }
  }

  std::function<void()> on_done_;
  bool done_ = false;
  bool first_frame_ = true;
  bool waiting_for_release_ = true;
  Clock::time_point open_time_ = Clock::now();
  std::unordered_map<uint64_t, PadState> pads_;  // by device
};

// UI thread only.
std::unique_ptr<PlayerMenuDialog> g_menu;
rex::ui::ImGuiDrawer* g_drawer = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;

void OpenPlayerMenu() {
  if (g_menu || !g_drawer || menu_input::MenuOpen()) return;
  g_menu = std::make_unique<PlayerMenuDialog>(g_drawer, [] {
    // The dialog closes inside its own OnDraw; destroy it afterwards.
    g_app_context->CallInUIThreadDeferred([] { g_menu.reset(); });
  });
}

}  // namespace

void RegisterPlayerMenu(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* app_context) {
  g_drawer = drawer;
  g_app_context = app_context;
  menu_input::SetButtonHandler(menu_input::ButtonAction::kPlayerMenu, [](DeviceId, uint32_t) {
    if (g_app_context) g_app_context->CallInUIThread(OpenPlayerMenu);
  });
  rex::ui::RegisterBind("bind_player_menu", "F9",
                        "Open or close the player menu (which controller plays as which player)",
                        [] {
                          if (g_menu) {
                            g_menu->Close();
                          } else {
                            OpenPlayerMenu();
                          }
                        });
}

void ShutdownPlayerMenu() { g_menu.reset(); }

}  // namespace skylanders
