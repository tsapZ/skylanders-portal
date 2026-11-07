#pragma once

namespace skylanders {

// Whether a program runs that shows a PlayStation controller to games as a virtual Xbox controller
// while the real one stays visible too (DS4Windows, DualSenseX). Walks the process list: only for
// when the controllers change.
bool ControllerRemapperRunning();

}  // namespace skylanders
