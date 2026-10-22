#pragma once

// Trap Team's trapped villains talk through the Traptanium Portal's speaker; with the emulated
// portal their voices come out of the PC's speakers (portal_audio). This shows who is talking, like
// a voice chat's speaking indicator (an idea from a friend of the owner, TrapTeamRecomp #10): the
// villain's picture from the game in a circle at the bottom left, a ring around it that pulses with
// the voice, its name under it; it fades in while the speaker plays and out a moment after.
// portal_speaker_overlay = false turns it off. The picture comes from the game's art by the key
// "villain:<id>" (portal_menu.h SetArtImageProvider), id as the trap stores it (1-46); without one
// the circle shows the name's first letter.

namespace rex::ui {
class ImGuiDrawer;
}  // namespace rex::ui

namespace skylanders {

// Call once the drawer exists (ReXApp::OnCreateDialogs or OnPostSetup).
void RegisterSpeakerOverlay(rex::ui::ImGuiDrawer* drawer);

// Removes it; call from ReXApp::OnShutdown.
void ShutdownSpeakerOverlay();

}  // namespace skylanders
