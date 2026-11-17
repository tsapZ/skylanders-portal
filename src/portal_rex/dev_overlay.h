#pragma once
// A line in the window's top-left corner for test runs and bug hunts (dev_overlay, off by default; the dev kit list
// of 2026-11-16, item 4): where the player is (dev/where.h: the level and player 1's position) and the frame rate,
// so a picture shows where and how fast it was taken. Not in the F4 menu: set on the command line or with the
// input script's set command.

namespace rex::ui {
class ImGuiDrawer;
}

namespace skylanders {

void RegisterDevOverlay(rex::ui::ImGuiDrawer* drawer);
void ShutdownDevOverlay();

}  // namespace skylanders
