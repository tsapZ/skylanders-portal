#include "portal_rex/portal_rex.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/kernel/xam/noncontroller.h>
#include <rex/ui/keybinds.h>
#include <rex/logging.h>

#include "portal/figure_file.h"
#include "portal_rex/settings_menu.h"
#include "portal/hybrid/real_portal_bridge.h"
#include "portal/portal_device.h"
#include "portal/portal_mode.h"
#include "portal/software/software_portal.h"
#include "portal/usb/usb_portal.h"
#include "portal/xam_bridge.h"
#include "portal/xbox_frame.h"
#include "portal_rex/portal_overlay_dialog.h"
#include "portal_rex/sdl_audio_sink.h"
#include "portal_rex/utf8_path.h"

REXCVAR_DEFINE_STRING(portal_mode, "software", "Portal",
                      "Portal backend: 'software', 'usb' (a real, physical Portal of Power over "
                      "USB -- no driver changes needed), 'hybrid' (the portal menu and a real "
                      "portal together), or 'none'")
    .allowed({"software", "usb", "hybrid", "none"});
REXCVAR_DEFINE_STRING(portal_audio, "auto", "Portal",
                      "Portal speaker audio: 'auto' (the real portal's speaker in usb mode, PC "
                      "speakers in software mode) or 'off'")
    .allowed({"auto", "off"});
REXCVAR_DEFINE_DOUBLE(portal_speaker_volume, 1.0, "Portal",
                      "Volume of portal speaker audio played on the PC (software mode), 0.0 to 1.0");
REXCVAR_DEFINE_BOOL(portal_test_figure, false, "Portal",
                    "Development: put an all-zero figure on the portal (the game reports it as a "
                    "problem toy)");
REXCVAR_DEFINE_STRING(portal_figure, "", "Portal",
                      "Path to a raw 1024-byte figure dump to put on the portal (slot 0) at "
                      "startup. Changes the game makes to it are saved back to this file.");
REXCVAR_DEFINE_STRING(portal_figures_dir, "", "Portal",
                      "Folder to search for .dump figure files for the in-game figure picker "
                      "(F6). Searched recursively; only used by the overlay.");

namespace {

std::atomic<skylanders::portal::PortalDevice*> g_portal{nullptr};
std::atomic<skylanders::portal::SoftwarePortal*> g_software_portal{nullptr};
std::atomic<skylanders::portal::UsbPortal*> g_usb_portal{nullptr};

// Set once a game hook has sent clean speaker PCM (SubmitSpeakerPcm).
std::atomic<bool> g_speaker_pcm_tap{false};

// Forwards the game's XamInputNonControllerGetRaw/SetRaw(Ex) calls (through the SDK's handler
// hook) to whichever portal is active right now. g_portal is re-read on every call, so a live
// portal_mode switch takes effect on the next poll.
class XamPortalHandler final : public rex::kernel::xam::NonControllerHandler {
 public:
  uint32_t Read(uint32_t device_id, std::span<uint8_t> buffer, uint32_t& bytes_read,
                uint16_t& state) override {
    (void)device_id;
    std::lock_guard<std::mutex> lock(mu_);
    return bridge_.Read(g_portal.load(), buffer, bytes_read, state);
  }
  uint32_t Write(uint32_t device_id, std::span<const uint8_t> buffer) override {
    (void)device_id;
    std::lock_guard<std::mutex> lock(mu_);
    // LED updates ('C') arrive ~10 times a second, and speaker audio (0B 17) constantly; leave
    // them out.
    if (buffer.size() >= 3 && buffer[1] == 0x14 && buffer[2] != 'C') {
      REXLOG_DEBUG("Portal write: {:02x} {:02x} {:02x}", buffer[2],
                   buffer.size() > 3 ? buffer[3] : 0, buffer.size() > 4 ? buffer[4] : 0);
    }
    // With clean PCM from a game hook, the software portal plays that instead of the encoded
    // speaker stream (0B 17).
    if (buffer.size() > 2 && buffer[0] == 0x0B && buffer[1] == 0x17 && g_speaker_pcm_tap.load() &&
        g_software_portal.load()) {
      return skylanders::portal::kXamSuccess;
    }
    return bridge_.Write(g_portal.load(), buffer);
  }

 private:
  std::mutex mu_;
  skylanders::portal::XamBridge bridge_;
};

XamPortalHandler g_xam_handler;
  // lives for the whole process, like the portals themselves

// portal_figure/portal_figures_dir arrive as UTF-8; convert explicitly so non-ANSI characters
// survive (path::string() would throw for characters outside the ANSI code page).
std::filesystem::path Utf8ToPath(const std::string& utf8) {
  const std::u8string u8(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
  return std::filesystem::path(u8);
}

// Mirrors UsbPortal's own local HexBytes helper (usb_portal.cpp) -- duplicated rather than shared
// across files, matching this project's existing style for small helpers backing a temporary
// diagnostic feature. Needed here, not in software_portal.cpp/.h, because SoftwarePortal
// deliberately has no ReXGlue dependency of its own (see CMakeLists.txt's "Portal core (no
// ReXGlue dependency)" library) and so cannot call REXLOG_TRACE directly; this file does link
// ReXGlue, so it is where SoftwarePortal::SetResearchLogCallback gets wired up to actually log.
std::string HexBytes(const uint8_t* data, size_t n) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(n * 3);
  for (size_t i = 0; i < n; ++i) {
    if (i) out += ' ';
    out += kHex[data[i] >> 4];
    out += kHex[data[i] & 0xF];
  }
  return out;
}

// Status-report announce dance shared by TransitioningPortal and UsbHotPlugPortal below: mirrors
// SoftwarePortal's own activate/deactivate announce dance (software_portal.cpp: kAddedReports) --
// one fully-empty report, then up to kAddedReports reports with any present slot forced to
// "added" (0b11) instead of "present" (0b01), so the game re-announces (and reloads) it rather
// than silently continuing to show whatever it believed was there. No-ops on non-status reports.
constexpr int kAnnounceAddedReports = 8;  // matches SoftwarePortal's own kAddedReports
enum class AnnouncePhase { kForceEmpty, kForceAdded, kDone };

void ForceStatusEmpty(skylanders::portal::Report& report) {
  for (int i = 1; i <= 4; ++i) report[i] = 0;
}

void ForceStatusPresentToAdded(skylanders::portal::Report& report) {
  for (int byte = 1; byte <= 4; ++byte) {
    uint8_t out = 0;
    for (int pair = 0; pair < 4; ++pair) {
      const int shift = pair * 2;
      const uint8_t state = (report[byte] >> shift) & 0x03;
      out |= static_cast<uint8_t>((state == 0x01 ? 0x03 : state) << shift);
    }
    report[byte] = out;
  }
}

// Advances `phase`/`added_reports` and applies the announce transform to `report` in place.
// Callers own their own phase/counter pair, since TransitioningPortal only ever runs this once
// while UsbHotPlugPortal resets and re-runs it on every reconnect.
void ApplyAnnounceStep(skylanders::portal::Report& report, AnnouncePhase& phase,
                       int& added_reports) {
  if (report[0] != 0x53 || phase == AnnouncePhase::kDone) return;
  if (phase == AnnouncePhase::kForceEmpty) {
    ForceStatusEmpty(report);
    phase = AnnouncePhase::kForceAdded;
    return;
  }
  ForceStatusPresentToAdded(report);
  if (++added_reports >= kAnnounceAddedReports) phase = AnnouncePhase::kDone;
}

// Wraps a freshly-installed PortalDevice for the first few status reports after a portal switch
// (see ApplyAnnounceStep above), then permanent, untouched passthrough. Used for the software
// side of a live switch -- SoftwarePortal doesn't need hot-plug recovery, just the announce.
class TransitioningPortal final : public skylanders::portal::PortalDevice {
 public:
  explicit TransitioningPortal(skylanders::portal::PortalDevice* real) : real_(real) {}

  void Write(const skylanders::portal::Report& report) override { real_->Write(report); }
  void WriteAudio(std::span<const int16_t> pcm) override { real_->WriteAudio(pcm); }

  skylanders::portal::Report Read() override {
    skylanders::portal::Report report = real_->Read();
    ApplyAnnounceStep(report, phase_, added_reports_);
    return report;
  }

 private:
  skylanders::portal::PortalDevice* real_;
  AnnouncePhase phase_ = AnnouncePhase::kForceEmpty;
  int added_reports_ = 0;
};

// Builds a ready-to-use UsbPortal, or nullptr if no matching device is connected. Never stored
// anywhere itself -- callers decide whether to publish it (SwitchPortalMode) or discard it.
skylanders::portal::UsbPortal* SetUpUsbPortal() {
  auto* usb = new skylanders::portal::UsbPortal();  // never freed if published, see below
  if (usb->IsOpen()) return usb;
  delete usb;  // never published anywhere, so nothing else could have seen this one -- safe to free
  return nullptr;
}

// The USB side of a live switch: on top of the same announce dance TransitioningPortal does, this
// one also owns hot-plug recovery. A device that was never found yet (portal_mode set to "usb"
// before anything was plugged in) and a device that was working and got unplugged look the same
// here: "not currently connected, keep retrying." Retries are rate-limited and driven entirely by
// the game's own continuous Read()/Write() polling -- no new thread. A successful (re)connect
// re-runs the announce dance so the game picks up whatever figure is now on the portal instead of
// showing stale state. Never publishes a UsbPortal that failed to open (SetUpUsbPortal already
// frees those), so g_usb_portal only ever points at a real, currently-connected device or null.
class UsbHotPlugPortal final : public skylanders::portal::PortalDevice {
 public:
  UsbHotPlugPortal() { TryConnect(); }

  void Write(const skylanders::portal::Report& report) override {
    MaybeReconnect();
    if (current_) current_->Write(report);
  }

  void WriteAudio(std::span<const int16_t> pcm) override {
    if (current_ && REXCVAR_GET(portal_audio) != "off") current_->WriteAudio(pcm);
  }

  skylanders::portal::Report Read() override {
    MaybeReconnect();
    skylanders::portal::Report report =
        current_ ? current_->Read() : skylanders::portal::Report{};
    ApplyAnnounceStep(report, phase_, added_reports_);
    return report;
  }

 private:
  void TryConnect() {
    current_ = SetUpUsbPortal();
    g_usb_portal.store(current_);
    if (current_) {
      phase_ = AnnouncePhase::kForceEmpty;
      added_reports_ = 0;
      REXLOG_INFO("Portal: usb device connected");
    }
  }

  void MaybeReconnect() {
    if (current_ && !current_->SeemsDisconnected()) return;
    if (current_) {
      REXLOG_WARN("Portal: usb device appears to have been disconnected; will keep retrying");
      current_ = nullptr;
      g_usb_portal.store(nullptr);
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last_attempt_ < std::chrono::seconds(1)) return;  // rate-limit hid_open attempts
    last_attempt_ = now;
    TryConnect();
  }

  skylanders::portal::UsbPortal* current_ = nullptr;  // never freed when replaced, see file comment
  AnnouncePhase phase_ = AnnouncePhase::kForceEmpty;
  int added_reports_ = 0;
  std::chrono::steady_clock::time_point last_attempt_{};
};

// portal_mode "hybrid" (portal/hybrid/real_portal_bridge.h). The bridge, its thread and the USB
// portal it drives live for the whole process like the portals; switching away stops the thread.
std::atomic<skylanders::portal::RealPortalBridge*> g_bridge{nullptr};
std::atomic<uint32_t> g_bridge_generation{0};
std::mutex g_arrived_mutex;
std::function<void(int)> g_arrived_handler;  // guarded by g_arrived_mutex

// What the game talks to in hybrid mode: the software portal (with the same announce dance as
// TransitioningPortal), its light commands also going to the real portal.
class HybridPortal final : public skylanders::portal::PortalDevice {
 public:
  HybridPortal(skylanders::portal::SoftwarePortal* software,
               skylanders::portal::RealPortalBridge* bridge)
      : software_(software), bridge_(bridge) {}

  void Write(const skylanders::portal::Report& report) override {
    software_->Write(report);
    if (report[0] == 'C') bridge_->OnLight(report);
  }
  void WriteAudio(std::span<const int16_t> pcm) override { software_->WriteAudio(pcm); }
  skylanders::portal::Report Read() override {
    skylanders::portal::Report report = software_->Read();
    ApplyAnnounceStep(report, phase_, added_reports_);
    return report;
  }

 private:
  skylanders::portal::SoftwarePortal* software_;
  skylanders::portal::RealPortalBridge* bridge_;
  AnnouncePhase phase_ = AnnouncePhase::kForceEmpty;
  int added_reports_ = 0;
};

// Drives the bridge every few milliseconds and keeps looking for the USB portal (once a second)
// while none is connected; ends when another portal mode takes over.
void BridgeThread(skylanders::portal::RealPortalBridge* bridge, uint32_t generation) {
  skylanders::portal::UsbPortal* usb = nullptr;
  auto last_attempt = std::chrono::steady_clock::now() - std::chrono::seconds(10);
  while (g_bridge_generation.load() == generation) {
    if (usb && usb->SeemsDisconnected()) {
      REXLOG_WARN("Hybrid portal: the USB portal seems unplugged; looking for it again");
      usb = nullptr;  // never freed, like every portal
      g_usb_portal.store(nullptr);
    }
    if (!usb && std::chrono::steady_clock::now() - last_attempt >= std::chrono::seconds(1)) {
      last_attempt = std::chrono::steady_clock::now();
      usb = SetUpUsbPortal();
      g_usb_portal.store(usb);
    }
    bridge->Step(usb);
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
  bridge->Step(nullptr);  // takes the copies off
}

// Builds a ready-to-use SoftwarePortal, wired the same way regardless of whether this is the
// startup portal or a live switch back into software mode from the F4 menu.
skylanders::portal::SoftwarePortal* SetUpSoftwarePortal() {
  auto* software = new skylanders::portal::SoftwarePortal();  // intentionally never freed, see below
  // The source path each slot's figure was loaded from (if any) is tracked by SoftwarePortal
  // itself, set atomically with the figure's data — see PlaceFigure's doc comment for why that
  // matters. This callback just saves whatever source it is handed.
  software->SetWriteCallback([](int slot, const skylanders::portal::FigureData& data,
                                const std::optional<std::filesystem::path>& source) {
    // In hybrid mode a copy of a real figure also goes back onto the real one.
    if (skylanders::portal::RealPortalBridge* bridge = g_bridge.load()) {
      bridge->OnEmulatedWrite(slot, data);
    }
    if (!source) return;  // this slot's figure did not come from a file
    if (skylanders::portal::SaveFigureFileAtomic(*source, data)) {
      REXLOG_INFO("Portal: saved changes back to slot {}'s figure file", slot);
    } else {
      REXLOG_WARN("Portal: could not save changes back to slot {}'s figure file", slot);
    }
  });
  // Trace-log every 'Q'/'W' figure block, matching UsbPortal's own research logging, so both
  // backends produce comparable "Portal figure research: ..." lines for the future offset
  // research phase (see docs/portal-protocol.md and figure_stats.h).
  software->SetResearchLogCallback([](const char* op, int slot, int block, const uint8_t* data,
                                      size_t n) {
    REXLOG_TRACE("Portal figure research: software slot {} block {} {} -> {}", slot, block, op,
                HexBytes(data, n));
  });
  if (REXCVAR_GET(portal_audio) != "off") {
    // Opened once and kept for the whole process, like the portals themselves.
    static std::unique_ptr<skylanders::SdlPortalAudioSink> sink =
        skylanders::SdlPortalAudioSink::Open();
    if (sink) {
      sink->SetVolume(float(REXCVAR_GET(portal_speaker_volume)));
      software->SetAudioSink(sink.get());
    }
  }
  return software;
}

// Switches the active portal to `mode`, live -- called both once at startup and again every time
// the portal_mode cvar changes afterward (e.g. from the F4 Settings overlay). The previous
// PortalDevice, if any, is never freed: the game's hook thread reads g_portal via a fresh
// .load() on every single call rather than caching it (see XamPortalHandler above), so
// swapping which pointer it sees is already safe, but deleting the old object out from under a
// hook call that might still be mid-flight on it would not be -- the same reasoning that already
// kept every portal alive for the whole process before this function could ever be called twice.
// On failure (unknown mode, or USB requested but not found), logs a warning and leaves whatever
// was already active running, rather than dropping to no portal.
void SwitchPortalMode(std::string_view mode_text) {
  const auto mode = skylanders::portal::ParsePortalMode(mode_text);
  if (!mode) {
    REXLOG_WARN("Unknown portal_mode '{}'; leaving the current portal active (use 'software', "
                "'usb', or 'none')",
                mode_text);
    return;
  }
  // Whatever comes next, a running bridge stops (its thread sees the new generation).
  g_bridge_generation.fetch_add(1);
  g_bridge.store(nullptr);
  if (*mode == skylanders::portal::PortalMode::kNone) {
    g_portal.store(nullptr);
    g_software_portal.store(nullptr);
    g_usb_portal.store(nullptr);
    REXLOG_INFO("Portal: none");
    return;
  }
  if (*mode == skylanders::portal::PortalMode::kUsb) {
    // g_software_portal is null in this mode: it is a SoftwarePortal-only status handle (used by
    // GetSoftwarePortal() for the figure-picker overlay), and there is no software portal active.
    // Installed even if no device is found right now -- UsbHotPlugPortal keeps retrying on its
    // own (checked known Skylanders portal VID/PIDs), including for a device plugged in later;
    // it maintains g_usb_portal itself, pointing at a real connected device or null, never a
    // failed/abandoned one. REXLOG_WARN("no USB portal found") intentionally isn't logged here
    // the way the old one-shot version of this branch did -- UsbHotPlugPortal would repeat that
    // warning every retry (once a second) while nothing is plugged in, which is not a real
    // problem worth spamming the log over.
    g_software_portal.store(nullptr);
    g_portal.store(new UsbHotPlugPortal());  // never freed, see this function's comment
    REXLOG_INFO("Portal: usb");
    return;
  }

  skylanders::portal::SoftwarePortal* software = SetUpSoftwarePortal();
  g_usb_portal.store(nullptr);
  g_software_portal.store(software);
  if (*mode == skylanders::portal::PortalMode::kHybrid) {
    // Copies of the real figures are backed up next to the menu's figure files.
    const std::string dir_utf8 = REXCVAR_GET(portal_figures_dir);
    skylanders::portal::RealPortalBridge::Options options;
    if (!dir_utf8.empty()) options.backup_dir = Utf8ToPath(dir_utf8) / "real";
    options.log = [](const std::string& text) { REXLOG_INFO("{}", text); };
    options.on_arrived = [](int slot) {
      std::function<void(int)> handler;
      {
        std::lock_guard lock(g_arrived_mutex);
        handler = g_arrived_handler;
      }
      if (handler) handler(slot);
    };
    auto* bridge = new skylanders::portal::RealPortalBridge(software, std::move(options));
    g_bridge.store(bridge);
    g_portal.store(new HybridPortal(software, bridge));  // never freed, see this function's comment
    std::thread(BridgeThread, bridge, g_bridge_generation.load()).detach();
    REXLOG_INFO("Portal: hybrid (portal menu and real portal)");
    return;
  }
  g_portal.store(new TransitioningPortal(software));  // never freed, see this function's comment
  REXLOG_INFO("Portal: software");
}

}  // namespace

namespace skylanders {

std::filesystem::path SharedFiguresDir(const std::filesystem::path& user_data_root) {
  namespace fs = std::filesystem;
  const fs::path old_dir = user_data_root / "figures";
  if (user_data_root.parent_path().empty()) return old_dir;
  const fs::path shared = user_data_root.parent_path() / "Skylanders" / "Figures";
  std::error_code ec;
  fs::create_directories(shared, ec);
  if (ec) {
    REXLOG_WARN("Portal: cannot create the shared figures folder {} ({}); using {}",
                Utf8Path(shared), ec.message(), Utf8Path(old_dir));
    return old_dir;
  }
  const fs::path note = old_dir / "MOVED_TO_SHARED_FOLDER.txt";
  if (!fs::is_directory(old_dir, ec) || fs::exists(note, ec)) return shared;
  size_t copied = 0, kept = 0, failed = 0;
  for (auto it = fs::recursive_directory_iterator(old_dir, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    std::error_code file_ec;
    if (!it->is_regular_file(file_ec)) continue;
    const fs::path dest = shared / fs::relative(it->path(), old_dir, file_ec);
    if (file_ec) {
      ++failed;
      continue;
    }
    if (fs::exists(dest, file_ec)) {
      ++kept;  // the shared folder's own version wins
      continue;
    }
    fs::create_directories(dest.parent_path(), file_ec);
    if (fs::copy_file(it->path(), dest, file_ec)) {
      ++copied;
    } else {
      ++failed;
    }
  }
  if (failed) {
    // Without the note the next start tries again.
    REXLOG_WARN("Portal: copied {} figure files from {} into {}, {} could not be copied", copied,
                Utf8Path(old_dir), Utf8Path(shared), failed);
    return shared;
  }
  std::ofstream(note) << "These figures were copied to the folder every game now shares:\n"
                      << Utf8Path(shared) << "\nThis folder is the old copy and is no longer used.\n\n"
                      << "Diese Figuren wurden in den Ordner kopiert, den jetzt alle Spiele nutzen:\n"
                      << Utf8Path(shared)
                      << "\nDieser Ordner ist die alte Kopie und wird nicht mehr benutzt.\n";
  REXLOG_INFO("Portal: copied {} figure files from {} into the shared folder {} ({} were already "
              "there)",
              copied, Utf8Path(old_dir), Utf8Path(shared), kept);
  return shared;
}

void InstallPortal(const std::filesystem::path& default_figures_dir) {
  // After F4's "Spiel neu starten": the old game first closes its saves and figure files.
  WaitForRestartedInstance();
  if (REXCVAR_GET(portal_figures_dir).empty()) {
    std::error_code ec;
    std::filesystem::create_directories(default_figures_dir, ec);
    const auto u8 = default_figures_dir.u8string();
    REXCVAR_SET(portal_figures_dir, std::string(reinterpret_cast<const char*>(u8.data()), u8.size()));
  }

  SwitchPortalMode(REXCVAR_GET(portal_mode));

  // The startup-only --portal_figure/--portal_test_figure flags only make sense once, for the
  // portal this process actually boots into -- a later live switch back to software mode starts
  // empty, same as if you'd launched straight into it (see docs/architecture.md, "Figures").
  if (portal::SoftwarePortal* software = g_software_portal.load()) {
    const std::string figure_path = REXCVAR_GET(portal_figure);
    if (!figure_path.empty()) {
      if (!PlaceFigureFromFile(0, Utf8ToPath(figure_path))) {
        REXLOG_WARN("Portal: cannot load '{}' (it must be a regular file of exactly {} bytes); "
                    "running with an empty portal",
                    figure_path, portal::kFigureSize);
      }
    } else if (REXCVAR_GET(portal_test_figure)) {
      software->PlaceFigure(0, portal::FigureData{});
      REXLOG_WARN("Portal: placed an all-zero test figure in slot 0");
    }
  }

  // Live-switch: whenever portal_mode changes after startup (e.g. from the F4 Settings overlay),
  // swap the active portal the same way. Registered once, here, at process lifetime -- never
  // unregistered, matching this project's existing "portal lives for the whole process" pattern.
  rex::cvar::RegisterChangeCallback(
      "portal_mode", [](std::string_view, std::string_view new_value) { SwitchPortalMode(new_value); });

  // Route the game's raw portal I/O here. Stays registered in every mode, including "none" (the
  // bridge then answers "device not connected"), so live portal_mode switches just work.
  rex::kernel::xam::RegisterNonControllerHandler(&g_xam_handler);
}

bool PlaceFigureFromFile(int slot, const std::filesystem::path& path) {
  portal::SoftwarePortal* software = g_software_portal.load();
  if (!software) return false;
  auto figure = portal::LoadFigureFile(path);
  if (!figure) return false;
  return software->PlaceFigure(slot, *figure, path);
}

bool RemoveFigureFromSlot(int slot) {
  portal::SoftwarePortal* software = g_software_portal.load();
  if (!software) return false;
  return software->RemoveFigure(slot);
}

std::optional<std::filesystem::path> CreateFigureFile(const portal::SkylanderInfo& sky) {
  const std::string dir_utf8 = REXCVAR_GET(portal_figures_dir);
  if (dir_utf8.empty()) return std::nullopt;

  const std::filesystem::path game_dir = Utf8ToPath(dir_utf8) / std::string(sky.game);
  std::error_code ec;
  std::filesystem::create_directories(game_dir, ec);
  if (ec) return std::nullopt;

  const std::filesystem::path path = portal::UniqueFigurePath(game_dir, sky.name);
  const portal::FigureData data = portal::CreateBlankFigure(sky.id, sky.variant);
  if (!portal::SaveFigureFileAtomic(path, data)) return std::nullopt;
  return path;
}

bool CreateAndPlaceFigure(int slot, const portal::SkylanderInfo& sky) {
  if (!g_software_portal.load()) return false;
  const std::optional<std::filesystem::path> path = CreateFigureFile(sky);
  return path && PlaceFigureFromFile(slot, *path);
}

bool CreateAndPlaceVillainTrap(int slot, const portal::TrapVillain& villain) {
  portal::SoftwarePortal* software = g_software_portal.load();
  if (!software) return false;
  const std::string dir_utf8 = REXCVAR_GET(portal_figures_dir);
  if (dir_utf8.empty()) return false;

  const uint16_t trap_variant = portal::DefaultTrapVariant(villain.trap_id);
  if (!trap_variant) return false;
  const auto data = portal::CreateTrapWithVillain(villain.trap_id, trap_variant, villain);
  if (!data) return false;

  const std::filesystem::path game_dir = Utf8ToPath(dir_utf8) / "Trap Team";
  std::error_code ec;
  std::filesystem::create_directories(game_dir, ec);
  if (ec) return false;

  const std::filesystem::path path = portal::UniqueFigurePath(game_dir, villain.name);
  if (!portal::SaveFigureFileAtomic(path, *data)) return false;
  return PlaceFigureFromFile(slot, path);
}

portal::SoftwarePortal* GetSoftwarePortal() { return g_software_portal.load(); }

std::vector<uint32_t> RealPortalUids() {
  portal::RealPortalBridge* bridge = g_bridge.load();
  return bridge ? bridge->RealUids() : std::vector<uint32_t>{};
}

bool IsRealPortalCopy(int slot) {
  portal::RealPortalBridge* bridge = g_bridge.load();
  return bridge && bridge->IsCopySlot(slot);
}

void SetRealFigureArrivedHandler(std::function<void(int slot)> handler) {
  std::lock_guard lock(g_arrived_mutex);
  g_arrived_handler = std::move(handler);
}

portal::UsbPortal* GetUsbPortal() { return g_usb_portal.load(); }

std::optional<portal::FigureData> ReadRealFigureBlocks(int slot) {
  portal::UsbPortal* usb = g_usb_portal.load();
  if (!usb) return std::nullopt;
  return usb->CachedFigureData(slot);
}

bool DumpRealFigureToFile(int slot, std::filesystem::path* saved_path, std::string* error) {
  auto Fail = [&](std::string_view msg) {
    if (error) *error = std::string(msg);
    return false;
  };

  portal::UsbPortal* usb = g_usb_portal.load();
  if (!usb) return Fail("no active USB portal");

  const std::string dir_utf8 = REXCVAR_GET(portal_figures_dir);
  if (dir_utf8.empty()) return Fail("no figures folder set (portal_figures_dir)");

  std::string dump_error;
  auto data = usb->DumpFigure(slot, &dump_error);
  if (!data) return Fail(dump_error);

  const uint16_t id = portal::ReadFigureId(*data);
  const uint16_t variant = portal::ReadFigureVariant(*data);
  const auto* sky = portal::FindSkylander(id, variant);
  const std::string game = sky ? std::string(sky->game) : std::string();
  const std::string name =
      sky ? std::string(sky->name)
          : "Unknown id" + std::to_string(id) + " variant" + std::to_string(variant);

  const std::filesystem::path dir = game.empty() ? Utf8ToPath(dir_utf8) : Utf8ToPath(dir_utf8) / game;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) return Fail("could not create the figures folder");

  const std::filesystem::path path = portal::UniqueFigurePath(dir, name);
  if (!portal::SaveFigureFileAtomic(path, *data)) return Fail("could not save the dump file");

  if (saved_path) *saved_path = path;
  return true;
}

}  // namespace skylanders

namespace skylanders {

void SubmitSpeakerPcm(std::span<const int16_t> pcm) {
  g_speaker_pcm_tap.store(true);
  if (portal::SoftwarePortal* software = g_software_portal.load()) software->WriteAudio(pcm);
}

void RegisterPortalOverlay(rex::ui::ImGuiDrawer* drawer) {
  static std::unique_ptr<PortalOverlayDialog> overlay;  // UI thread only
  // Developer tool: figure files, USB figure dumps, any slot. Players use the portal menu
  // (portal_menu.h, F6 and the controller open button).
  rex::ui::RegisterBind("bind_portal_overlay", "F8", "Toggle the portal developer tools", [drawer] {
    if (overlay) {
      overlay.reset();
    } else {
      overlay = std::make_unique<PortalOverlayDialog>(drawer);
    }
  });
}

}  // namespace skylanders
