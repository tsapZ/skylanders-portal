#include "portal_rex/speaker_overlay.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "portal/software/software_portal.h"
#include "portal/trap_villain.h"
#include "portal_rex/menu_draw.h"
#include "portal_rex/portal_rex.h"

REXCVAR_DEFINE_BOOL(portal_speaker_overlay, true, "Portal",
                    "While a trapped villain talks through the portal speaker (PC speakers), show "
                    "its picture with a ring pulsing to the voice");

namespace skylanders {
namespace {

using namespace menu_draw;
using Clock = std::chrono::steady_clock;

// The villain in a trap on the emulated portal (the first one found), nullopt if none.
std::optional<portal::TrapContents> TrappedVillain() {
  portal::SoftwarePortal* software = GetSoftwarePortal();
  if (!software) return std::nullopt;
  for (int slot = 0; slot < portal::kMaxFigures; ++slot) {
    if (!software->HasFigure(slot)) continue;
    const auto data = software->Figure(slot);
    if (!data) continue;
    if (auto trap = portal::ReadTrapVillain(*data); trap && trap->villain_id != 0) return trap;
  }
  return std::nullopt;
}

class SpeakerOverlay final : public rex::ui::ImGuiDialog {
 public:
  explicit SpeakerOverlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const auto now = Clock::now();
    const float dt = std::clamp(std::chrono::duration<float>(now - last_frame_).count(), 0.0f, 0.1f);
    last_frame_ = now;
    const float level = REXCVAR_GET(portal_speaker_overlay) ? PortalSpeakerLevel() : 0.0f;
    if (level > 0.08f) {
      if (alpha_ <= 0.0f || !villain_) villain_ = TrappedVillain();  // who, once per sound
      shown_until_ = now + std::chrono::milliseconds(700);
    }
    const bool show = now < shown_until_ && villain_;
    alpha_ = std::clamp(alpha_ + (show ? dt / 0.15f : -dt / 0.4f), 0.0f, 1.0f);
    if (alpha_ <= 0.0f) {
      villain_.reset();
      return;
    }
    pulse_ += (level - pulse_) * std::min(1.0f, dt * 18.0f);  // the ring follows the voice smoothly

    SetScale(io.DisplaySize.y / 1080.0f);
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##speaker_overlay", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int first = dl->VtxBuffer.Size;
    const float r = S(54);
    const ImVec2 c{S(96), io.DisplaySize.y - S(190)};
    // The ring: green like a voice chat's, wider and brighter with the voice.
    const float ring = S(4) + pulse_ * S(7);
    dl->AddCircle(c, r + S(6) + pulse_ * S(10), Rgba(67, 181, 129, int(70 + 120 * pulse_)), 64,
                  S(3));
    dl->AddCircle(c, r + ring * 0.5f, Rgba(67, 181, 129, 255), 64, ring);
    // The villain's picture from the game, round; else its first letter on dark blue.
    const portal::TrapVillain* info = portal::FindTrapVillain(villain_->villain_id, villain_->variant);
    const std::string name = info ? std::string(info->name) : std::string();
    if (ImTextureID t = GameArtTexture("villain:" + std::to_string(villain_->villain_id))) {
      dl->AddImageRounded(t, {c.x - r, c.y - r}, {c.x + r, c.y + r}, {0, 0}, {1, 1},
                          IM_COL32_WHITE, r);
    } else {
      dl->AddCircleFilled(c, r, Rgba(16, 22, 60, 235), 64);
      const std::string letter = name.empty() ? "?" : name.substr(0, 1);
      const ImVec2 ts = TextSize(TitleFont(), S(48), letter);
      Label(dl, TitleFont(), S(48), {c.x - ts.x / 2, c.y - ts.y / 2}, kGold, letter);
    }
    // The name on a dark pill under it.
    if (!name.empty()) {
      const ImVec2 ts = TextSize(BoldFont(), S(22), name);
      const ImVec2 a{c.x - ts.x / 2 - S(14), c.y + r + S(16)}, b{c.x + ts.x / 2 + S(14), a.y + ts.y + S(10)};
      dl->AddRectFilled(a, b, Rgba(16, 22, 60, 225), S(14));
      Label(dl, BoldFont(), S(22), {a.x + S(14), a.y + S(5)}, kWhite, name);
    }
    FadeVertices(dl, first, alpha_);
    ImGui::End();
  }

 private:
  Clock::time_point last_frame_ = Clock::now();
  Clock::time_point shown_until_{};
  float alpha_ = 0.0f, pulse_ = 0.0f;
  std::optional<portal::TrapContents> villain_;
};

std::unique_ptr<SpeakerOverlay> g_overlay;  // UI thread only

}  // namespace

void RegisterSpeakerOverlay(rex::ui::ImGuiDrawer* drawer) {
  if (!g_overlay && drawer) g_overlay = std::make_unique<SpeakerOverlay>(drawer);
}

void ShutdownSpeakerOverlay() { g_overlay.reset(); }

}  // namespace skylanders
