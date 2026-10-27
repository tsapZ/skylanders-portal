// Finds which game function writes a guest address, without a debugger (input script `watch` /
// `unwatch`, src/dev/input_script.h). A hardware write breakpoint (debug register 0) goes on every
// thread of the process; a vectored exception handler notes where each write came from, and
// StopWriteWatch names those places by guest function through PPCFuncMappings (host function
// containing the instruction -> its guest address). Developer tool for reverse engineering;
// Windows only.

#pragma once

#include <cstdint>
#include <optional>

namespace rex::memory {
class Memory;
}

namespace skylanders::dev {

// Watches 4 bytes at a virtual guest address; with `value`, only writes that leave that
// big-endian word there count. Each hit keeps the call chain (host frames, named by guest function
// in the log). Threads started later are not covered.
// `reads`: reads count too (debug register mode read/write instead of write).
// `snapshot` / `snapshot_words`: at the first counted hit, copy that much guest memory (e.g. the
// text a parser is reading), logged as text and words by StopWriteWatch.
void StartWriteWatch(rex::memory::Memory* memory, uint32_t guest_address,
                     std::optional<uint32_t> value = std::nullopt, bool reads = false,
                     uint32_t snapshot = 0, uint32_t snapshot_words = 0);

// Removes the breakpoint and logs every writer seen since StartWriteWatch.
void StopWriteWatch();

// Logs where every other thread of the process is right now, `samples` times `interval_ms` apart:
// the thread's name and call chain, recompiled frames as guest functions, the others as module and
// nearest export. Shows where the game hangs without a debugger (input script `stacks`).
void LogThreadStacks(int samples = 3, int interval_ms = 250);

// When the game really crashes (an exception nothing handles, e.g. the SDK's "Unhandled guest
// access violation"), logs the faulting thread's call chain, recompiled frames as guest functions,
// and its guest registers, so a crash in a player's log says where in the game it happened. An
// unhandled-exception filter, so exceptions the SDK or the game handle themselves never reach it.
void InstallCrashReport(rex::memory::Memory* memory);

}  // namespace skylanders::dev
