#include "portal_rex/problem_note.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
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

// The settings a picture or speed problem most often depends on, "name=value, ..." (only those this
// game has; an empty value is a setting switched off, e.g. scene_size), so a note says them without
// the log (Core of Light #76, imafuturecorpse: the picture, the place and the settings at once).
std::string SettingsText() {
  static constexpr const char* kNames[] = {
      "gpu_backend",       "render_target_path_d3d12", "resolution_scale", "scene_size",
      "present_effect",    "frame_rate_limit",         "vsync",            "fullscreen",
      "draw_resolution_scale_threshold", "gpu_native_surfaces", "anisotropic_override",
      "bloom_strength",    "swap_post_effect",         "portal_mode",      "language",
      "button_prompts"};
  std::string text;
  for (const char* name : kNames) {
    if (!rex::cvar::GetFlagInfo(name)) continue;  // not in this game
    text += (text.empty() ? "" : ", ") + std::string(name) + "=" + rex::cvar::GetFlagByName(name);
  }
  return text;
}

// What a note holds: taken on the UI thread when it is made (TakeNote), saved by SaveNote.
struct Note {
  std::string kind;   // kKinds' English name, or the quick note's
  std::string words;  // typed by the player, may be empty
  std::string portal, place, settings;
  std::tm when{};
  long long ran = 0;  // seconds since the program started
  bool has_picture = false;
  int picture_w = 0, picture_h = 0;
  std::vector<uint8_t> picture;
};

// A note of this moment: the picture first, as close to the key press as can be, then the place,
// the portal and the settings. UI thread.
Note TakeNote(std::string kind) {
  Note note;
  note.has_picture = CaptureGamePicture(note.picture_w, note.picture_h, note.picture);
  note.kind = std::move(kind);
  note.portal = PortalFiguresText();
  note.place = dev::PlaceText();
  note.settings = SettingsText();
  const std::time_t now = std::time(nullptr);
#if defined(_WIN32)
  localtime_s(&note.when, &now);
#else
  localtime_r(&now, &note.when);
#endif
  note.ran = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - g_started).count();
  return note;
}

// base + ".txt" without path::string() (it throws for a folder name outside the Windows code page).
std::filesystem::path WithExtension(const std::filesystem::path& base, const char* extension) {
  return std::filesystem::path(base.native() + std::filesystem::path(extension).native());
}

// Pictures being packed on their threads; a note made while two are still at it keeps no picture,
// so a burst of notes can't pile up frames (about 15 MB each at 2x) and threads.
std::atomic<int> g_pictures_packing{0};
constexpr int kMaxPicturesPacking = 2;

// Saves a note into the logs folder as problem-<date>.txt (problem-<date>-2 and on when that name is
// taken) and its picture as .png beside it. The text is written here and now, which also takes the
// name: a crash or a closed window right after it keeps the note (the game exits without tidying
// up). The picture is packed on a thread, as a 2x picture takes a moment that would hold up the
// screen. UI thread. Returns the note's file name without extension.
std::string SaveNote(Note note) {
  std::error_code ec;
  const std::filesystem::path dir = std::filesystem::current_path(ec) / "logs";
  std::filesystem::create_directories(dir, ec);
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y-%m-%d-%H%M%S", &note.when);
  std::filesystem::path base = dir / (std::string("problem-") + stamp);
  for (int n = 2; std::filesystem::exists(WithExtension(base, ".txt"), ec); ++n) {
    base = dir / (std::string("problem-") + stamp + "-" + std::to_string(n));
  }
  const std::string name = Utf8Path(base.filename());
  const bool packing = note.has_picture && g_pictures_packing.fetch_add(1) < kMaxPicturesPacking;
  if (note.has_picture && !packing) g_pictures_packing.fetch_sub(1);
  char when[64];
  std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &note.when);
  char running[32];
  std::snprintf(running, sizeof(running), "%lld:%02lld:%02lld", note.ran / 3600, note.ran / 60 % 60,
                note.ran % 60);
  {
    std::ofstream out(WithExtension(base, ".txt"), std::ios::binary);
    out << "Problem noted in the game\n"
        << "Game: " << ProgramName() << "\n"
        << "When: " << when << " (running for " << running << ")\n"
        << "Kind: " << note.kind << "\n"
        << "Note: " << note.words << "\n"
        << "On the portal: " << (note.portal.empty() ? "nothing" : note.portal) << "\n";
    // Where the player was when the note was made (games that report it, dev/where.h).
    if (!note.place.empty()) out << "Where: " << note.place << "\n";
    out << "Settings: " << note.settings << "\n"
        << "Picture: "
        << (packing ? name + ".png" : note.has_picture ? std::string("none (the last ones were still being saved)")
                                                       : std::string("none"))
        << "\n";
  }
  if (packing) {
    std::thread([png = WithExtension(base, ".png"), w = note.picture_w, h = note.picture_h,
                 rgba = std::move(note.picture)] {
      WritePng(png, w, h, rgba.data());
      g_pictures_packing.fetch_sub(1);
    }).detach();
  }
  return name;
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
    // menus anyway); the place, the portal and the settings of this moment with it (TakeNote).
    note_ = TakeNote(kKinds[0].en);
    menu_input::SetMenuOpen(true);
    menu_input::SetMenuCloser([this] { Close(); });
    REXLOG_INFO("Problem note: opened ({})", note_.has_picture ? "picture kept" : "no picture");
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
          note_.has_picture ? Tr("The picture of this moment is kept. What's wrong?",
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
                                                words_, sizeof(words_), ImGuiInputTextFlags_EnterReturnsTrue);
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
    note_.kind = kKinds[kind_].en;
    note_.words = words_;
    const std::string place = note_.place;
    const std::string name = SaveNote(std::move(note_));
    REXLOG_WARN("Problem noted by the player: {}{}{} ({}.txt)", kKinds[kind_].en, words_[0] ? ": " : "",
                words_, name);
    if (!place.empty()) REXLOG_INFO("Where (problem note): {}", place);
    ShowPortalToast(Tr("Noted. Report a problem in Core of Light sends it along.",
                       "Notiert. \xE2\x80\x9EProblem melden\xE2\x80\x9C in Core of Light schickt es mit."));
    Close();
  }

  std::function<void()> on_done_;
  bool done_ = false;
  bool first_frame_ = true;
  bool save_ = false;
  int kind_ = 0;
  char words_[200] = {};
  Note note_;
  std::unordered_map<uint64_t, Pad> pads_;
};

rex::ui::ImGuiDrawer* g_drawer = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;
std::unique_ptr<ProblemNoteDialog> g_dialog;  // UI thread only

}  // namespace

void RegisterProblemNote(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* app_context) {
  g_drawer = drawer;
  g_app_context = app_context;
  // F12: the quick note. F12 is free in the games (the SDK has F3, F4, F7 and the backtick; the
  // menus F2, F4, F5, F6, F8, F9); F10 is a Windows menu key and Print a Windows screenshot key.
  // Under a debugger Windows takes F12 for itself (a break), so there the key does nothing.
  rex::ui::RegisterBind("bind_quick_problem_note", "F12",
                        "Note a problem at once: the picture, the place and the settings, no menu",
                        [] { QuickProblemNote(); });
}

std::string QuickProblemNoteKey() {
  std::string key = rex::cvar::GetFlagByName("bind_quick_problem_note");
  if (const auto comma = key.find(','); comma != std::string::npos) key.resize(comma);
  return key.empty() ? "F12" : key;
}

void QuickProblemNote() {
  // Not over a menu or the note dialog (the picture would be of the game behind it, the keys are
  // theirs). One note per press: the binds also get Windows' key repeats (about every 30 ms after
  // half a second), so a call within 0.7 s of the one before belongs to the same press or a double
  // press and is left out, the hold going on resetting that time.
  static Clock::time_point last_call{};
  const Clock::time_point now = Clock::now();
  const bool repeat = last_call != Clock::time_point{} && now - last_call < std::chrono::milliseconds(700);
  last_call = now;
  if (repeat || !g_drawer || g_dialog || menu_input::MenuOpen()) return;
  Note note = TakeNote("Picture or graphics (quick note, " + QuickProblemNoteKey() + ")");
  const std::string place = note.place;
  const std::string name = SaveNote(std::move(note));
  REXLOG_WARN("Problem noted by the player: quick note ({}.txt)", name);
  if (!place.empty()) REXLOG_INFO("Where (problem note): {}", place);
  ShowPortalToast(Tr("Noted: the picture, the place and the settings. Report a problem in Core of "
                     "Light sends it along.",
                     "Notiert: Bild, Ort und Einstellungen. \xE2\x80\x9EProblem melden\xE2\x80\x9C in "
                     "Core of Light schickt es mit."));
}

void OpenProblemNote() {
  if (!g_drawer || !g_app_context || g_dialog || menu_input::MenuOpen()) return;
  g_dialog = std::make_unique<ProblemNoteDialog>(g_drawer, [] {
    g_app_context->CallInUIThreadDeferred([] { g_dialog.reset(); });
  });
}

void ShutdownProblemNote() { g_dialog.reset(); }

}  // namespace skylanders
