#pragma once

// The settings menu in the portal menu's look, for controller and keyboard: pages of settings the
// app describes (SettingsPage), switched with LB / RB or LT / RT, each setting changed with left /
// right. Opens with settings_menu_buttons (default Back + RB) on any controller or
// bind_settings_menu (F4), also over the portal or the player menu (which then closes like with
// B). A change applies at once where it can, so it can be seen; Y saves the changes into the
// settings file, line by line (settings_file.h), and leaving with unsaved changes asks whether to
// save them (not saving puts the old values back). Settings that only apply at start-up are marked
// and the menu says so. The game sees no input while it is open.
//
// Started by the Core of Light launcher (--started_by_launcher, which it passes), the menu leaves
// out the Graphics page and every setting that only applies at start-up: the launcher sets those
// on the game's card before starting (owner's wish, Core of Light #10), and changing them in the
// middle of a game does nothing until the next start. Started on its own, the game keeps them all.

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace rex::ui {
class ImGuiDrawer;
class WindowedAppContext;
}  // namespace rex::ui

namespace skylanders {

struct SettingOption {
  std::string value;  // as the cvar takes it ("true", "2", "cas")
  std::string en, de;
};

struct Setting {
  std::string cvar;  // empty for an action
  std::string en, de;
  std::string help_en, help_de;
  // The values to step through; empty for a bool cvar (off / on).
  std::vector<SettingOption> options;
  // Takes effect only after a restart. Settings whose cvar says so (start-up only or restart) are
  // marked anyway.
  bool restart = false;
  // A preset: `current` says which option matches now ("" for none: shown as custom), `preset`
  // gives the settings (cvar, value) an option stands for; the menu stores them.
  std::function<std::string()> current;
  std::function<std::vector<std::pair<std::string, std::string>>(const std::string& value)>
      preset;
  // An action row: A runs it (with the menu closed).
  std::function<void()> action;
  // With an action: the question asked first (yes / no), e.g. before quitting the game.
  std::string confirm_en, confirm_de;
  // The options are controller button entries in the *_buttons settings' format
  // ("guide,back+start,hold_back"); the info panel shows them on the controller picture
  // (SetControllerPicture), each entry in its own colour. Without a cvar (and no action) the row is
  // an overview: every button setting of its page on the picture, one colour each.
  bool buttons = false;
  // A bool cvar this setting needs on: while it is off the row is greyed and can't be changed (the
  // keyboard's player with the keyboard off; a colleague's wish, 2026-10-23). Empty: always.
  std::string enabled_by;
};

struct SettingsPage {
  std::string en, de;
  std::vector<Setting> settings;
};

// A controller picture for the button settings: an art image (portal_menu.h SetArtImageProvider)
// and where each button sits in it, in the image's pixels. Without one, button settings show their
// buttons as text only.
struct ControllerSpot {
  std::string button;  // as the *_buttons settings name it: "a", "guide", "back", "lb", "ls", "up"
  float x = 0, y = 0, radius = 0;
};
struct ControllerPicture {
  std::string art_key;
  float width = 0, height = 0;  // the image's size
  std::vector<ControllerSpot> spots;
};
void SetControllerPicture(ControllerPicture picture);

// `config_path`: the settings file changes go to. Call from ReXApp::OnCreateDialogs.
void RegisterSettingsMenu(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* app_context,
                          std::vector<SettingsPage> pages, std::filesystem::path config_path);

// Closes the menu; call from ReXApp::OnShutdown.
void ShutdownSettingsMenu();

// The rows every game's Game page ends with, so a controller alone can leave the game (it can't
// press Alt+F4 or click the window's X): "Spiel beenden" closes the game as its window's X does
// (saves closed as usual; a launcher that started it comes back), "Neu starten" starts it again
// with the same command line, for the settings that apply only from the next start. Both ask
// first. Neu starten is left out when the launcher started the game: it sets those settings on
// the game's card before starting, and it waits for this process.
void AddQuitSettings(SettingsPage& page);

// A game that "Neu starten" started waits here until the old one has ended (its saves and figure
// files closed), at most 20 s. Called by InstallPortal (portal_rex.h), early in OnPostSetup.
void WaitForRestartedInstance();

// "Fenstergröße: Automatisch" in fullscreen: `resolution` set, for this start only (nothing is
// written), to the screen's mode, so a 21:9 or 32:9 screen gets the ultrawide picture by itself
// (the ultrawide hooks and the presenter take the aspect from `resolution`; a colleague's 21:9
// screen stayed 16:9, 2026-10-23). Nothing when started for the launcher's schema, which shows the
// file's own value. Call first thing in OnPreSetup.
void ResolutionFromScreenIfAutomatic();

}  // namespace skylanders
