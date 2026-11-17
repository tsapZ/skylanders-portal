#include "portal_rex/dev_overlay.h"

#include <cstdio>
#include <memory>
#include <string>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "dev/where.h"

REXCVAR_DEFINE_BOOL(dev_overlay, false, "Developer",
                    "Shows the level, player 1's position and the frame rate in the window's top-left corner "
                    "(for test runs and bug hunts)");

namespace skylanders {
namespace {

class DevOverlay final : public rex::ui::ImGuiDialog {
 public:
  explicit DevOverlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!REXCVAR_GET(dev_overlay)) return;
    std::string place = dev::PlaceText();
    if (place.empty()) place = "(no level yet)";
    char line[320];
    const float fps = io.Framerate;
    std::snprintf(line, sizeof(line), "%s   %.0f FPS  %.1f ms", place.c_str(), fps, fps > 0 ? 1000.0f / fps : 0.0f);
    const float scale = io.DisplaySize.y / 1080.0f;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##dev_overlay", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float size = 22.0f * scale;
    ImFont* font = ImGui::GetFont();
    const ImVec2 text = font->CalcTextSizeA(size, 10000.0f, 0.0f, line);
    const ImVec2 pos{10.0f * scale, 8.0f * scale};
    const float pad = 6.0f * scale;
    dl->AddRectFilled({pos.x - pad, pos.y - pad}, {pos.x + text.x + pad, pos.y + text.y + pad},
                      IM_COL32(0, 0, 0, 160), 4.0f * scale);
    dl->AddText(font, size, pos, IM_COL32(255, 255, 255, 235), line);
    ImGui::End();
  }
};

std::unique_ptr<DevOverlay> g_overlay;  // UI thread only

}  // namespace

void RegisterDevOverlay(rex::ui::ImGuiDrawer* drawer) {
  if (!g_overlay && drawer) g_overlay = std::make_unique<DevOverlay>(drawer);
}

void ShutdownDevOverlay() { g_overlay.reset(); }

}  // namespace skylanders
