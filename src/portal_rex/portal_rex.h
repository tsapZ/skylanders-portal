#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <rex/cvar.h>

#include "portal/figure_catalog.h"  // for portal::SkylanderInfo
#include "portal/trap_villain.h"   // for portal::TrapVillain
#include "portal/usb/usb_portal.h"  // for portal::FigureData

namespace rex::ui {
class ImGuiDrawer;
}  // namespace rex::ui

namespace skylanders::portal {
class SoftwarePortal;
class UsbPortal;
}  // namespace skylanders::portal

// So other translation units (the overlay) can read the folder the figure picker searches.
REXCVAR_DECLARE(std::string, portal_figures_dir);

// So the overlay can distinguish "portal_mode is usb but no device was found" from "portal_mode
// isn't usb at all" -- both show GetUsbPortal() == nullptr, and those need different messages.
REXCVAR_DECLARE(std::string, portal_mode);

namespace skylanders {

// Creates the portal selected by the `portal_mode` cvar and routes the game's portal reads and
// writes to it. With no portal (mode `none` or an unknown value) the game keeps its own path and
// shows "Can't find the Portal of Power". The portal lives for the whole process, because game
// threads may still call into it while the app shuts down.
//
// If the `portal_figures_dir` cvar is empty, it is set to `default_figures_dir` (which is created
// if it doesn't exist yet) so the overlay has somewhere to look without the user passing a flag.
void InstallPortal(const std::filesystem::path& default_figures_dir);

// For a game's OnConfigurePaths: its user data in the one data folder of every game
// (portal/data_folder.h), <data folder>\<game> ("Spyro's Adventure", ...), unless --user_data_root
// names a folder. The first time, the game's old folder in Documents (what `user_data_root` holds on
// the way in, Documents\ssarecomp) is copied there and keeps a note; if that fails the old folder
// stays in use. The shader cache goes along unless --cache_root names one. Logging starts after
// OnConfigurePaths: SharedFiguresDir logs what this did.
void UseSharedDataFolder(std::filesystem::path& user_data_root, std::filesystem::path& cache_root,
                         std::string_view game);

// The figure folder every game shares, as the real figures lie next to the portal whichever game
// runs: Figures in the data folder (Documents\Skylanders\Giants -> Documents\Skylanders\Figures),
// or for an old per-game folder "Skylanders\Figures" next to it (portal/data_folder.h SharedBase). A
// run with a copied --user_data_root gets its own one next to the copy, so tests never touch the
// real figures. The first time a game uses it, the files of its old
// per-game folder (<user_data_root>\figures) are copied in, never over a file the shared folder
// already has, and a note is left in the old folder, which then stays as a backup and is no longer
// used. Pass the result to InstallPortal.
std::filesystem::path SharedFiguresDir(const std::filesystem::path& user_data_root);

// Loads the figure at `path` and places it in `slot`; future writes to that slot save back to
// `path`, replacing any earlier file that slot saved to. Returns false, and changes nothing, if
// there is no active software portal or `path` cannot be loaded as a figure.
bool PlaceFigureFromFile(int slot, const std::filesystem::path& path);

// Removes the figure from `slot`, if any, and forgets what file it was saving to. Returns false if
// there is no active software portal or the slot was already empty.
bool RemoveFigureFromSlot(int slot);

// The figure RemoveFigureFromSlot last took off `slot`, with the file it was saving to, so it can
// go back when the game wants that one back (portal_menu.h PlayerSkylanderWantedBack). `order`
// grows with every figure taken off, so the newest of several slots can be told. Any thread.
struct TakenOffFigure {
  int slot = -1;
  uint64_t order = 0;
  std::optional<std::filesystem::path> path;
  portal::FigureData data;
};
std::optional<TakenOffFigure> TakenOffFrom(int slot);

// Creates a new blank figure for `sky` under portal_figures_dir/<game>/<name>.dump (creating the
// game subfolder if needed; an existing file of that name is never overwritten -- a " (2)", " (3)"
// suffix is added instead), then places it into `slot` exactly like PlaceFigureFromFile. Returns
// false, and creates nothing, if there is no active software portal, portal_figures_dir is unset,
// or the folder can't be created.
bool CreateAndPlaceFigure(int slot, const portal::SkylanderInfo& sky);

// Creates a new blank figure file for `sky` like CreateAndPlaceFigure, without placing it.
// Returns its path, or nullopt if portal_figures_dir is unset or the file can't be written.
std::optional<std::filesystem::path> CreateFigureFile(const portal::SkylanderInfo& sky);

// Like CreateAndPlaceFigure, but a Trap Team trap holding `villain`: the first crystal trap shape
// of the villain's element, saved as "<villain>.dump" under the "Trap Team" folder.
bool CreateAndPlaceVillainTrap(int slot, const portal::TrapVillain& villain);

// The active software portal, for read-only status queries (HasFigure/Figure) from the overlay.
// nullptr if portal_mode is not "software".
portal::SoftwarePortal* GetSoftwarePortal();

// How loud the portal speaker plays on the PC right now, 0..1 (software mode with portal_audio on;
// 0 otherwise), for speaker_overlay.h.
float PortalSpeakerLevel();

// portal_mode "hybrid": the UIDs of the figures standing on the real portal now (each has a copy
// in the emulated portal), and whether an emulated slot holds such a copy. Empty / false in the
// other modes. Any thread.
std::vector<uint32_t> RealPortalUids();
bool IsRealPortalCopy(int slot);
// Called on the bridge's thread when a figure from the real portal arrived in an emulated slot,
// e.g. so the portal menu can give it to a player.
void SetRealFigureArrivedHandler(std::function<void(int slot)> handler);

// The active USB portal, for read-only status queries (FigurePresent/DetectedIdVariant) from the
// overlay. nullptr if portal_mode is not "usb" or no device was found.
portal::UsbPortal* GetUsbPortal();

// Best-effort figure data for a real figure in `slot`, assembled passively from whatever the
// game's own portal polling has already read (see UsbPortal::CachedFigureData) -- never an
// active read, cheap enough to call every frame. Returns nullopt if there's no active USB
// portal, no figure in `slot`, or not enough of it has been observed yet.
std::optional<portal::FigureData> ReadRealFigureBlocks(int slot);

// Dumps the real figure in `slot` (see UsbPortal::DumpFigure -- the game briefly sees that slot
// as empty while this runs) and saves it under portal_figures_dir, grouped by game and named
// like CreateAndPlaceFigure's own figures ("<name>.dump", " (2).dump" on collision; an
// unrecognized id/variant falls back to "Unknown id<N> variant<N>" at the folder root). On
// success, `saved_path` (if given) is set to where it was written. Returns false -- with a
// reason in `error`, if given -- if there's no active USB portal, portal_figures_dir is unset,
// the dump itself fails, or the file can't be saved.
bool DumpRealFigureToFile(int slot, std::filesystem::path* saved_path = nullptr,
                          std::string* error = nullptr);

// For a game-specific hook that has the portal speaker audio before the game encodes it (8 kHz
// mono PCM): the software portal then plays these samples, which sound cleaner than decoding the
// encoded stream (G.726 adds noise). Once called, the encoded stream is no longer played in
// software mode; a real USB portal keeps getting the encoded stream either way.
void SubmitSpeakerPcm(std::span<const int16_t> pcm);

// Binds F6 to toggle the portal figure picker overlay. Call from ReXApp::OnCreateDialogs.
void RegisterPortalOverlay(rex::ui::ImGuiDrawer* drawer);

}  // namespace skylanders
