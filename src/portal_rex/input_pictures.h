#pragma once

// Pictures for the settings menu's controls page: a drawn controller, for a game without a
// controller picture of its own and for controllers that aren't an Xbox pad (PlayStation,
// Nintendo or a generic layout like the Logitech Dual Action), and a drawn keyboard (QWERTY, or
// QWERTZ when the menus speak German) for when the menu is used with the keyboard. Both put marks
// on buttons or keys. Everything draws straight into an ImDrawList; UI thread only.

#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

namespace skylanders::input_pictures {

enum class PadKind { kXbox, kPlayStation, kNintendo, kGeneric };

// The layout to draw for a device, by the name SDL reports for it ("Xbox 360 Controller",
// "PS4 Controller", "Nintendo Switch Pro Controller", "Logitech Dual Action").
PadKind PadKindOf(std::string_view device_name);

// A mark on a controller button, named as the *_buttons settings do ("a", "lb", "ls", "up",
// "guide"); `ring` > 0 draws it outside the earlier marks of the same button.
struct PadMark {
  std::string button;
  ImU32 colour = 0;
  int ring = 0;
};
// Draws the controller as large as fits into [a, b], centred at the top.
void DrawController(ImDrawList* dl, ImVec2 a, ImVec2 b, PadKind kind,
                    const std::vector<PadMark>& marks);

// A button's name on that kind of controller: "A" / "Kreuz" / "B" (Nintendo's bottom button),
// "LB" / "L1" / "L", ...; nullptr for a name the settings don't use.
const char* PadButtonName(PadKind kind, std::string_view button, bool german);

// A face button ("a", "b", "x", "y") as that controller has it, a circle of radius `r` at `c`:
// the Xbox colours and letters, PlayStation's symbols, Nintendo's letters in their places.
void DrawPadFaceButton(ImDrawList* dl, PadKind kind, std::string_view button, ImVec2 c, float r);

// A mark on a key, by the SDK's key name ("F6", "W", "Semicolon", "Space", "Up"); `label` is
// printed small on the key under its own legend (e.g. the controller button it stands for).
struct KeyMark {
  std::string key;
  ImU32 colour = 0;
  std::string label;
};
// Draws the keyboard as large as fits into [a, b], centred at the top; returns its height.
float DrawKeyboard(ImDrawList* dl, ImVec2 a, ImVec2 b, const std::vector<KeyMark>& marks,
                   bool german);

// The key a "Shift+Up"-style binding presses last ("Up"), and the modifier before it, if any.
std::string MainKey(std::string_view binding);
std::string ModifierKey(std::string_view binding);

// A key's legend as the drawing prints it ("Semicolon" is "\xC3\x9C" on a German keyboard).
std::string KeyLegend(std::string_view key, bool german);

}  // namespace skylanders::input_pictures
