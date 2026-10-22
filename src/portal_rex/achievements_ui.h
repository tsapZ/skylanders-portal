#pragma once

// The achievements, as the games show them: an Xbox 360-style pop-up when one unlocks (bottom
// middle, the round icon opening into a bar with "Achievement unlocked" and "20G - name", no
// sound) and a list of every achievement in the menus' style, steered with a controller or the
// keyboard. Both replace the SDK's built-in ones (ReXApp::CreateAchievementNotificationDialog,
// CreateAchievementsOverlay).

#include <filesystem>
#include <functional>
#include <memory>

#include "portal_rex/settings_menu.h"

namespace rex {
class Runtime;
}  // namespace rex
namespace rex::system {
class AchievementManager;
}  // namespace rex::system
namespace rex::ui {
class AchievementNotificationDialog;
class ImGuiDrawer;
class ImmediateDrawer;
class WindowedAppContext;
}  // namespace rex::ui

namespace skylanders {

// For ReXApp::CreateAchievementNotificationDialog.
std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementToast(
    rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
    rex::Runtime* runtime);

// The list. Call once the runtime exists (ReXApp::OnPostSetup); `manager` is asked each time the
// list opens (the app's achievements(), which needs the kernel).
void RegisterAchievementsMenu(rex::ui::ImGuiDrawer* drawer,
                              rex::ui::ImmediateDrawer* immediate_drawer,
                              rex::ui::WindowedAppContext* app_context, rex::Runtime* runtime,
                              std::function<rex::system::AchievementManager*()> manager);

// Opens the list, or closes it when it is open. From ReXApp::CreateAchievementsOverlay (F7, return
// nullptr there) and the settings menu's "Achievements" row. UI thread only.
void ToggleAchievementsMenu();

// Closes the list and frees its icons; call from ReXApp::OnShutdown.
void ShutdownAchievementsMenu();

// The achievements for the Core of Light launcher, which shows each profile's progress without a
// game running: in `dir` (the game's picture folder, SharedPicturesDir) achievements.txt and the
// title's own pictures, ach-<image id>.png (the PNG files of its XDBF as they are). The first line
// is "title<TAB><title id in hex>", then one line per achievement: id, gamerscore, picture file,
// name, the text once unlocked, the text before. On a background thread once the game's list is
// there (RegisterAchievementsMenu); StartFigurePictureCache starts it.
void StartAchievementCache(std::filesystem::path dir);

// The settings menu row that opens the list (for a page of the game's settings menu), so the
// achievements can be seen with a controller too.
Setting AchievementsSetting();

}  // namespace skylanders
