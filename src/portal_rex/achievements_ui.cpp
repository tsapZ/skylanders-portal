#include "portal_rex/achievements_ui.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <rex/input/input.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/achievement_manager.h>
#include <rex/system/kernel_state.h>
#include <rex/system/util/xdbf_utils.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/overlay/achievement_icon_cache.h>
#include <rex/ui/overlay/achievement_notification.h>
#include <rex/ui/windowed_app_context.h>

#include "portal_rex/menu_draw.h"
#include "portal_rex/portal_menu_input.h"
#include "portal_rex/utf8_path.h"

namespace skylanders {
namespace {

using namespace menu_draw;
using Clock = std::chrono::steady_clock;
using rex::system::AchievementEvent;
using rex::system::AchievementInfo;

const char* Tr(const char* en, const char* de) { return GermanText() ? de : en; }

ImTextureID IconTexture(rex::ui::AchievementIconCache& cache, const AchievementInfo& a) {
  return reinterpret_cast<ImTextureID>(cache.GetIcon(a));
}

// ---- The pop-up -----------------------------------------------------------------------------
// Like the Xbox 360's: a round icon in the bottom middle opens sideways into a dark bar, holds,
// and closes again. Queued, one at a time.

constexpr float kOpenSeconds = 0.35f, kHoldSeconds = 4.6f, kCloseSeconds = 0.35f;
constexpr float kToastSeconds = kOpenSeconds + kHoldSeconds + kCloseSeconds;

class AchievementToast final : public rex::ui::AchievementNotificationDialog {
 public:
  AchievementToast(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
                   rex::Runtime* runtime)
      : AchievementNotificationDialog(drawer), icons_(immediate_drawer, runtime) {
    // Registered only while there is something to show, as the SDK's own toast (ReXApp adds it
    // again on every event): a registered dialog makes the presenter paint from the UI thread.
    drawer->RemoveDialog(this);
  }

  void Push(const AchievementEvent& event) override {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back({event, std::nullopt});
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    const auto now = Clock::now();
    Pending toast;
    float age = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      while (!queue_.empty()) {
        if (!queue_.front().since) queue_.front().since = now;
        age = std::chrono::duration<float>(now - *queue_.front().since).count();
        if (age < kToastSeconds) break;
        queue_.pop_front();
      }
      if (queue_.empty()) {
        imgui_drawer()->RemoveDialog(this);
        return;
      }
      toast = queue_.front();
    }
    SetScale(io.DisplaySize.y / 1080.0f);
    // How far the bar is open: 0 = only the round icon, 1 = full width.
    float open = 1;
    if (age < kOpenSeconds) open = age / kOpenSeconds;
    if (age > kToastSeconds - kCloseSeconds) open = (kToastSeconds - age) / kCloseSeconds;
    open = std::clamp(open, 0.0f, 1.0f);
    open = open * open * (3 - 2 * open);
    const float alpha = std::clamp(std::min(age, kToastSeconds - age) / 0.15f, 0.0f, 1.0f);

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##achievement_toast", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int first_vertex = dl->VtxBuffer.Size;

    const AchievementInfo& a = toast.event.achievement;
    const char* title = Tr("Achievement unlocked", "Erfolg freigeschaltet");
    char line[512];
    std::snprintf(line, sizeof(line), "%uG - %s", unsigned(a.gamerscore), a.label.c_str());
    const float h = S(84), icon_r = h / 2;
    const float text_w =
        std::max(TextSize(BoldFont(), S(20), title).x, TextSize(BoldFont(), S(26), line).x);
    const float full_w = std::min(io.DisplaySize.x * 0.9f, h + S(28) + text_w + S(36));
    const float w = h + (full_w - h) * open;
    const float cx = io.DisplaySize.x / 2, top = io.DisplaySize.y * 0.82f;
    const ImVec2 p0{cx - w / 2, top}, p1{cx + w / 2, top + h};
    // The bar: near-black with a soft light edge, like the 360's.
    dl->AddRectFilled({p0.x, p0.y + S(5)}, {p1.x, p1.y + S(5)}, Rgba(0, 0, 0, 110), h / 2);
    GradientRect(dl, p0, p1, Rgba(62, 64, 70, 240), Rgba(22, 23, 27, 245), h / 2);
    dl->AddRect(p0, p1, Rgba(255, 255, 255, 60), h / 2, 0, S(1.5f));
    // The icon in its ring at the left end.
    const ImVec2 ic{p0.x + icon_r, p0.y + icon_r};
    dl->AddCircleFilled(ic, icon_r - S(6), Rgba(18, 18, 22), 48);
    if (ImTextureID t = IconTexture(icons_, a)) {
      const float r = icon_r - S(12);
      dl->AddImageRounded(t, {ic.x - r, ic.y - r}, {ic.x + r, ic.y + r}, {0, 0}, {1, 1},
                          IM_COL32_WHITE, r);
    } else {
      const ImVec2 ts = TextSize(TitleFont(), S(34), "G");
      Label(dl, TitleFont(), S(34), {ic.x - ts.x / 2, ic.y - ts.y / 2}, kWhite, "G");
    }
    dl->AddCircle(ic, icon_r - S(6), Rgba(120, 200, 90), 48, S(3));
    // The text, shown once the bar has opened far enough for it.
    if (open > 0.6f) {
      const float x = p0.x + h + S(14);
      dl->PushClipRect({x, p0.y}, {p1.x - S(18), p1.y}, true);
      Label(dl, BoldFont(), S(20), {x, p0.y + S(12)}, Rgba(200, 204, 214), title);
      Label(dl, BoldFont(), S(26), {x, p0.y + S(38)}, kWhite, line);
      dl->PopClipRect();
    }
    FadeVertices(dl, first_vertex, alpha);
    ImGui::End();
  }

 private:
  struct Pending {
    AchievementEvent event;
    std::optional<Clock::time_point> since;
  };
  std::mutex mutex_;
  std::deque<Pending> queue_;
  rex::ui::AchievementIconCache icons_;
};

// ---- The list -------------------------------------------------------------------------------

struct Edges {
  bool up = false, down = false, page_up = false, page_down = false, close = false;
};

// Edges and key repeat for one controller.
class PadEdges {
 public:
  Edges Read(const rex::input::X_INPUT_GAMEPAD& pad) {
    Edges e;
    const uint16_t buttons = pad.buttons;
    auto pressed = [&](uint16_t bit) { return (buttons & bit) && !(prev_ & bit); };
    const float ly = float(int16_t(pad.thumb_ly)) / 32767.0f;
    const bool up = (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_UP) || ly > 0.55f;
    const bool down = (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_DOWN) || ly < -0.55f;
    const auto now = Clock::now();
    auto repeat = [&](bool held, bool& was, Clock::time_point& next) {
      bool fire = false;
      if (held && !was) {
        fire = true;
        next = now + std::chrono::milliseconds(380);
      } else if (held && now >= next) {
        fire = true;
        next = now + std::chrono::milliseconds(90);
      }
      was = held;
      return fire;
    };
    e.up = repeat(up, up_, up_next_);
    e.down = repeat(down, down_, down_next_);
    e.page_up = pressed(rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER);
    e.page_down = pressed(rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER);
    e.close = pressed(rex::input::X_INPUT_GAMEPAD_B) || pressed(rex::input::X_INPUT_GAMEPAD_BACK) ||
              pressed(rex::input::X_INPUT_GAMEPAD_START);
    prev_ = buttons;
    return e;
  }

  static bool Released(const rex::input::X_INPUT_GAMEPAD& pad) {
    return uint16_t(pad.buttons) == 0 && pad.left_trigger < 30 && pad.right_trigger < 30;
  }

 private:
  uint16_t prev_ = 0;
  bool up_ = false, down_ = false;
  Clock::time_point up_next_, down_next_;
};

// "21.10.2026" / "10/21/2026" from a FILETIME.
std::string UnlockDate(uint64_t filetime) {
  if (!filetime) return {};
  const std::time_t t = std::time_t(filetime / 10000000ull - 11644473600ull);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char text[32];
  if (GermanText()) {
    std::snprintf(text, sizeof(text), "%02d.%02d.%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
  } else {
    std::snprintf(text, sizeof(text), "%02d/%02d/%04d", tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900);
  }
  return text;
}

class AchievementsMenu final : public rex::ui::ImGuiDialog {
 public:
  AchievementsMenu(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
                   rex::Runtime* runtime, rex::system::AchievementManager* manager,
                   std::function<void()> on_done)
      : ImGuiDialog(drawer), icons_(immediate_drawer, runtime), on_done_(std::move(on_done)) {
    for (const AchievementInfo& a : manager->ListAchievements()) {
      Row row{a, manager->IsUnlocked(a.id), manager->GetUnlockTime(a.id)};
      rows_.push_back(std::move(row));
    }
    // Unlocked first (newest on top), then the rest in the game's order.
    std::stable_sort(rows_.begin(), rows_.end(), [](const Row& x, const Row& y) {
      if (x.unlocked != y.unlocked) return x.unlocked;
      return x.unlocked && x.time > y.time;
    });
    menu_input::SetMenuOpen(true);
    menu_input::SetMenuCloser([this] { Close(); });
    REXLOG_INFO("Achievements: list opened ({} of {} unlocked)", Unlocked(), rows_.size());
  }

  ~AchievementsMenu() override {
    if (!done_) menu_input::SetMenuOpen(false);
  }

  void Close() {
    if (done_) return;
    done_ = true;
    menu_input::SetMenuOpen(false);
    REXLOG_INFO("Achievements: list closed");
    if (on_done_) on_done_();
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (done_) return;
    SetScale(io.DisplaySize.y / 1080.0f);
    HandleInput();
    if (done_) return;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    if (first_frame_) {
      ImGui::SetNextWindowFocus();
      first_frame_ = false;
    }
    ImGui::Begin("##achievements_menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int first_vertex = dl->VtxBuffer.Size;
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    dl->AddRectFilled({0, 0}, {W, H}, Rgba(6, 10, 30, 120));
    const float pw = std::min(W * 0.84f, S(1400));
    const ImVec2 p0{(W - pw) / 2, H * 0.08f}, p1{(W + pw) / 2, H * 0.92f};
    DrawPanel(dl, p0, p1);
    OutlinedLabel(dl, TitleFont(), S(46), {p0.x + S(38), p0.y + S(20)}, kGold,
                  Tr("ACHIEVEMENTS", "ERFOLGE"), S(2.5f));
    // The totals at the top right.
    unsigned score = 0, total = 0;
    for (const Row& r : rows_) {
      total += r.info.gamerscore;
      if (r.unlocked) score += r.info.gamerscore;
    }
    char summary[96];
    std::snprintf(summary, sizeof(summary), Tr("%zu of %zu  -  %uG of %uG", "%zu von %zu  -  %uG von %uG"),
                  Unlocked(), rows_.size(), score, total);
    const ImVec2 ss = TextSize(BoldFont(), S(24), summary);
    Label(dl, BoldFont(), S(24), {p1.x - S(38) - ss.x, p0.y + S(34)}, kWhite, summary);

    const ImVec2 l0{p0.x + S(38), p0.y + S(100)}, l1{p1.x - S(38), p1.y - S(86)};
    DrawList(dl, l0, l1);
    float x = p0.x + S(38);
    const float y = p1.y - S(54);
    x += Hint(dl, {x, y}, Tr("Up / Down", "Hoch / Runter"), kHintGrey,
              Tr("Scroll", "Bl\xC3\xA4ttern"), false);
    x += Hint(dl, {x, y}, "LB / RB", kHintGrey, Tr("Page", "Seite"), false, "PgUp / PgDn");
    Hint(dl, {x, y}, "B", kBackRed, Tr("Close", "Schlie\xC3\x9F" "en"), true, "Esc");
    FadeVertices(dl, first_vertex, std::chrono::duration<float>(Clock::now() - open_time_).count() / 0.15f);
    ImGui::End();
  }

 private:
  struct Row {
    AchievementInfo info;
    bool unlocked = false;
    uint64_t time = 0;
  };

  size_t Unlocked() const {
    return size_t(std::count_if(rows_.begin(), rows_.end(), [](const Row& r) { return r.unlocked; }));
  }

  void HandleInput() {
    Edges e;
    bool released = true;
    for (const menu_input::ConnectedDevice& device : menu_input::ConnectedDevices()) {
      if (!device.pollable) continue;
      const auto pad = menu_input::ReadPad(device.id);
      if (!pad) continue;
      released = released && PadEdges::Released(*pad);
      const Edges r = pads_[uint64_t(device.id)].Read(*pad);
      if (waiting_for_release_) continue;
      if (r.up || r.down || r.page_up || r.page_down || r.close) UseKeyboardHints(device.keyboard);
      e.up |= r.up;
      e.down |= r.down;
      e.page_up |= r.page_up;
      e.page_down |= r.page_down;
      e.close |= r.close;
    }
    if (waiting_for_release_) {
      if (released) waiting_for_release_ = false;
      return;
    }
    e.up |= ImGui::IsKeyPressed(ImGuiKey_UpArrow, true);
    e.down |= ImGui::IsKeyPressed(ImGuiKey_DownArrow, true);
    e.page_up |= ImGui::IsKeyPressed(ImGuiKey_PageUp, true);
    e.page_down |= ImGui::IsKeyPressed(ImGuiKey_PageDown, true);
    e.close |= ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
               ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel > 0) e.up = true;
    if (wheel < 0) e.down = true;
    if (rows_.empty()) {
      if (e.close) Close();
      return;
    }
    const int n = int(rows_.size());
    if (e.up) focus_ = std::max(0, focus_ - 1);
    if (e.down) focus_ = std::min(n - 1, focus_ + 1);
    if (e.page_up) focus_ = std::max(0, focus_ - visible_);
    if (e.page_down) focus_ = std::min(n - 1, focus_ + visible_);
    if (e.close) Close();
  }

  void DrawList(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    if (rows_.empty()) {
      Label(dl, BoldFont(), S(24), a, kDim,
            Tr("This game lists no achievements.", "Dieses Spiel hat keine Erfolge."));
      return;
    }
    const float row_h = S(96), gap = S(10);
    visible_ = std::max(1, int((b.y - a.y + gap) / (row_h + gap)));
    if (focus_ < scroll_) scroll_ = focus_;
    if (focus_ >= scroll_ + visible_) scroll_ = focus_ - visible_ + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, int(rows_.size()) - visible_));
    const float text_w = b.x - a.x - S(240);
    for (int i = scroll_; i < int(rows_.size()) && i < scroll_ + visible_; ++i) {
      const Row& r = rows_[size_t(i)];
      const float y = a.y + float(i - scroll_) * (row_h + gap);
      const ImVec2 r0{a.x, y}, r1{b.x - S(14), y + row_h};
      const bool focused = i == focus_;
      GradientRect(dl, r0, r1, r.unlocked ? Rgba(52, 66, 128, 235) : Rgba(30, 34, 62, 225),
                   r.unlocked ? Rgba(34, 44, 94, 235) : Rgba(22, 24, 46, 225), S(16));
      dl->AddRect(r0, r1, focused ? kGold : Rgba(255, 255, 255, 30), S(16), 0,
                  focused ? S(3) : S(1.5f));
      // The icon, greyed while locked.
      const float is = row_h - S(20);
      const ImVec2 i0{r0.x + S(10), r0.y + S(10)}, i1{i0.x + is, i0.y + is};
      dl->AddRectFilled(i0, i1, Rgba(10, 12, 24), S(10));
      if (ImTextureID t = IconTexture(icons_, r.info)) {
        dl->AddImageRounded(t, i0, i1, {0, 0}, {1, 1},
                            r.unlocked ? IM_COL32_WHITE : Rgba(110, 110, 125, 150), S(10));
      }
      const float tx = i1.x + S(18);
      Label(dl, BoldFont(), S(24), {tx, r0.y + S(12)}, r.unlocked ? kWhite : kDim, r.info.label,
            text_w);
      const std::string& text = !r.unlocked && !r.info.unachieved_description.empty()
                                    ? r.info.unachieved_description
                                    : r.info.description;
      dl->PushClipRect({tx, r0.y}, {tx + text_w, r1.y - S(6)}, true);
      Label(dl, BoldFont(), S(18), {tx, r0.y + S(44)}, Rgba(185, 194, 222, r.unlocked ? 255 : 170),
            text, text_w);
      dl->PopClipRect();
      // The gamerscore, and the day it was unlocked.
      char score[16];
      std::snprintf(score, sizeof(score), "%uG", unsigned(r.info.gamerscore));
      const ImVec2 gs = TextSize(TitleFont(), S(28), score);
      Label(dl, TitleFont(), S(28), {r1.x - S(20) - gs.x, r0.y + S(14)},
            r.unlocked ? kGold : Rgba(150, 156, 180), score);
      const std::string when = r.unlocked ? UnlockDate(r.time) : std::string(Tr("Locked", "Gesperrt"));
      const ImVec2 ws = TextSize(BoldFont(), S(17), when);
      Label(dl, BoldFont(), S(17), {r1.x - S(20) - ws.x, r0.y + S(58)}, kDim, when);
    }
    // A scroll bar when not everything fits.
    if (int(rows_.size()) > visible_) {
      const float track = b.y - a.y;
      const float knob = std::max(S(40), track * float(visible_) / float(rows_.size()));
      const float at = (track - knob) * float(scroll_) / float(int(rows_.size()) - visible_);
      dl->AddRectFilled({b.x - S(6), a.y}, {b.x, b.y}, Rgba(255, 255, 255, 20), S(3));
      dl->AddRectFilled({b.x - S(6), a.y + at}, {b.x, a.y + at + knob}, kGold, S(3));
    }
  }

  rex::ui::AchievementIconCache icons_;
  std::function<void()> on_done_;
  std::vector<Row> rows_;
  int focus_ = 0, scroll_ = 0, visible_ = 1;
  bool done_ = false;
  bool first_frame_ = true;
  bool waiting_for_release_ = true;
  Clock::time_point open_time_ = Clock::now();
  std::unordered_map<uint64_t, PadEdges> pads_;
};

// UI thread only.
std::unique_ptr<AchievementsMenu> g_menu;
rex::ui::ImGuiDrawer* g_drawer = nullptr;
rex::ui::ImmediateDrawer* g_immediate_drawer = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;
rex::Runtime* g_runtime = nullptr;
std::function<rex::system::AchievementManager*()> g_manager;
// Set once the four above are (RegisterAchievementsMenu), for the launcher's cache thread, which
// only reads them after that.
std::atomic<bool> g_registered{false};
std::atomic<bool> g_cache_stop{false};

// One field of achievements.txt: no tabs or line breaks inside.
std::string CacheField(std::string text) {
  for (char& c : text) {
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  }
  return text;
}

}  // namespace

std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementToast(
    rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
    rex::Runtime* runtime) {
  if (!drawer || !immediate_drawer || !runtime) return nullptr;
  return std::make_unique<AchievementToast>(drawer, immediate_drawer, runtime);
}

void RegisterAchievementsMenu(rex::ui::ImGuiDrawer* drawer,
                              rex::ui::ImmediateDrawer* immediate_drawer,
                              rex::ui::WindowedAppContext* app_context, rex::Runtime* runtime,
                              std::function<rex::system::AchievementManager*()> manager) {
  g_drawer = drawer;
  g_immediate_drawer = immediate_drawer;
  g_app_context = app_context;
  g_runtime = runtime;
  g_manager = std::move(manager);
  g_registered.store(true, std::memory_order_release);
}

void ToggleAchievementsMenu() {
  if (g_menu) {
    g_menu->Close();
    return;
  }
  if (!g_drawer || !g_immediate_drawer || !g_runtime || !g_manager || menu_input::MenuOpen()) {
    return;
  }
  rex::system::AchievementManager* manager = g_manager();
  if (!manager) return;
  g_menu = std::make_unique<AchievementsMenu>(g_drawer, g_immediate_drawer, g_runtime, manager, [] {
    // The dialog closes inside its own OnDraw; destroy it afterwards.
    if (g_app_context) g_app_context->CallInUIThreadDeferred([] { g_menu.reset(); });
  });
}

void ShutdownAchievementsMenu() {
  g_cache_stop = true;
  g_menu.reset();
}

void StartAchievementCache(std::filesystem::path dir) {
  std::thread([dir] {
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;
    // The list is the kernel's from launch on, once the game's module is loaded: asked every
    // second for up to two minutes.
    rex::system::AchievementManager* manager = nullptr;
    std::vector<rex::system::AchievementInfo> list;
    for (int i = 0; i < 120 && !g_cache_stop; ++i) {
      std::this_thread::sleep_for(1s);
      if (!g_registered.load(std::memory_order_acquire)) continue;
      manager = g_manager();
      if (manager) list = manager->ListAchievements();
      if (!list.empty()) break;
    }
    if (list.empty() || g_cache_stop || !g_runtime || !g_runtime->kernel_state()) return;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return;
    const auto db = g_runtime->kernel_state()->title_xdbf();
    char title[16];
    std::snprintf(title, sizeof(title), "%08X", g_runtime->kernel_state()->title_id());
    std::string text = std::string("title\t") + title + "\n";
    size_t pictures = 0;
    for (const rex::system::AchievementInfo& a : list) {
      std::string picture;
      if (db.is_valid() && a.image_id) {
        const auto block = db.GetEntry(rex::system::util::XdbfSection::kImage, uint64_t(a.image_id));
        if (block && block.size > 0) {
          picture = "ach-" + std::to_string(a.image_id) + ".png";
          std::ofstream out(dir / picture, std::ios::binary | std::ios::trunc);
          out.write(reinterpret_cast<const char*>(block.buffer), std::streamsize(block.size));
          if (out) ++pictures;
          else picture.clear();
        }
      }
      text += std::to_string(a.id) + "\t" + std::to_string(a.gamerscore) + "\t" + picture + "\t" +
              CacheField(a.label) + "\t" + CacheField(a.description) + "\t" +
              CacheField(a.unachieved_description) + "\n";
    }
    const fs::path temp = dir / "achievements.txt.new";
    {
      std::ofstream out(temp, std::ios::binary | std::ios::trunc);
      out << text;
      if (!out) return;
    }
    fs::rename(temp, dir / "achievements.txt", ec);
    if (!ec) {
      REXLOG_INFO("Achievements: {} ({} pictures) for the launcher in {}", list.size(), pictures,
                  Utf8Path(dir));
    }
  }).detach();
}

Setting AchievementsSetting() {
  Setting s;
  s.en = "Achievements";
  s.de = "Erfolge";
  s.help_en = "Every achievement of the game, the unlocked ones first (also with F7).";
  s.help_de = "Alle Erfolge des Spiels, die freigeschalteten zuerst (auch mit F7).";
  s.action = [] { ToggleAchievementsMenu(); };
  return s;
}

}  // namespace skylanders
