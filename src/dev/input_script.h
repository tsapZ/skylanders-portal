// Scripted controller input for automated test runs (--input_script=<file>).
//
// The keyboard emulation only reads keys while the game window has the focus, so a test that
// drives the game from outside breaks as soon as anything else takes the foreground (a system
// dialog, a notification). This plays a fixed timeline instead, independent of the focus, as an
// extra input device that merges with the controller and keyboard like the keyboard does.
//
// Script: one command per line or ';'-separated, '#' starts a comment. Each command starts when
// the previous one ends; the timeline starts when the game starts.
//   wait <seconds>
//   tap <button>             press for 120 ms
//   hold <button> <ms>
//   stick <x> <y> <ms>       left stick, x and y in -1..1
//   log <text>               writes a log line, to find a point of the timeline in the log
//   fps <seconds>            measures the guest frame rate over that time and logs it
//   scan <hex> [<hex> ...]   logs where this big-endian u32 sequence is in guest memory
//                            (hit0, hit1, ... in the order logged)
//   poke <hex addr> <hex>    writes a big-endian u32 to guest memory
//   dump <hex addr> <words>  logs that many big-endian u32 from guest memory, 8 per line
//   save <hex addr> <hex bytes> <file>
//                            writes that much guest memory to a file, unchanged (big-endian;
//                            pages that are not mapped come out as zeros): snapshots at
//                            different moments, compared offline, find state fields
//   watch <hex addr> [<hex> [<hex snapshot addr> <hex words>]]
//                            logs which guest functions write this address, only writes of that
//                            big-endian value if given, and copies guest memory at the first hit
//                            (write_watch.h)
//   watchread <hex addr>     the same for reads and writes
//   pollwatch <hex addr> [<n>]
//                            polls this big-endian u32 instead of trapping its writes and logs the
//                            call chains and guest registers of the threads in recompiled code at
//                            each of its first n changes (16): no exceptions at all (write_watch.h)
//   unwatch                  ends the watch (or poll watch) and logs the writers
//   For watch, watchread, pollwatch, poke and dump the address can also be hit<n>[+<hex>]: the
//   n-th hit of the last scan, plus an offset, or *<hex>[+<hex>]: the pointer stored at that
//   address when the command runs, plus an offset. For data whose heap address changes from run
//   to run.
//   stacks [<samples>]       logs where every thread is (call chains named by guest function),
//                            3 samples 250 ms apart by default: finds where the game hangs
//   renderdoc [n]            captures the next n guest frames with an attached RenderDoc
//   player <n>               the scripted controller plays as player n (1-4) from now on
//   key <name>               presses a key for the app's key binds (F4, F6, F9; names as in the
//                            bind_* settings), e.g. to open an overlay
//   place <slot> <file>      puts a figure file straight onto the emulated portal's slot (0-15),
//                            past the portal menu, e.g. a second sidekick for the portal check
//   remove <slot>            takes the figure off that slot again
//   trap <slot> <villain>    puts a new trap holding that villain (its name, e.g. Chompy Mage)
//                            onto the slot, as the portal developer tools (F8) make one
//   quit                     ends the game
// Buttons: a b x y start back guide up down left right lb rb ls rs lt rt, or several pressed
// together joined with + (tap back+y)

#pragma once

#include <cstdint>
#include <functional>

namespace rex::memory {
class Memory;
}
namespace rex::system {
class IInputSystem;
}

namespace skylanders::dev {

// Installs the scripted input device if --input_script is set. Call once the runtime exists.
// guest_frame_count returns the number of guest frames presented so far (renderdoc command);
// run_in_ui runs a function on the UI thread (key command).
void InstallInputScript(rex::system::IInputSystem* input_system, rex::memory::Memory* memory,
                        std::function<uint64_t()> guest_frame_count,
                        std::function<void(std::function<void()>)> run_in_ui);

}  // namespace skylanders::dev
