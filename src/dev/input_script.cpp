#include "dev/input_script.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

#include <rex/cvar.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>

#include "portal_rex/png_file.h"
#include "portal_rex/portal_menu.h"
#include "portal_rex/portal_menu_input.h"
#include "portal/trap_villain.h"
#include "portal_rex/portal_rex.h"
#include "dev/write_watch.h"

REXCVAR_DEFINE_STRING(input_script, "", "Input",
                      "Developer option: play controller input from this script file (see "
                      "src/dev/input_script.h), for automated test runs")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace skylanders::dev {
namespace {

using Clock = std::chrono::steady_clock;
using rex::X_RESULT;
using rex::X_STATUS;

constexpr auto kScriptDevice = static_cast<rex::input::DeviceId>(0x53435200);  // "SCR"

struct Event {
  enum class Kind {
    kPress,
    kStick,
    kLog,
    kFps,
    kScan,
    kPoke,
    kRenderDoc,
    kPlayer,
    kKey,
    kPlace,
    kRemove,
    kPutBack,
    kShot,
    kTrap,
    kWatch,
    kPollWatch,
    kUnwatch,
    kDump,
    kSave,
    kStacks,
    kWaitFor,
    kCustom,
    kQuit
  } kind;
  double start = 0.0;     // seconds since the timeline started
  double duration = 0.0;  // seconds, for presses and stick moves
  uint16_t buttons = 0;
  bool left_trigger = false;
  bool right_trigger = false;
  double x = 0.0, y = 0.0;
  std::string text;
  uint32_t frames = 0;  // renderdoc: guest frames to capture; player: the player (1-4); place: slot;
                        // pollwatch: the changes to log
  std::vector<uint32_t> words;  // scan: the sequence; poke: address and value
  int hit = -1;  // watch, dump: words[0] is an offset from this hit of the last scan
  uint32_t pointer = 0;  // watch, dump: if nonzero, words[0] is an offset from the u32 stored here
  std::string args;      // a registered command: the rest of its line
};

// The hits of the last scan, for hit<n> addresses. Only the script thread uses it.
std::vector<uint32_t> g_scan_hits;

// Seconds the timeline has moved from the times the script gives: down when a waitfor was met
// early, up while a registered command ran. Every event and press is due at its time plus this.
std::atomic<double> g_timeline_shift{0.0};

// The script file's folder: where `shot` puts its pictures. Set before the timeline starts.
std::filesystem::path g_script_dir;

// What the game registered (RegisterScriptCommand, RegisterScriptCondition).
struct Registry {
  std::mutex mutex;
  std::map<std::string, std::function<void(const std::string&)>> commands;
  std::map<std::string, std::function<bool()>> conditions;
};

Registry& GetRegistry() {
  static Registry registry;
  return registry;
}

std::function<void(const std::string&)> FindCommand(const std::string& name) {
  Registry& r = GetRegistry();
  std::lock_guard lock(r.mutex);
  auto it = r.commands.find(name);
  return it == r.commands.end() ? nullptr : it->second;
}

std::function<bool()> FindCondition(const std::string& name) {
  Registry& r = GetRegistry();
  std::lock_guard lock(r.mutex);
  auto it = r.conditions.find(name);
  return it == r.conditions.end() ? nullptr : it->second;
}

// Whether [address, address + size) is committed, readable guest memory: a dump or a pointer read
// anywhere else would end the game with a guest access violation.
bool Readable(rex::memory::Memory* memory, uint32_t address, uint32_t size) {
  for (uint64_t at = address; at < uint64_t(address) + size;) {
    rex::memory::BaseHeap* heap = memory->LookupHeap(uint32_t(at));
    rex::memory::HeapAllocationInfo info{};
    if (!heap || !heap->QueryRegionInfo(uint32_t(at), &info) ||
        !(info.state & rex::memory::kMemoryAllocationCommit) ||
        !(info.protect & rex::memory::kMemoryProtectRead) || !info.region_size) {
      return false;
    }
    // The region is counted in whole pages from the one that holds `at`.
    at = (at & ~uint64_t(heap->page_size() - 1)) + info.region_size;
  }
  return true;
}

// An address word: a hex address, hit<n>[+<hex>] (sets e.hit) or *<hex>[+<hex>] (sets
// e.pointer); for the last two it returns the offset.
uint32_t ParseAddress(const std::string& word, Event& e) {
  size_t used = 0;
  if (word.rfind("hit", 0) == 0) {
    e.hit = std::stoi(word.substr(3), &used);
    used += 3;
  } else if (word.rfind("*", 0) == 0) {
    e.pointer = uint32_t(std::stoul(word.substr(1), &used, 16));
    used += 1;
  } else {
    return uint32_t(std::stoul(word, nullptr, 16));
  }
  if (used == word.size()) return 0;
  if (word[used] != '+') throw std::invalid_argument(word);
  return uint32_t(std::stoul(word.substr(used + 1), nullptr, 16));
}

// Resolves a hit<n> address against the last scan, or a *<hex> one through guest memory; false
// (and a log line) if there was no such hit.
bool ResolveAddress(rex::memory::Memory* memory, const Event& e, uint32_t& address) {
  address = e.words[0];
  if (e.pointer) {
    if (!Readable(memory, e.pointer, 4)) {
      REXLOG_WARN("Input script: {} through {:08X}, which isn't mapped", e.text, e.pointer);
      return false;
    }
    uint32_t v;
    std::memcpy(&v, memory->TranslateVirtual<const uint8_t*>(e.pointer), 4);
    address += rex::byte_swap(v);
    return true;
  }
  if (e.hit < 0) return true;
  if (size_t(e.hit) >= g_scan_hits.size()) {
    REXLOG_WARN("Input script: {} needs scan hit {}, the last scan had {}", e.text, e.hit,
                g_scan_hits.size());
    return false;
  }
  address += g_scan_hits[e.hit];
  return true;
}

bool ParseOneButton(const std::string& name, Event& e) {
  static const struct {
    const char* name;
    uint16_t bit;
  } kButtons[] = {
      {"guide", rex::input::X_INPUT_GAMEPAD_GUIDE},
      {"a", rex::input::X_INPUT_GAMEPAD_A},
      {"b", rex::input::X_INPUT_GAMEPAD_B},
      {"x", rex::input::X_INPUT_GAMEPAD_X},
      {"y", rex::input::X_INPUT_GAMEPAD_Y},
      {"start", rex::input::X_INPUT_GAMEPAD_START},
      {"back", rex::input::X_INPUT_GAMEPAD_BACK},
      {"up", rex::input::X_INPUT_GAMEPAD_DPAD_UP},
      {"down", rex::input::X_INPUT_GAMEPAD_DPAD_DOWN},
      {"left", rex::input::X_INPUT_GAMEPAD_DPAD_LEFT},
      {"right", rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT},
      {"lb", rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER},
      {"rb", rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER},
      {"ls", rex::input::X_INPUT_GAMEPAD_LEFT_THUMB},
      {"rs", rex::input::X_INPUT_GAMEPAD_RIGHT_THUMB},
  };
  if (name == "lt") {
    e.left_trigger = true;
    return true;
  }
  if (name == "rt") {
    e.right_trigger = true;
    return true;
  }
  for (const auto& b : kButtons) {
    if (name == b.name) {
      e.buttons |= b.bit;
      return true;
    }
  }
  return false;
}

// A button, or several pressed together joined with '+' ("back+y", "ls+rs").
bool ParseButton(const std::string& name, Event& e) {
  size_t start = 0;
  while (true) {
    const size_t end = name.find('+', start);
    if (!ParseOneButton(name.substr(start, end == std::string::npos ? end : end - start), e)) {
      return false;
    }
    if (end == std::string::npos) return true;
    start = end + 1;
  }
}

// Returns false (and logs the line) on a syntax error, so a typo doesn't silently shift the
// whole rest of the timeline.
bool ParseScript(std::istream& in, std::vector<Event>& events) {
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  for (char& c : text) {
    if (c == ';') c = '\n';
  }
  std::istringstream lines(text);
  std::string line;
  double t = 0.0;
  while (std::getline(lines, line)) {
    if (auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
    std::istringstream words(line);
    std::string cmd;
    if (!(words >> cmd)) continue;
    Event e{};
    e.start = t;
    bool ok = true;
    if (cmd == "wait") {
      double seconds = 0.0;
      ok = bool(words >> seconds);
      t += seconds;
      continue;
    } else if (cmd == "tap" || cmd == "hold") {
      std::string button;
      double ms = 120.0;
      ok = bool(words >> button) && ParseButton(button, e);
      if (cmd == "hold") ok = ok && bool(words >> ms);
      e.kind = Event::Kind::kPress;
      e.duration = ms / 1000.0;
    } else if (cmd == "stick") {
      double ms = 0.0;
      ok = bool(words >> e.x >> e.y >> ms);
      e.kind = Event::Kind::kStick;
      e.duration = ms / 1000.0;
    } else if (cmd == "log") {
      std::getline(words >> std::ws, e.text);
      e.kind = Event::Kind::kLog;
    } else if (cmd == "fps") {
      ok = bool(words >> e.duration) && e.duration > 0.0;
      e.kind = Event::Kind::kFps;
    } else if (cmd == "scan") {
      std::string word;
      while (words >> word) e.words.push_back(uint32_t(std::stoul(word, nullptr, 16)));
      ok = !e.words.empty();
      e.kind = Event::Kind::kScan;
    } else if (cmd == "poke") {
      std::string address, value;
      ok = bool(words >> address >> value);
      if (ok) {
        e.words.push_back(ParseAddress(address, e));
        e.words.push_back(uint32_t(std::stoul(value, nullptr, 16)));
      }
      e.text = cmd;
      e.kind = Event::Kind::kPoke;
    } else if (cmd == "renderdoc") {
      e.kind = Event::Kind::kRenderDoc;
      e.frames = 1;
      if (words >> e.frames) ok = e.frames > 0;
    } else if (cmd == "player") {
      ok = bool(words >> e.frames) && e.frames >= 1 && e.frames <= 4;
      e.kind = Event::Kind::kPlayer;
    } else if (cmd == "watch" || cmd == "watchread") {
      e.text = cmd;
      std::string word;
      if (words >> word) e.words.push_back(ParseAddress(word, e));
      while (words >> word) e.words.push_back(uint32_t(std::stoul(word, nullptr, 16)));
      ok = e.words.size() == 1 || e.words.size() == 2 || e.words.size() == 4;
      e.kind = Event::Kind::kWatch;
    } else if (cmd == "pollwatch") {
      std::string word;
      ok = bool(words >> word);
      if (ok) e.words.push_back(ParseAddress(word, e));
      e.frames = 16;
      if (ok && words >> e.frames) ok = e.frames > 0 && e.frames <= 256;
      e.kind = Event::Kind::kPollWatch;
    } else if (cmd == "unwatch") {
      e.kind = Event::Kind::kUnwatch;
    } else if (cmd == "stacks") {
      e.kind = Event::Kind::kStacks;
      e.frames = 3;
      if (words >> e.frames) ok = e.frames > 0 && e.frames <= 50;
    } else if (cmd == "dump") {
      std::string address;
      ok = bool(words >> address >> e.frames) && e.frames > 0 && e.frames <= 4096;
      if (ok) e.words.push_back(ParseAddress(address, e));
      e.text = cmd;
      e.kind = Event::Kind::kDump;
    } else if (cmd == "key") {
      ok = bool(words >> e.text) && rex::ui::ParseVirtualKey(e.text) != rex::ui::VirtualKey::kNone;
      e.kind = Event::Kind::kKey;
    } else if (cmd == "save") {
      std::string address, size;
      ok = bool(words >> address >> size);
      std::getline(words >> std::ws, e.text);
      ok = ok && !e.text.empty();
      if (ok) {
        e.words.push_back(uint32_t(std::stoul(address, nullptr, 16)));
        e.words.push_back(uint32_t(std::stoul(size, nullptr, 16)));
      }
      e.kind = Event::Kind::kSave;
    } else if (cmd == "place") {
      ok = bool(words >> e.frames) && e.frames < 16;
      std::getline(words >> std::ws, e.text);
      ok = ok && !e.text.empty();
      e.kind = Event::Kind::kPlace;
    } else if (cmd == "trap") {
      ok = bool(words >> e.frames) && e.frames < 16;
      std::getline(words >> std::ws, e.text);
      ok = ok && !e.text.empty();
      e.kind = Event::Kind::kTrap;
    } else if (cmd == "remove") {
      ok = bool(words >> e.frames) && e.frames < 16;
      e.kind = Event::Kind::kRemove;
    } else if (cmd == "putback") {
      e.kind = Event::Kind::kPutBack;
    } else if (cmd == "shot") {
      ok = bool(words >> e.text);
      e.kind = Event::Kind::kShot;
    } else if (cmd == "waitfor") {
      ok = bool(words >> e.text >> e.duration) && e.duration > 0.0 && FindCondition(e.text);
      e.kind = Event::Kind::kWaitFor;
    } else if (cmd == "quit") {
      e.kind = Event::Kind::kQuit;
    } else if (FindCommand(cmd)) {
      e.text = cmd;
      std::getline(words >> std::ws, e.args);
      e.kind = Event::Kind::kCustom;
    } else {
      ok = false;
    }
    if (!ok) {
      REXLOG_ERROR("Input script: cannot parse '{}'", line);
      return false;
    }
    events.push_back(e);
    t += e.duration;
  }
  return true;
}

// Logs where the big-endian u32 sequence `words` occurs in committed guest memory (the virtual
// heaps, the image and one view of physical memory), with the words around each hit. For finding
// engine objects by known field values while reverse engineering.
constexpr uint32_t kMaxScanHits = 512;

void ScanGuestMemory(rex::memory::Memory* memory, const std::vector<uint32_t>& words) {
  // 0xA0000000-0xBFFFFFFF is one of the three views of physical memory; the other two alias it.
  static const struct {
    uint32_t start, end;
  } kRanges[] = {{0x40000000, 0x7FFFFFFF},
                 {0x80000000, 0x9FFFFFFF},
                 {0xA0000000, 0xBFFFFFFF}};
  auto read = [](const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return rex::byte_swap(v);
  };
  uint32_t hits = 0;
  g_scan_hits.clear();
  for (const auto& range : kRanges) {
    uint64_t address = range.start;
    while (address < range.end && hits < kMaxScanHits) {
      rex::memory::BaseHeap* heap = memory->LookupHeap(uint32_t(address));
      rex::memory::HeapAllocationInfo info{};
      if (!heap || !heap->QueryRegionInfo(uint32_t(address), &info) || !info.region_size) {
        address += 0x10000;
        continue;
      }
      const uint64_t region_end = uint64_t(info.base_address) + info.region_size;
      if ((info.state & rex::memory::kMemoryAllocationCommit) &&
          (info.protect & rex::memory::kMemoryProtectRead)) {
        const uint8_t* host = memory->TranslateVirtual<const uint8_t*>(info.base_address);
        const uint32_t count = info.region_size / 4;
        for (uint32_t i = 0; i + words.size() <= count && hits < kMaxScanHits; ++i) {
          bool match = true;
          for (size_t k = 0; k < words.size() && match; ++k) {
            match = read(host + 4 * (i + k)) == words[k];
          }
          if (!match) continue;
          std::string around;
          for (int k = -4; k < 8; ++k) {
            if (int64_t(i) + k < 0 || i + k >= count) continue;
            const uint32_t v = read(host + 4 * (i + k));
            around += k == 0 ? fmt::format(" [{:08X}]", v) : fmt::format(" {:08X}", v);
          }
          REXLOG_INFO("Input script: scan hit {} at {:08X}:{}", hits, info.base_address + 4 * i,
                      around);
          g_scan_hits.push_back(info.base_address + 4 * i);
          ++hits;
        }
      }
      address = std::max(region_end, address + 0x1000);
    }
  }
  REXLOG_INFO("Input script: scan done, {} hit(s)", hits);
}

// Captures `frames` whole guest frames with an injected RenderDoc. RenderDoc's own triggers capture
// host frames, but the host presents more often than the game renders and one guest frame's GPU
// work is spread over several of them (the lighting of a frame can end up in a different capture
// than its G-buffer). So the capture is started and ended by hand when the guest frame counter
// (advanced by the GPU thread on each guest swap) moves on.
// RenderDoc's in-application API (renderdoc_app.h) is an array of function pointers whose layout
// later versions only extend: StartFrameCapture is entry 19, EndFrameCapture entry 21.
void CaptureGuestFrames(uint32_t frames, const std::function<uint64_t()>& guest_frame_count) {
#ifdef _WIN32
  HMODULE module = GetModuleHandleA("renderdoc.dll");
  if (!module) {
    REXLOG_WARN("Input script: renderdoc requested, but RenderDoc is not attached");
    return;
  }
  using GetApi = int (*)(int version, void** api);
  auto get_api = reinterpret_cast<GetApi>(GetProcAddress(module, "RENDERDOC_GetAPI"));
  void** api = nullptr;
  if (!get_api || !get_api(10000, reinterpret_cast<void**>(&api)) || !api) {
    REXLOG_WARN("Input script: RenderDoc API 1.0.0 not available");
    return;
  }
  using StartFrameCapture = void (*)(void* device, void* window);
  using EndFrameCapture = uint32_t (*)(void* device, void* window);
  auto wait_for_frame = [&](uint64_t after) {
    uint64_t now;
    while ((now = guest_frame_count()) <= after) {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return now;
  };
  const uint64_t first = wait_for_frame(guest_frame_count());
  reinterpret_cast<StartFrameCapture>(api[19])(nullptr, nullptr);
  const uint64_t last = wait_for_frame(first + frames - 1);
  const uint32_t ok = reinterpret_cast<EndFrameCapture>(api[21])(nullptr, nullptr);
  REXLOG_INFO("Input script: RenderDoc capture of guest frames {}-{} {}", first, last,
              ok ? "saved" : "failed");
#else
  (void)frames;
  (void)guest_frame_count;
  REXLOG_WARN("Input script: renderdoc is only supported on Windows");
#endif
}

class ScriptInputDriver final : public rex::input::InputDriver {
 public:
  ScriptInputDriver(std::vector<Event> events, std::function<uint64_t()> guest_frame_count,
                    rex::memory::Memory* memory,
                    std::function<void(std::function<void()>)> run_in_ui)
      : InputDriver(nullptr, 0),
        events_(std::move(events)),
        guest_frame_count_(std::move(guest_frame_count)),
        memory_(memory),
        run_in_ui_(std::move(run_in_ui)),
        start_(Clock::now()) {}

  X_STATUS Setup() override {
    // Logs, captures and quit fire at their time even while the game polls no input. The thread
    // works on copies and is detached, so it never outlives what it uses.
    std::thread([events = events_, start = start_, guest_frame_count = guest_frame_count_,
                 memory = memory_, run_in_ui = run_in_ui_] {
      for (const Event& e : events) {
        if (e.kind == Event::Kind::kPress || e.kind == Event::Kind::kStick) continue;
        std::this_thread::sleep_until(
            start + std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(e.start + g_timeline_shift.load())));
        if (e.kind == Event::Kind::kLog) {
          REXLOG_INFO("Input script: {}", e.text);
        } else if (e.kind == Event::Kind::kFps) {
          // The frame counter polled every millisecond: the rate, and the gaps between two frames
          // (a stall, e.g. a shader compiled the first time, shows as a long one).
          const uint64_t frames_before = guest_frame_count();
          const auto time_before = Clock::now();
          const auto until = time_before + std::chrono::duration_cast<Clock::duration>(
                                               std::chrono::duration<double>(e.duration));
          uint64_t last_count = frames_before;
          auto last_frame = time_before;
          double longest_ms = 0.0;
          int over50 = 0;
          while (Clock::now() < until) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            const uint64_t count = guest_frame_count();
            if (count != last_count) {
              const auto now = Clock::now();
              const double gap = std::chrono::duration<double, std::milli>(now - last_frame).count();
              if (last_count != frames_before) {  // the first gap started before the measurement
                longest_ms = std::max(longest_ms, gap);
                over50 += gap > 50.0;
              }
              last_count = count;
              last_frame = now;
            }
          }
          const uint64_t frames = guest_frame_count() - frames_before;
          const double seconds = std::chrono::duration<double>(Clock::now() - time_before).count();
          REXLOG_INFO("Input script: {:.1f} guest FPS ({} frames in {:.2f} s), longest frame {:.0f} ms, {} over 50 ms",
                      double(frames) / seconds, frames, seconds, longest_ms, over50);
        } else if (e.kind == Event::Kind::kScan) {
          ScanGuestMemory(memory, e.words);
        } else if (e.kind == Event::Kind::kPoke) {
          uint32_t address;
          if (!ResolveAddress(memory, e, address)) continue;
          if (!Readable(memory, address, 4)) {
            REXLOG_WARN("Input script: poke {:08X}, which isn't mapped", address);
            continue;
          }
          auto* host = memory->TranslateVirtual<uint8_t*>(address);
          const uint32_t value = rex::byte_swap(e.words[1]);
          std::memcpy(host, &value, 4);
          REXLOG_INFO("Input script: poke {:08X} = {:08X}", address, e.words[1]);
        } else if (e.kind == Event::Kind::kRenderDoc) {
          CaptureGuestFrames(e.frames, guest_frame_count);
        } else if (e.kind == Event::Kind::kPlayer) {
          skylanders::menu_input::SetDevicePlayer(kScriptDevice, e.frames - 1);
          REXLOG_INFO("Input script: now playing as player {}", e.frames);
        } else if (e.kind == Event::Kind::kWatch) {
          uint32_t address;
          if (!ResolveAddress(memory, e, address)) continue;
          StartWriteWatch(memory, address,
                          e.words.size() > 1 ? std::optional<uint32_t>(e.words[1]) : std::nullopt,
                          e.text == "watchread", e.words.size() == 4 ? e.words[2] : 0,
                          e.words.size() == 4 ? e.words[3] : 0);
        } else if (e.kind == Event::Kind::kPollWatch) {
          uint32_t address;
          if (!ResolveAddress(memory, e, address)) continue;
          StartPollWatch(memory, address, uint32_t(e.frames));
        } else if (e.kind == Event::Kind::kUnwatch) {
          StopWriteWatch();
        } else if (e.kind == Event::Kind::kStacks) {
          LogThreadStacks(int(e.frames));
        } else if (e.kind == Event::Kind::kSave) {
          // Pages that aren't committed (or readable) are written as zeros, so any range works.
          std::ofstream file(std::filesystem::u8path(e.text), std::ios::binary);
          const std::vector<char> zeros(0x1000);
          for (uint64_t page = e.words[0]; page < uint64_t(e.words[0]) + e.words[1];
               page += 0x1000) {
            rex::memory::BaseHeap* heap = memory->LookupHeap(uint32_t(page));
            rex::memory::HeapAllocationInfo info{};
            const bool readable = heap && heap->QueryRegionInfo(uint32_t(page), &info) &&
                                  (info.state & rex::memory::kMemoryAllocationCommit) &&
                                  (info.protect & rex::memory::kMemoryProtectRead);
            const auto size = std::streamsize(
                std::min<uint64_t>(0x1000, uint64_t(e.words[0]) + e.words[1] - page));
            file.write(
                readable ? memory->TranslateVirtual<const char*>(uint32_t(page)) : zeros.data(),
                size);
          }
          REXLOG_INFO("Input script: saved {:08X}+{:X} to {}{}", e.words[0], e.words[1], e.text,
                      file ? "" : " (failed)");
        } else if (e.kind == Event::Kind::kDump) {
          uint32_t address;
          if (!ResolveAddress(memory, e, address)) continue;
          if (!Readable(memory, address, 4 * e.frames)) {
            REXLOG_WARN("Input script: dump {:08X}: not mapped", address);
            continue;
          }
          for (uint32_t i = 0; i < e.frames; i += 8) {
            std::string line;
            for (uint32_t k = i; k < e.frames && k < i + 8; ++k) {
              uint32_t v;
              std::memcpy(&v, memory->TranslateVirtual<const uint8_t*>(address + 4 * k), 4);
              line += fmt::format(" {:08X}", rex::byte_swap(v));
            }
            REXLOG_INFO("Input script: dump {:08X}:{}", address + 4 * i, line);
          }
        } else if (e.kind == Event::Kind::kKey) {
          // Through the key binds, as a key press in the window would go.
          if (run_in_ui) {
            run_in_ui([key = rex::ui::ParseVirtualKey(e.text)] {
              rex::ui::KeyEvent event(nullptr, key, 1, false, false, false, false, false);
              rex::ui::ProcessKeyEvent(event);
            });
          }
          REXLOG_INFO("Input script: key {}", e.text);
        } else if (e.kind == Event::Kind::kPlace) {
          if (run_in_ui) {
            run_in_ui([slot = int(e.frames), path = std::filesystem::u8path(e.text)] {
              const bool placed = skylanders::PlaceFigureFromFile(slot, path);
              REXLOG_INFO("Input script: {} slot {}", placed ? "placed in" : "could not place in",
                          slot);
            });
          }
        } else if (e.kind == Event::Kind::kTrap) {
          if (run_in_ui) {
            run_in_ui([slot = int(e.frames), name = e.text] {
              const skylanders::portal::TrapVillain* villain = nullptr;
              for (const auto& v : skylanders::portal::AllTrapVillains()) {
                if (v.name == name && !v.variant) villain = &v;
              }
              const bool placed = villain && skylanders::CreateAndPlaceVillainTrap(slot, *villain);
              REXLOG_INFO("Input script: trap with {} {} slot {}", name,
                          placed ? "placed in" : "could not be placed in", slot);
            });
          }
        } else if (e.kind == Event::Kind::kRemove) {
          if (run_in_ui) {
            run_in_ui([slot = int(e.frames)] {
              const bool removed = skylanders::RemoveFigureFromSlot(slot);
              REXLOG_INFO("Input script: {} slot {}", removed ? "removed from" : "nothing in",
                          slot);
            });
          }
        } else if (e.kind == Event::Kind::kPutBack) {
          if (run_in_ui) {
            run_in_ui([] {
              const bool put = skylanders::PutBackWantedSkylander();
              REXLOG_INFO("Input script: putback: {}", put ? "the Skylander the game wants back goes back"
                                                            : "the game wants none back");
            });
          }
        } else if (e.kind == Event::Kind::kShot) {
          if (run_in_ui) {
            run_in_ui([path = g_script_dir / std::filesystem::u8path(e.text + ".png")] {
              int width = 0, height = 0;
              std::vector<uint8_t> rgba;
              bool saved = false;
              if (skylanders::CaptureGamePicture(width, height, rgba)) {
                // Every n-th pixel, so the picture is at most 960 wide.
                const int step = (width + 959) / 960;
                const int w = width / step, h = height / step;
                std::vector<uint8_t> scaled(size_t(w) * h * 4);
                for (int y = 0; y < h; ++y) {
                  for (int x = 0; x < w; ++x) {
                    std::memcpy(&scaled[(size_t(y) * w + x) * 4],
                                &rgba[(size_t(y) * step * width + size_t(x) * step) * 4], 4);
                  }
                }
                saved = skylanders::WritePng(path, w, h, scaled.data());
              }
              REXLOG_INFO("Input script: shot {}{}", path.filename().string(), saved ? "" : " (no picture)");
            });
          }
        } else if (e.kind == Event::Kind::kWaitFor) {
          const auto holds = FindCondition(e.text);
          const auto begin = Clock::now();
          auto waited = [&] { return std::chrono::duration<double>(Clock::now() - begin).count(); };
          bool met = false;
          while (!(met = holds()) && waited() < e.duration) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
          }
          const double seconds = waited();
          if (met) g_timeline_shift = g_timeline_shift.load() - std::max(0.0, e.duration - seconds);
          REXLOG_INFO("Input script: waitfor {} {} after {:.1f} s", e.text, met ? "met" : "timed out",
                      seconds);
        } else if (e.kind == Event::Kind::kCustom) {
          const auto run = FindCommand(e.text);
          const auto begin = Clock::now();
          run(e.args);
          g_timeline_shift =
              g_timeline_shift.load() + std::chrono::duration<double>(Clock::now() - begin).count();
        } else {
          REXLOG_INFO("Input script: quit");
          std::_Exit(0);
        }
      }
    }).detach();
    return X_STATUS_SUCCESS;
  }

  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    rex::input::DeviceInfo info;
    info.id = kScriptDevice;
    // It plays like an Xbox 360 pad (A/B/X/Y), so the menus draw that pad for it.
    info.name = "Input script (Xbox 360 pad)";
    info.synthetic = true;
    out.push_back(info);
  }

  // What the script presses right now.
  void Current(uint16_t& buttons, bool& lt, bool& rt, double& x, double& y) const {
    const double now =
        std::chrono::duration<double>(Clock::now() - start_).count() - g_timeline_shift.load();
    buttons = 0;
    lt = rt = false;
    x = y = 0.0;
    for (const Event& e : events_) {
      if (now < e.start || now >= e.start + e.duration) continue;
      if (e.kind == Event::Kind::kPress) {
        buttons |= e.buttons;
        lt |= e.left_trigger;
        rt |= e.right_trigger;
      } else if (e.kind == Event::Kind::kStick) {
        x = e.x;
        y = e.y;
      }
    }
  }

  X_RESULT GetDeviceState(rex::input::DeviceId id, rex::input::X_INPUT_STATE* out_state) override {
    if (id != kScriptDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (!out_state) return X_ERROR_SUCCESS;
    uint16_t buttons;
    bool lt, rt;
    double x, y;
    Current(buttons, lt, rt, x, y);
    std::memset(out_state, 0, sizeof(*out_state));
    out_state->packet_number = ++packet_number_;
    out_state->gamepad.buttons = buttons;
    out_state->gamepad.left_trigger = lt ? 0xFF : 0;
    out_state->gamepad.right_trigger = rt ? 0xFF : 0;
    out_state->gamepad.thumb_lx = static_cast<int16_t>(x * 32767.0);
    out_state->gamepad.thumb_ly = static_cast<int16_t>(y * 32767.0);
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t,
                                 rex::input::X_INPUT_CAPABILITIES* out_caps) override {
    if (id != kScriptDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out_caps) {
      std::memset(out_caps, 0, sizeof(*out_caps));
      out_caps->type = 0x01;
      out_caps->sub_type = 0x01;
      out_caps->gamepad.buttons = 0xFFFF;
      out_caps->gamepad.left_trigger = 0xFF;
      out_caps->gamepad.right_trigger = 0xFF;
      out_caps->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
      out_caps->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT SetDeviceVibration(rex::input::DeviceId id, rex::input::X_INPUT_VIBRATION*) override {
    return id == kScriptDevice ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // Key events for the presses, like the SDL driver makes them from a controller's state: one
  // down or up event per call for each button that changed, so screens that read XInputGetKeystroke
  // (Imaginators' licence agreement) see the script's taps too. No key repeat.
  X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t,
                              rex::input::X_INPUT_KEYSTROKE* out_keystroke) override {
    if (id != kScriptDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (!out_keystroke) return X_ERROR_BAD_ARGUMENTS;
    struct Key {
      uint32_t bit;  // in the field below: the 16 buttons, then 16 = LT, 17 = RT
      rex::ui::VirtualKey vk;
    };
    using rex::ui::VirtualKey;
    static constexpr Key kKeys[] = {
        {0, VirtualKey::kXInputPadDpadUp},     {1, VirtualKey::kXInputPadDpadDown},
        {2, VirtualKey::kXInputPadDpadLeft},   {3, VirtualKey::kXInputPadDpadRight},
        {4, VirtualKey::kXInputPadStart},      {5, VirtualKey::kXInputPadBack},
        {6, VirtualKey::kXInputPadLThumbPress}, {7, VirtualKey::kXInputPadRThumbPress},
        {8, VirtualKey::kXInputPadLShoulder},  {9, VirtualKey::kXInputPadRShoulder},
        {12, VirtualKey::kXInputPadA},         {13, VirtualKey::kXInputPadB},
        {14, VirtualKey::kXInputPadX},         {15, VirtualKey::kXInputPadY},
        {16, VirtualKey::kXInputPadLTrigger},  {17, VirtualKey::kXInputPadRTrigger},
    };
    uint16_t buttons;
    bool lt, rt;
    double x, y;
    Current(buttons, lt, rt, x, y);
    const uint32_t now = buttons | (lt ? 1u << 16 : 0) | (rt ? 1u << 17 : 0);
    const uint32_t changed = now ^ keys_down_;
    for (const Key& k : kKeys) {
      const uint32_t mask = 1u << k.bit;
      if (!(changed & mask)) continue;
      keys_down_ ^= mask;
      std::memset(out_keystroke, 0, sizeof(*out_keystroke));
      out_keystroke->virtual_key = uint16_t(k.vk);
      out_keystroke->flags = (now & mask) ? rex::input::X_INPUT_KEYSTROKE_KEYDOWN
                                          : rex::input::X_INPUT_KEYSTROKE_KEYUP;
      return X_ERROR_SUCCESS;
    }
    return X_ERROR_EMPTY;
  }

 private:
  uint32_t keys_down_ = 0;  // GetDeviceKeystroke's last reported state
  std::vector<Event> events_;
  std::function<uint64_t()> guest_frame_count_;
  rex::memory::Memory* memory_;
  std::function<void(std::function<void()>)> run_in_ui_;
  Clock::time_point start_;
  uint32_t packet_number_ = 0;
};

}  // namespace

void InstallInputScript(rex::system::IInputSystem* input_system, rex::memory::Memory* memory,
                        std::function<uint64_t()> guest_frame_count,
                        std::function<void(std::function<void()>)> run_in_ui) {
  const std::string path = REXCVAR_GET(input_script);
  if (path.empty()) return;
  std::ifstream file(path);
  std::vector<Event> events;
  if (!file || !ParseScript(file, events)) {
    REXLOG_ERROR("Input script: cannot use '{}'", path);
    return;
  }
  auto* input = dynamic_cast<rex::input::InputSystem*>(input_system);
  if (!input) {
    REXLOG_ERROR("Input script: no input system");
    return;
  }
  g_script_dir = std::filesystem::u8path(path).parent_path();
  auto driver = std::make_unique<ScriptInputDriver>(std::move(events), std::move(guest_frame_count),
                                                    memory, std::move(run_in_ui));
  driver->Setup();
  // Wrapped like the real controllers, so scripted runs can open and drive the portal menu.
  input->AddDriver(skylanders::WrapForPortalMenu(std::move(driver), true));
  REXLOG_INFO("Input script: playing '{}'", path);
#ifdef _WIN32
  // The process's memory every 30 s of a scripted run, so a long test shows a leak (a level or a
  // figure after which it keeps growing). Detached: it ends with the process.
  std::thread([] {
    for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(30));
      PROCESS_MEMORY_COUNTERS_EX counters{};
      DWORD handles = 0;
      if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                  sizeof(counters)) &&
          GetProcessHandleCount(GetCurrentProcess(), &handles)) {
        REXLOG_INFO("Input script: memory {} MB private, {} MB working set, {} handles",
                    counters.PrivateUsage >> 20, counters.WorkingSetSize >> 20, handles);
      }
    }
  }).detach();
#endif
}

void RegisterScriptCommand(const std::string& name, std::function<void(const std::string& args)> run) {
  Registry& r = GetRegistry();
  std::lock_guard lock(r.mutex);
  r.commands[name] = std::move(run);
}

void RegisterScriptCondition(const std::string& name, std::function<bool()> holds) {
  Registry& r = GetRegistry();
  std::lock_guard lock(r.mutex);
  r.conditions[name] = std::move(holds);
}

}  // namespace skylanders::dev
