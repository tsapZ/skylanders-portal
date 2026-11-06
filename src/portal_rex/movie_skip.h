#pragma once
// Any movie skipped with a button (the owner's wish, 2026-11-06; also handy in tests). Each game
// hooks the place where its own movie code asks Bink whether the movie is over (the HBINK's
// FrameNum at +12 equal to its Frames at +8) and calls MovieEndCheck there with the HBINK. A press
// of a movie_skip_buttons button while that movie plays sets its Frames to the frame it is on, so
// the game ends the movie its own way, as if it had played out. Movies the game loops get their
// Frames back when they start over.

#include <cstdint>
#include <string>

#include <rex/cvar.h>

#include "portal_rex/settings_menu.h"

REXCVAR_DECLARE(std::string, movie_skip_buttons);

namespace skylanders {

// `membase`: the guest's virtual memory base, `hbink`: the movie's guest address. Game thread,
// once per movie frame; no system calls.
void MovieEndCheck(uint8_t* membase, uint32_t hbink);

// The F4 > Spiel row for movie_skip_buttons (off, Start, A, Start or A; at once). Every game's
// game page has it.
Setting MovieSkipSetting();

}  // namespace skylanders
