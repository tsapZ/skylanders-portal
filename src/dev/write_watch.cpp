#include "dev/write_watch.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <chrono>
#include <csignal>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <immintrin.h>
#include <tlhelp32.h>
#endif

#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/xmemory.h>

#include "dev/where.h"

namespace skylanders::dev {
namespace {

#ifdef _WIN32

// The call chain of each hit (the instruction after the write first), written by the exception
// handler (no locks or logging there: it runs inside whatever game code did the write).
constexpr size_t kMaxHits = 2048;
constexpr int kFrames = 6;
struct Hit {
  uintptr_t frames[kFrames];
  uint64_t regs[16];  // rax rcx rdx rbx rsp rbp rsi rdi r8-r15 at the hit
  // The guest registers, if a host register points at the recompiled code's PPCContext (it
  // starts with r3, r0, r1, r2, r4 ... r31, 8 bytes each); the low words, else 0.
  uint32_t guest[32];
  int guest_from = -1;  // which host register held the context
  uint32_t stack[256];  // big-endian words from the guest stack pointer (r1) up
};

// Copies `size` bytes from `from`, or returns false if that memory can't be read. Through
// ReadProcessMemory, which fails instead of raising an exception: in Trap Team an access violation
// inside __try ended the game anyway (SwapForceRecomp #60).
bool SafeCopy(void* to, uint64_t from, size_t size) {
  SIZE_T copied = 0;
  return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(from), to, size,
                           &copied) &&
         copied == size;
}
std::array<Hit, kMaxHits> g_hits{};
std::atomic<size_t> g_hit_count{0};
std::atomic<size_t> g_write_count{0};
std::atomic<uintptr_t> g_watched{0};
std::atomic<bool> g_filter{false};
std::atomic<uint32_t> g_value{0};  // as the bytes read little-endian
PVOID g_handler = nullptr;
uint32_t g_guest_address = 0;
uint64_t g_membase = 0;  // host address of guest virtual 0
uint32_t g_snapshot = 0, g_snapshot_words = 0;
std::vector<uint8_t> g_snapshot_data;  // sized at start, filled once in the handler
std::atomic<bool> g_snapshot_taken{false};
std::thread g_refresher;  // puts the breakpoint on threads started after StartWriteWatch
bool g_reads = false;      // debug register mode 11 (read or write) instead of 01 (write)

// The stack a thread runs on, [low, high); {0, 0} if unknown (Unwind then doesn't check).
struct StackRange {
  uint64_t low = 0, high = 0;
};

// The calling thread's stack (for an exception handler: the thread that took the exception).
StackRange CurrentStackRange() {
  ULONG_PTR low = 0, high = 0;
  GetCurrentThreadStackLimits(&low, &high);
  return {low, high};
}

// Another thread's stack: StackBase from its TEB (NT_TIB) and the region reserved below it. The
// handle needs THREAD_QUERY_INFORMATION.
StackRange ThreadStackRange(HANDLE thread) {
  using QueryFn = LONG(NTAPI*)(HANDLE, int, PVOID, ULONG, PULONG);
  static const auto query = reinterpret_cast<QueryFn>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
  struct BasicInformation {  // THREAD_BASIC_INFORMATION (class 0)
    LONG exit_status;
    PVOID teb;
    PVOID client_id[2];
    ULONG_PTR affinity;
    LONG priority, base_priority;
  } info{};
  if (!query || query(thread, 0, &info, sizeof(info), nullptr) < 0 || !info.teb) return {};
  uint64_t base = 0;
  SIZE_T copied = 0;
  if (!ReadProcessMemory(GetCurrentProcess(),
                         static_cast<const uint8_t*>(info.teb) + offsetof(NT_TIB, StackBase),
                         &base, sizeof(base), &copied) ||
      copied != sizeof(base) || !base) {
    return {};
  }
  MEMORY_BASIC_INFORMATION region{};
  if (!VirtualQuery(reinterpret_cast<LPCVOID>(base - 1), &region, sizeof(region))) return {};
  return {reinterpret_cast<uint64_t>(region.AllocationBase), base};
}

// Walks the stack from the exception context with the binary's own unwind data (no symbols needed).
// Stops where the stack pointer leaves the thread's stack instead of reading there: in Trap Team
// such a read (host FFFFFFFF7DE60010) ended the game although it was inside __try (SwapForceRecomp
// #60). `contexts`, if given, gets each frame's registers (for the guest registers of a frame).
void Unwind(CONTEXT ctx, uintptr_t* frames, int count, StackRange stack,
            CONTEXT* contexts = nullptr) {
  for (int i = 0; i < count; ++i) {
    frames[i] = uintptr_t(ctx.Rip);
    if (contexts) contexts[i] = ctx;
    if (stack.high && (ctx.Rsp < stack.low || ctx.Rsp + 8 > stack.high)) {
      for (int k = i + 1; k < count; ++k) frames[k] = 0;
      return;
    }
    DWORD64 image_base = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &image_base, nullptr);
    if (!fn) {
      // A leaf function: the return address is on top of the stack.
      ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
      ctx.Rsp += 8;
    } else {
      PVOID handler_data = nullptr;
      DWORD64 establisher = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, ctx.Rip, fn, &ctx, &handler_data,
                       &establisher, nullptr);
    }
    if (!ctx.Rip) {
      for (int k = i + 1; k < count; ++k) frames[k] = 0;
      return;
    }
  }
}

// Unwind may walk into frames without unwind data and read garbage; a fault ends the chain.
void UnwindSafe(const CONTEXT& ctx, uintptr_t* frames, int count, StackRange stack,
                CONTEXT* contexts = nullptr) {
  __try {
    Unwind(ctx, frames, count, stack, contexts);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

// The guest registers r0-r31 (low words) if a host register points at the recompiled code's
// PPCContext: a host (not guest) pointer, 64-byte aligned, starting with r3, r0, r1, r2, r4 ... r31
// (8 bytes each), whose r1 is a guest stack (0x7xxxxxxx). Returns that host register's index in
// rax rcx rdx rbx rsp rbp rsi rdi r8-r15, or -1.
int ReadGuestRegisters(const DWORD64 (&regs)[16], uint32_t (&guest)[32]) {
  for (int r : {7, 6, 3, 5, 12, 13, 14, 15}) {  // rdi rsi rbx rbp r12-r15
    const uint64_t p = regs[r];
    if (!p || (p & 0x3F) || (p >= g_membase && p < g_membase + 0x100000000ull)) continue;
    uint64_t raw[32];
    if (!SafeCopy(raw, p, sizeof(raw))) continue;
    const uint32_t r1 = uint32_t(raw[2]);
    if (r1 < 0x40000000u || r1 >= 0x80000000u) continue;
    static const int kOrder[32] = {3, 0, 1, 2, 4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                                   16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    for (int k = 0; k < 32; ++k) guest[kOrder[k]] = uint32_t(raw[k]);
    return r;
  }
  return -1;
}

LONG CALLBACK OnException(PEXCEPTION_POINTERS info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
  CONTEXT* ctx = info->ContextRecord;
  if (!(ctx->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
  // Ours even while the watch is being taken down: a trap left unhandled would end the process.
  ctx->Dr6 = 0;
  const uintptr_t watched = g_watched.load();
  if (!watched) return EXCEPTION_CONTINUE_EXECUTION;
  g_write_count.fetch_add(1);
  if (g_filter.load() && *reinterpret_cast<const uint32_t*>(watched) != g_value.load()) {
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  const size_t i = g_hit_count.fetch_add(1);
  if (i == 0 && !g_snapshot_data.empty() &&
      SafeCopy(g_snapshot_data.data(), g_membase + g_snapshot, g_snapshot_data.size())) {
    g_snapshot_taken.store(true);
  }
  if (i < kMaxHits) {
    UnwindSafe(*ctx, g_hits[i].frames, kFrames, CurrentStackRange());
    const DWORD64 regs[16] = {ctx->Rax, ctx->Rcx, ctx->Rdx, ctx->Rbx, ctx->Rsp, ctx->Rbp,
                              ctx->Rsi, ctx->Rdi, ctx->R8,  ctx->R9,  ctx->R10, ctx->R11,
                              ctx->R12, ctx->R13, ctx->R14, ctx->R15};
    for (int r = 0; r < 16; ++r) g_hits[i].regs[r] = regs[r];
    g_hits[i].guest_from = ReadGuestRegisters(regs, g_hits[i].guest);
    if (g_hits[i].guest_from >= 0 &&
        !SafeCopy(g_hits[i].stack, g_membase + g_hits[i].guest[1], sizeof(g_hits[i].stack))) {
      memset(g_hits[i].stack, 0, sizeof(g_hits[i].stack));
    }
  }
  return EXCEPTION_CONTINUE_EXECUTION;
}

// Sets (address != 0) or clears debug register 0 on every other thread of the process.
void SetBreakpointOnAllThreads(uintptr_t address, bool quiet = false) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return;
  const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  int threads = 0;
  for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self) continue;
    HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                               FALSE, entry.th32ThreadID);
    if (!thread) continue;
    if (SuspendThread(thread) != DWORD(-1)) {
      CONTEXT ctx{};
      ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
      if (GetThreadContext(thread, &ctx)) {
        if (address) {
          ctx.Dr0 = address;
          // L0 on, RW0 = 01 (write) or 11 (read or write), LEN0 = 11 (4 bytes).
          ctx.Dr7 = (ctx.Dr7 & ~DWORD64(0xF0003)) | 1 | ((g_reads ? 3u : 1u) << 16) | (3u << 18);
        } else {
          ctx.Dr0 = 0;
          ctx.Dr7 &= ~DWORD64(0xF0003);
        }
        if (SetThreadContext(thread, &ctx)) ++threads;
      }
      ResumeThread(thread);
    }
    CloseHandle(thread);
  }
  CloseHandle(snapshot);
  if (!quiet) {
    REXLOG_INFO("Write watch: debug register {} on {} threads", address ? "set" : "cleared",
                threads);
  }
}

// The guest function whose host code contains `rip`: the mapping with the highest host address
// not above it.
std::pair<uint32_t, uintptr_t> GuestFunctionAt(uintptr_t rip) {
  static std::vector<std::pair<uintptr_t, uint32_t>> table;
  if (table.empty()) {
    for (const PPCFuncMapping* m = PPCFuncMappings; m->host; ++m) {
      table.emplace_back(reinterpret_cast<uintptr_t>(m->host), uint32_t(m->guest));
    }
    std::sort(table.begin(), table.end());
  }
  auto it = std::upper_bound(table.begin(), table.end(), std::make_pair(rip, UINT32_MAX));
  if (it == table.begin()) return {0, 0};
  --it;
  return {it->second, rip - it->first};
}

// A host frame outside the recompiled code: module, and the nearest export below it if the module
// exports any (the SDK DLLs do, so their frames read as SDK function names).
std::string HostFrameName(uintptr_t address) {
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(address), &module)) {
    return fmt::format("{:X}", address);
  }
  char path[MAX_PATH] = {};
  GetModuleFileNameA(module, path, MAX_PATH);
  const char* name = std::strrchr(path, '\\');
  name = name ? name + 1 : path;
  const auto base = reinterpret_cast<uintptr_t>(module);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
      base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
  const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  const char* best = nullptr;
  uintptr_t best_rva = 0;
  if (dir.Size) {
    const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    const auto* functions = reinterpret_cast<const DWORD*>(base + exports->AddressOfFunctions);
    const auto* names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
    const auto* ordinals = reinterpret_cast<const WORD*>(base + exports->AddressOfNameOrdinals);
    const uintptr_t rva = address - base;
    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
      const DWORD f = functions[ordinals[i]];
      if (f <= rva && f > best_rva) {
        best_rva = f;
        best = reinterpret_cast<const char*>(base + names[i]);
      }
    }
  }
  if (best) return fmt::format("{}!{}+0x{:X}", name, best, address - base - best_rva);
  return fmt::format("{}+0x{:X}", name, address - base);
}

std::string FrameName(uintptr_t address);

LPTOP_LEVEL_EXCEPTION_FILTER g_previous_filter = nullptr;
std::atomic<bool> g_crash_logged{false};

// The unhandled-exception filter of InstallCrashReport: logs once, then lets the crash go on.
LONG WINAPI OnCrash(PEXCEPTION_POINTERS info) {
  if (!g_crash_logged.exchange(true)) {
    const EXCEPTION_RECORD& record = *info->ExceptionRecord;
    const CONTEXT& ctx = *info->ContextRecord;
    uintptr_t frames[16] = {};
    UnwindSafe(ctx, frames, 16, CurrentStackRange());
    std::string chain;
    for (uintptr_t frame : frames) {
      if (!frame) break;
      chain += (chain.empty() ? "" : " <- ") + FrameName(frame);
    }
    std::string fault;
    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
      const uint64_t address = record.ExceptionInformation[1];
      const char* kind = record.ExceptionInformation[0] == 1 ? "write" : "read";
      fault = address >= g_membase && address < g_membase + 0x100000000ull
                  ? fmt::format(", {} of guest {:08X}", kind, uint32_t(address - g_membase))
                  : fmt::format(", {} of host {:X}", kind, address);
    }
    REXLOG_ERROR("Crash: exception {:08X}{} on thread {}; call chain: {}", record.ExceptionCode,
                 fault, GetCurrentThreadId(), chain);
    const DWORD64 regs[16] = {ctx.Rax, ctx.Rcx, ctx.Rdx, ctx.Rbx, ctx.Rsp, ctx.Rbp,
                              ctx.Rsi, ctx.Rdi, ctx.R8,  ctx.R9,  ctx.R10, ctx.R11,
                              ctx.R12, ctx.R13, ctx.R14, ctx.R15};
    uint32_t guest[32] = {};
    if (ReadGuestRegisters(regs, guest) >= 0) {
      std::string text;
      for (int r = 0; r < 32; ++r) text += fmt::format(" r{}={:08X}", r, guest[r]);
      REXLOG_ERROR("Crash: guest registers{}", text);
    }
    LogPlace("crash");
    rex::FlushLogging();
  }
  return g_previous_filter ? g_previous_filter(info) : EXCEPTION_CONTINUE_SEARCH;
}

// One frame of a sampled call chain: a guest function if the address is recompiled code (in the
// executable, within the range of the recompiled functions), else a host name.
std::string FrameName(uintptr_t address) {
  static uintptr_t exe_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  static uintptr_t code_end = [] {
    uintptr_t end = 0;
    for (const PPCFuncMapping* m = PPCFuncMappings; m->host; ++m) {
      end = std::max(end, reinterpret_cast<uintptr_t>(m->host));
    }
    return end + 0x10000;
  }();
  HMODULE module = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(address), &module);
  if (reinterpret_cast<uintptr_t>(module) == exe_base && address < code_end) {
    const auto [guest, offset] = GuestFunctionAt(address);
    if (guest) return fmt::format("sub_{:08X}+0x{:X}", guest, offset);
  }
  return HostFrameName(address);
}

// The SIGABRT handler of InstallCrashReport. The SDK ends the game with std::abort() on its fatal
// errors (REX_FATAL, e.g. "Call to invalid or unregistered function"), which no exception filter
// sees, so a player's log said what failed but not where. Logs the aborting thread's call chain
// once, and the guest registers of the first recompiled frame in it (the guest code that made the
// bad call), then lets the abort go on.
void OnAbort(int) {
  if (g_crash_logged.exchange(true)) return;
  constexpr int kAbortFrames = 24;
  static CONTEXT contexts[kAbortFrames];  // static: the handler may run on a small stack
  CONTEXT ctx{};
  RtlCaptureContext(&ctx);
  uintptr_t frames[kAbortFrames] = {};
  UnwindSafe(ctx, frames, kAbortFrames, CurrentStackRange(), contexts);
  std::string chain;
  int guest_frame = -1;
  for (int i = 0; i < kAbortFrames && frames[i]; ++i) {
    const std::string name = FrameName(frames[i]);
    // Frame 0 is this handler (host code that FrameName may take for recompiled code).
    if (guest_frame < 0 && i > 0 && name.rfind("sub_", 0) == 0) guest_frame = i;
    chain += (chain.empty() ? "" : " <- ") + name;
  }
  REXLOG_ERROR("Crash: abort on thread {}; call chain: {}", GetCurrentThreadId(), chain);
  if (guest_frame >= 0) {
    const CONTEXT& at = contexts[guest_frame];
    const DWORD64 regs[16] = {at.Rax, at.Rcx, at.Rdx, at.Rbx, at.Rsp, at.Rbp,
                              at.Rsi, at.Rdi, at.R8,  at.R9,  at.R10, at.R11,
                              at.R12, at.R13, at.R14, at.R15};
    uint32_t guest[32] = {};
    if (ReadGuestRegisters(regs, guest) >= 0) {
      std::string text;
      for (int r = 0; r < 32; ++r) text += fmt::format(" r{}={:08X}", r, guest[r]);
      REXLOG_ERROR("Crash: guest registers{}", text);
    }
  }
  LogPlace("crash");
  rex::FlushLogging();
}

std::thread g_poller;  // the poll watch's thread
std::atomic<bool> g_polling{false};

// The other threads of the process, opened ahead of time so that the poll watch can stop them the
// moment the value changes: opening them takes a millisecond or two, by which time the writer is
// usually back in its wait. Threads started after Open are not covered (Open again to add them).
class ThreadSet {
 public:
  ~ThreadSet() { Close(); }

  void Open() {
    Close();
    const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Thread32First(snapshot, &entry); ok && samples_.size() < kMaxThreads;
         ok = Thread32Next(snapshot, &entry)) {
      if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self) continue;
      HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME |
                                     THREAD_QUERY_INFORMATION,
                                 FALSE, entry.th32ThreadID);
      if (!thread) continue;
      Entry sample;
      sample.id = entry.th32ThreadID;
      sample.thread = thread;
      sample.stack = ThreadStackRange(thread);
      samples_.push_back(sample);
    }
    CloseHandle(snapshot);
  }

  // Stops every thread at once, takes their call chains and lets them go again. Nothing is
  // allocated while the threads are stopped: a stopped thread may hold the heap lock.
  void Sample() {
    for (Entry& s : samples_) s.taken = SuspendThread(s.thread) != DWORD(-1);
    for (Entry& s : samples_) {
      if (!s.taken) continue;
      CONTEXT ctx{};
      ctx.ContextFlags = CONTEXT_FULL;
      std::fill(std::begin(s.frames), std::end(s.frames), uintptr_t(0));
      s.guest_from = -1;
      if (GetThreadContext(s.thread, &ctx)) {
        UnwindSafe(ctx, s.frames, kStackFrames, s.stack);
        const DWORD64 regs[16] = {ctx.Rax, ctx.Rcx, ctx.Rdx, ctx.Rbx, ctx.Rsp, ctx.Rbp,
                                  ctx.Rsi, ctx.Rdi, ctx.R8,  ctx.R9,  ctx.R10, ctx.R11,
                                  ctx.R12, ctx.R13, ctx.R14, ctx.R15};
        s.guest_from = ReadGuestRegisters(regs, s.guest);
      }
    }
    for (Entry& s : samples_) {
      if (s.taken) ResumeThread(s.thread);
    }
  }

  // Logs the last Sample's chains of the threads that were in recompiled code (for the poll
  // watch: the writer is among them if it hasn't returned yet).
  void Log(const std::string& title) const {
    REXLOG_INFO("{}", title);
    for (const Entry& s : samples_) {
      if (!s.taken) continue;
      std::string chain;
      bool guest = false;
      for (uintptr_t frame : s.frames) {
        if (!frame) break;
        const std::string name = FrameName(frame);
        guest = guest || name.rfind("sub_", 0) == 0;
        chain += (chain.empty() ? "" : " <- ") + name;
      }
      if (!guest) continue;
      REXLOG_INFO("Poll watch:   {}: {}", s.id, chain);
      // The guest registers as the recompiled code last stored them (r3 often the object
      // the function works on).
      if (s.guest_from >= 0) {
        std::string text;
        for (int r = 0; r < 32; ++r) text += fmt::format(" r{}={:08X}", r, s.guest[r]);
        REXLOG_INFO("Poll watch:   {} guest registers:{}", s.id, text);
      }
    }
  }

 private:
  static constexpr int kStackFrames = 14;
  static constexpr size_t kMaxThreads = 512;
  struct Entry {
    DWORD id = 0;
    HANDLE thread = nullptr;
    StackRange stack;
    bool taken = false;
    uintptr_t frames[kStackFrames] = {};
    int guest_from = -1;  // ReadGuestRegisters' result: -1 if the thread wasn't in guest code
    uint32_t guest[32] = {};
  };

  void Close() {
    for (Entry& s : samples_) CloseHandle(s.thread);
    samples_.clear();
    samples_.reserve(kMaxThreads);
  }

  std::vector<Entry> samples_;
};

#endif  // _WIN32

}  // namespace

void StartPollWatch(rex::memory::Memory* memory, uint32_t guest_address, uint32_t max_changes) {
#ifdef _WIN32
  if (!memory || g_polling.load()) return;
  if (g_poller.joinable()) g_poller.join();
  const auto* host = memory->TranslateVirtual<const volatile uint32_t*>(guest_address);
  g_membase = reinterpret_cast<uint64_t>(memory->virtual_membase());  // for ReadGuestRegisters
  g_polling.store(true);
  g_poller = std::thread([host, guest_address, max_changes] {
    SetThreadDescription(GetCurrentThread(), L"Poll watch");
    uint32_t last = *host;
    REXLOG_INFO("Poll watch: watching guest {:08X}, now {:08X}", guest_address,
                _byteswap_ulong(last));
    // The thread list is opened again every second, for threads the game starts meanwhile.
    ThreadSet threads;
    threads.Open();
    auto opened = std::chrono::steady_clock::now();
    uint32_t changes = 0;
    for (uint32_t spins = 0; g_polling.load() && changes < max_changes; ++spins) {
      const uint32_t now = *host;
      if (now == last) {
        _mm_pause();
        if ((spins & 0xFFFF) == 0 &&
            std::chrono::steady_clock::now() - opened >= std::chrono::seconds(1)) {
          threads.Open();
          opened = std::chrono::steady_clock::now();
        }
        continue;
      }
      ++changes;
      threads.Sample();
      threads.Log(fmt::format("Poll watch: {:08X} went {:08X} -> {:08X} (change {})",
                              guest_address, _byteswap_ulong(last), _byteswap_ulong(now),
                              changes));
      last = now;
    }
    REXLOG_INFO("Poll watch: done, {} change(s) of {:08X}", changes, guest_address);
    g_polling.store(false);
  });
#else
  (void)memory;
  (void)guest_address;
  (void)max_changes;
  REXLOG_WARN("Poll watch: only on Windows");
#endif
}

void StartWriteWatch(rex::memory::Memory* memory, uint32_t guest_address,
                     std::optional<uint32_t> value, bool reads, uint32_t snapshot,
                     uint32_t snapshot_words) {
#ifdef _WIN32
  if (!memory || g_watched.load()) return;
  const auto host = reinterpret_cast<uintptr_t>(memory->TranslateVirtual<uint8_t*>(guest_address));
  g_guest_address = guest_address;
  g_membase = reinterpret_cast<uint64_t>(memory->virtual_membase());
  g_hit_count.store(0);
  g_write_count.store(0);
  g_filter.store(value.has_value());
  g_reads = reads;
  g_snapshot = snapshot;
  g_snapshot_words = snapshot_words;
  g_snapshot_taken.store(false);
  g_snapshot_data.assign(size_t(snapshot_words) * 4, 0);
  // Guest memory is big-endian; the handler compares the raw bytes.
  if (value) g_value.store(_byteswap_ulong(*value));
  if (!g_handler) g_handler = AddVectoredExceptionHandler(1, OnException);
  g_watched.store(host);
  SetBreakpointOnAllThreads(host);
  // Threads the game starts later (loading, rendering) get the breakpoint too.
  g_refresher = std::thread([host] {
    while (g_watched.load() == host) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      if (g_watched.load() == host) SetBreakpointOnAllThreads(host, true);
    }
  });
  REXLOG_INFO("Write watch: watching {} guest {:08X} (host {:X})",
              reads ? "reads and writes of" : "writes to", guest_address, host);
#else
  (void)memory;
  (void)guest_address;
  (void)value;
  (void)reads;
  (void)snapshot;
  (void)snapshot_words;
  REXLOG_WARN("Write watch: only on Windows");
#endif
}

void StopWriteWatch() {
#ifdef _WIN32
  g_polling.store(false);
  if (g_poller.joinable()) g_poller.join();
  if (!g_watched.load()) return;
  g_watched.store(0);
  if (g_refresher.joinable()) g_refresher.join();
  SetBreakpointOnAllThreads(0);
  const size_t count = std::min(g_hit_count.load(), kMaxHits);
  // Each distinct call chain once, as guest functions.
  std::map<std::string, size_t> chains;
  for (size_t i = 0; i < count; ++i) {
    std::string chain;
    for (uintptr_t frame : g_hits[i].frames) {
      if (!frame) break;
      const auto [guest, offset] = GuestFunctionAt(frame);
      chain += fmt::format("{}sub_{:08X}+0x{:X}", chain.empty() ? "" : " <- ", guest, offset);
    }
    ++chains[chain];
  }
  REXLOG_INFO("Write watch: {} writes to {:08X}, {} counted, {} call chains", g_write_count.load(),
              g_guest_address, g_hit_count.load(), chains.size());
  for (const auto& [chain, n] : chains) REXLOG_INFO("Write watch:   {} x {}", n, chain);
  if (g_snapshot_taken.load()) {
    // As text (other bytes shown as '.'), 96 characters per line.
    for (size_t o = 0; o < g_snapshot_data.size(); o += 96) {
      std::string text;
      for (size_t k = o; k < g_snapshot_data.size() && k < o + 96; ++k) {
        const uint8_t c = g_snapshot_data[k];
        text += c >= 32 && c < 127 ? char(c) : '.';
      }
      REXLOG_INFO("Write watch:   text {:08X}: {}", g_snapshot + uint32_t(o), text);
    }
  }
  // The registers of the first few hits, as guest addresses where they point into guest memory
  // (e.g. the source of a copy).
  static const char* const kNames[16] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                         "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};
  for (size_t i = 0; i < std::min<size_t>(count, 4); ++i) {
    std::string line;
    for (int r = 0; r < 16; ++r) {
      const uint64_t v = g_hits[i].regs[r];
      const bool guest = v >= g_membase && v < g_membase + 0x100000000ull;
      line += guest ? fmt::format(" {}=g:{:08X}", kNames[r], v - g_membase)
                    : fmt::format(" {}={:X}", kNames[r], v);
    }
    REXLOG_INFO("Write watch:   hit {}:{}", i, line);
    if (g_hits[i].guest_from >= 0) {
      std::string guest;
      for (int r = 0; r < 32; ++r) guest += fmt::format(" r{}={:08X}", r, g_hits[i].guest[r]);
      REXLOG_INFO("Write watch:   hit {} guest (context in {}):{}", i, kNames[g_hits[i].guest_from],
                  guest);
      if (i == 0) {
        for (int w = 0; w < 256; w += 8) {
          std::string line;
          for (int k = w; k < w + 8; ++k) line += fmt::format(" {:08X}", _byteswap_ulong(g_hits[i].stack[k]));
          REXLOG_INFO("Write watch:   stack {:08X}:{}", g_hits[i].guest[1] + 4 * w, line);
        }
      }
    }
  }
#endif
}

void LogThreadStacks(int samples, int interval_ms) {
#ifdef _WIN32
  constexpr int kStackFrames = 14;
  struct Sample {
    DWORD id;
    std::string name;
    uintptr_t frames[kStackFrames];
  };
  const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
  for (int s = 0; s < samples; ++s) {
    if (s) std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    // Room for every thread before any is suspended: a suspended thread may hold the heap lock.
    std::vector<Sample> taken(512);
    size_t count = 0;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Thread32First(snapshot, &entry); ok && count < taken.size();
         ok = Thread32Next(snapshot, &entry)) {
      if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self) continue;
      HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME |
                                     THREAD_QUERY_INFORMATION,
                                 FALSE, entry.th32ThreadID);
      if (!thread) continue;
      Sample& sample = taken[count];
      sample.id = entry.th32ThreadID;
      std::fill(std::begin(sample.frames), std::end(sample.frames), 0);
      const StackRange stack = ThreadStackRange(thread);
      if (SuspendThread(thread) != DWORD(-1)) {
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(thread, &ctx)) UnwindSafe(ctx, sample.frames, kStackFrames, stack);
        ResumeThread(thread);
        PWSTR description = nullptr;
        if (SUCCEEDED(GetThreadDescription(thread, &description)) && description) {
          for (const wchar_t* c = description; *c; ++c) sample.name += *c < 128 ? char(*c) : '?';
          LocalFree(description);
        }
        ++count;
      }
      CloseHandle(thread);
    }
    CloseHandle(snapshot);
    REXLOG_INFO("Stacks: sample {}/{}, {} threads", s + 1, samples, count);
    for (size_t i = 0; i < count; ++i) {
      std::string chain;
      for (uintptr_t frame : taken[i].frames) {
        if (!frame) break;
        chain += (chain.empty() ? "" : " <- ") + FrameName(frame);
      }
      REXLOG_INFO("Stacks:   {} '{}': {}", taken[i].id, taken[i].name, chain);
    }
  }
#else
  (void)samples;
  (void)interval_ms;
  REXLOG_WARN("Stacks: only on Windows");
#endif
}

std::string CurrentCallChain(int depth) {
#ifdef _WIN32
  depth = std::clamp(depth, 1, 31);
  CONTEXT ctx{};
  RtlCaptureContext(&ctx);
  uintptr_t frames[32] = {};
  UnwindSafe(ctx, frames, depth + 1, CurrentStackRange());
  std::string chain;
  // Frame 0 is this function.
  for (int i = 1; i <= depth && frames[i]; ++i) {
    chain += (chain.empty() ? "" : " <- ") + FrameName(frames[i]);
  }
  return chain;
#else
  (void)depth;
  return "(only on Windows)";
#endif
}

void InstallCrashReport(rex::memory::Memory* memory) {
#ifdef _WIN32
  if (!memory || g_previous_filter) return;
  if (!g_membase) g_membase = reinterpret_cast<uint64_t>(memory->virtual_membase());
  g_previous_filter = SetUnhandledExceptionFilter(OnCrash);
  std::signal(SIGABRT, OnAbort);
#else
  (void)memory;
#endif
}

}  // namespace skylanders::dev
