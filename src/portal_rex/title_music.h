#pragma once

// Recording a game's title music for the Core of Light launcher, which plays it while the game's
// card is chosen (the owner's wish, 2026-10-22). Started with --capture_title_music=<file.wav>
// (and, from the launcher, the logo movies skipped, the sound muted and no input), the game records
// what it plays from 28 to 58 seconds after its window opened, when every game sits at its title
// screen, through the SDK's output tap (rex/audio/output_tap.h), writes it as a 48 kHz 16-bit
// stereo WAV with a short fade at both ends and quits. Without the flag nothing happens.

namespace skylanders {

// Called by RegisterSettingsMenu (settings_menu.h), once the window exists.
void StartTitleMusicCaptureIfRequested();

}  // namespace skylanders
