#pragma once

// "Problem melden" in the game's settings menu (problem_note.cpp; the owner's wish, 2026-10-23:
// note what went wrong right where it happened). The game's picture as it is that moment, a kind
// of problem picked with the controller (Grafik, Ton, Steuerung, Portal, Absturz, Spielstand,
// Sonstiges) and a few words typed on the keyboard if there is one go into the game's logs folder,
// problem-<date>.txt and .png, with the game, how long it ran and the figures on the portal; the
// log gets a line at that moment. The Core of Light launcher's Problem melden takes the newest of
// them along.

namespace rex::ui {
class ImGuiDrawer;
class WindowedAppContext;
}  // namespace rex::ui

namespace skylanders {

// Call from ReXApp::OnCreateDialogs (RegisterSettingsMenu does).
void RegisterProblemNote(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* app_context);

// Opens the dialog (UI thread); the settings menu's "Problem melden" row does.
void OpenProblemNote();

void ShutdownProblemNote();

}  // namespace skylanders
