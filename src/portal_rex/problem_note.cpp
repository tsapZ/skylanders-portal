#include "portal_rex/problem_note.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <rex/input/input.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/windowed_app_context.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "dev/where.h"
#include "portal_rex/menu_draw.h"
#include "portal_rex/png_file.h"
#include "portal_rex/portal_menu.h"
#include "portal_rex/portal_menu_input.h"
#include "portal_rex/utf8_path.h"

namespace skylanders {
namespace {

using namespace menu_draw;
using Clock = std::chrono::steady_clock;

const char* Tr(const char* en, const char* de) { return GermanText() ? de : en; }

// When the program started, for "running for" in the note.
const Clock::time_point g_started = Clock::now();

struct Kind {
  const char* en;
  const char* de;
};
constexpr Kind kKinds[] = {
    {"Picture or graphics", "Grafikfehler"},
    {"Sound", "Ton"},
    {"Controls", "Steuerung"},
    {"Portal or figures", "Portal oder Figuren"},
    {"Freezes or crashes", "H\xC3\xA4ngt oder st\xC3\xBCrzt ab"},
    {"Saves", "Spielstand"},
    {"Something else", "Etwas anderes"},
};

// The program's own name ("swapforcerecompiled"), which says the game.
std::string ProgramName() {
#if defined(_WIN32)
  wchar_t path[MAX_PATH] = {};
  if (GetModuleFileNameW(nullptr, path, MAX_PATH)) return Utf8Path(std::filesystem::path(path).stem());
#endif
  return "game";
}

// Edges for one controller: up / down with repeat, A, B.
class Pad {
 public:
  struct Edges {
    bool up = false, down = false, accept = false, back = false;
  };
  Edges Read(const rex::input::X_INPUT_GAMEPAD& pad) {
    Edges e;
    const uint16_t buttons = pad.buttons;
    auto pressed = [&](uint16_t bit) { return (buttons & bit) && !(prev_ & bit); };
    const float ly = float(int16_t(pad.thumb_ly)) / 32767.0f;
    const bool up_held = (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_UP) || ly > 0.55f;
    const bool down_held = (buttons & rex::input::X_INPUT_GAMEPAD_DPAD_DOWN) || ly < -0.55f;
    // The first read only notes what is held already (the A that chose "Problem melden" in the
    // settings menu is still down), so only new presses count.
    if (!seen_) {
      seen_ = true;
      prev_ = buttons;
      up_ = up_held;
      down_ = down_held;
      return e;
    }
    const auto now = Clock::now();
    auto repeat = [&](bool held, bool& was, Clock::time_point& next) {
      bool fire = false;
      if (held && !was) {
        fire = true;
        next = now + std::chrono::milliseconds(380);
      } else if (held && now >= next) {
        fire = true;
        next = now + std::chrono::milliseconds(110);
      }
      was = held;
      return fire;
    };
    e.up = repeat(up_held, up_, up_next_);
    e.down = repeat(down_held, down_, down_next_);
    e.accept = pressed(rex::input::X_INPUT_GAMEPAD_A);
    e.back = pressed(rex::input::X_INPUT_GAMEPAD_B);
    prev_ = buttons;
    return e;
  }
 private:
  bool seen_ = false;
  uint16_t prev_ = 0;
  bool up_ = false, down_ = false;
  Clock::time_point up_next_, down_next_;
};

class ProblemNoteDialog final : public rex::ui::ImGuiDialog {
 public:
  ProblemNoteDialog(rex::ui::ImGuiDrawer* drawer, std::function<void()> on_done)
      : ImGuiDialog(drawer), on_done_(std::move(on_done)) {
    // The picture first, before anything of the dialog could be in it (the guest output has no
    // menus anyway).
    has_picture_ = CaptureGamePicture(picture_w_, picture_h_, picture_);
    portal_ = PortalFiguresText();
    place_ = dev::PlaceText();
    menu_input::SetMenuOpen(true);
    menu_input::SetMenuCloser([this] { Close(); });
    REXLOG_INFO("Problem note: opened ({})", has_picture_ ? "picture kept" : "no picture");
  }
  ~ProblemNoteDialog() override {
    if (!done_) menu_input::SetMenuOpen(false);
  }

  void Close() {
    if (done_) return;
    done_ = true;
    menu_input::SetMenuOpen(false);
    if (on_done_) on_done_();
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (done_) return;
    SetScale(io.DisplaySize.y / 1080.0f);
    HandlePads();
    if (done_) return;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##problem_note", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    dl->AddRectFilled({0, 0}, {W, H}, Rgba(6, 10, 30, 140));
    const float pw = std::min(W * 0.8f, S(980));
    const float row_h = S(56);
    const float ph = S(250) + float(std::size(kKinds)) * (row_h + S(8));
    const ImVec2 p0{(W - pw) / 2, (H - ph) / 2}, p1{(W + pw) / 2, (H + ph) / 2};
    DrawPanel(dl, p0, p1);
    OutlinedLabel(dl, TitleFont(), S(40), {p0.x + S(36), p0.y + S(22)}, kGold,
                  Tr("REPORT A PROBLEM", "PROBLEM MELDEN"), S(2.0f));
    Label(dl, BoldFont(), S(20), {p0.x + S(38), p0.y + S(80)}, kDim,
          has_picture_ ? Tr("The picture of this moment is kept. What's wrong?",
                            "Das Bild von gerade eben wird gespeichert. Was passt nicht?")
                       : Tr("What's wrong?", "Was passt nicht?"),
          pw - S(76));
    // A few words with the keyboard (Enter saves too).
    ImGui::SetCursorScreenPos({p0.x + S(38), p0.y + S(118)});
    ImGui::PushItemWidth(pw - S(76));
    if (first_frame_) {
      ImGui::SetKeyboardFocusHere();
      first_frame_ = false;
    }
    const bool enter = ImGui::InputTextWithHint("##note", Tr("A few words (keyboard, optional)",
                                                              "Kurz beschreiben (Tastatur, freiwillig)"),
                                                note_, sizeof(note_), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    float y = p0.y + S(176);
    for (int i = 0; i < int(std::size(kKinds)); ++i) {
      const ImVec2 r0{p0.x + S(36), y}, r1{p1.x - S(36), y + row_h};
      const bool hovered = ImGui::IsMouseHoveringRect(r0, r1);
      if (hovered && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) kind_ = i;
      const bool chosen = i == kind_;
      dl->AddRectFilled(r0, r1, chosen ? Rgba(60, 70, 140, 240) : Rgba(24, 30, 70, 220), S(14));
      if (chosen) dl->AddRect(r0, r1, kGold, S(14), 0, S(3));
      Label(dl, BoldFont(), S(24), {r0.x + S(20), CapCentredY((r0.y + r1.y) / 2, S(24))}, kWhite,
            Tr(kKinds[i].en, kKinds[i].de));
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        kind_ = i;
        save_ = true;
      }
      y += row_h + S(8);
    }
    float x = p0.x + S(38);
    const float hy = p1.y - S(48);
    x += Hint(dl, {x, hy}, "A", kAcceptGreen, Tr("Save", "Speichern"), true);
    Hint(dl, {x, hy}, "B", kBackRed, Tr("Cancel", "Abbrechen"), true, "Esc");
    ImGui::End();
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) kind_ = std::max(0, kind_ - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) kind_ = std::min(int(std::size(kKinds)) - 1, kind_ + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) Close();
    if (enter) save_ = true;
    if (save_ && !done_) Save();
  }

 private:
  void HandlePads() {
    Pad::Edges e;
    for (const menu_input::ConnectedDevice& device : menu_input::ConnectedDevices()) {
      if (!device.pollable) continue;
      const auto pad = menu_input::ReadPad(device.id);
      if (!pad) continue;
      const Pad::Edges r = pads_[uint64_t(device.id)].Read(*pad);
      e.up |= r.up;
      e.down |= r.down;
      e.accept |= r.accept;
      e.back |= r.back;
    }
    if (e.up) kind_ = std::max(0, kind_ - 1);
    if (e.down) kind_ = std::min(int(std::size(kKinds)) - 1, kind_ + 1);
    if (e.back) Close();
    if (e.accept) save_ = true;
  }

  void Save() {
    save_ = false;
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::current_path(ec) / "logs";
    std::filesystem::create_directories(dir, ec);
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d-%H%M%S", &tm);
    const std::string base = std::string("problem-") + stamp;
    std::string picture_name;
    if (has_picture_ && WritePng(dir / (base + ".png"), picture_w_, picture_h_, picture_.data())) {
      picture_name = base + ".png";
    }
    const auto ran = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - g_started).count();
    char when[64];
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tm);
    char running[32];
    std::snprintf(running, sizeof(running), "%lld:%02lld:%02lld", static_cast<long long>(ran / 3600),
                  static_cast<long long>(ran / 60 % 60), static_cast<long long>(ran % 60));
    const Kind& kind = kKinds[kind_];
    std::ofstream out(dir / (base + ".txt"), std::ios::binary);
    out << "Problem noted in the game\n"
        << "Game: " << ProgramName() << "\n"
        << "When: " << when << " (running for " << running << ")\n"
        << "Kind: " << kind.en << "\n"
        << "Note: " << note_ << "\n"
        << "On the portal: " << (portal_.empty() ? "nothing" : portal_) << "\n";
    // Where the player was when the note opened (games that report it, dev/where.h).
    if (!place_.empty()) out << "Where: " << place_ << "\n";
    out << "Picture: " << (picture_name.empty() ? "none" : picture_name) << "\n";
    out.close();
    REXLOG_WARN("Problem noted by the player: {}{}{} ({}.txt)", kind.en, note_[0] ? ": " : "", note_, base);
    if (!place_.empty()) REXLOG_INFO("Where (problem note): {}", place_);
    ShowPortalToast(Tr("Noted. Report a problem in Core of Light sends it along.",
                       "Notiert. Problem melden im Hub schickt es mit."));
    Close();
  }

  std::function<void()> on_done_;
  bool done_ = false;
  bool first_frame_ = true;
  bool save_ = false;
  int kind_ = 0;
  char note_[200] = {};
  bool has_picture_ = false;
  int picture_w_ = 0, picture_h_ = 0;
  std::vector<uint8_t> picture_;
  std::string portal_;
  std::string place_;
  std::unordered_map<uint64_t, Pad> pads_;
};

rex::ui::ImGuiDrawer* g_drawer = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;
std::unique_ptr<ProblemNoteDialog> g_dialog;  // UI thread only

}  // namespace

void RegisterProblemNote(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* app_context) {
  g_drawer = drawer;
  g_app_context = app_context;
}

void OpenProblemNote() {
  if (!g_drawer || !g_app_context || g_dialog || menu_input::MenuOpen()) return;
  g_dialog = std::make_unique<ProblemNoteDialog>(g_drawer, [] {
    g_app_context->CallInUIThreadDeferred([] { g_dialog.reset(); });
  });
}

void ShutdownProblemNote() { g_dialog.reset(); }

}  // namespace skylanders
