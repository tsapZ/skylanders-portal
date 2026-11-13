#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>

#include "portal/figure_catalog.h"
#include "portal/figure_stats.h"

struct ImGuiIO;

namespace rex::ui {
class ImGuiDrawer;
}  // namespace rex::ui

namespace skylanders {

// The in-game figure picker (F6). Browses figure files (.dump, .sky, .bin) under the portal_figures_dir cvar and
// places or removes a figure in a chosen slot (0-15) on the active software portal. Talks only to
// PlaceFigureFromFile/RemoveFigureFromSlot/GetSoftwarePortal (portal_rex/portal_rex.h) and
// ScanFigureCatalog (portal/figure_catalog.h); it never touches portal protocol bytes.
class PortalOverlayDialog : public rex::ui::ImGuiDialog {
 public:
  explicit PortalOverlayDialog(rex::ui::ImGuiDrawer* drawer);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void Rescan();
  // Cheap: stats a file's mtime and only re-decodes entries whose file actually changed since the
  // last check (e.g. the game just saved progress back to it) -- called every frame the Browse
  // tab is visible, unlike Rescan() itself (which re-lists the directory).
  void RefreshChangedFigureStats();

  std::vector<portal::FigureCatalogEntry> entries_;
  std::vector<std::optional<portal::FigureStats>> entry_stats_;  // parallel to entries_
  std::vector<std::filesystem::file_time_type> entry_mtimes_;    // parallel to entries_
  std::string figures_dir_at_last_scan_;
  char filter_[128] = {};
  int selected_slot_ = 0;  // which slot Place/Remove act on
  bool creating_ = false;  // false: Browse tab: true: New Figure tab
  std::string usb_dump_message_;  // result of the last "Dump to file" click, shown until the next one
};

}  // namespace skylanders
