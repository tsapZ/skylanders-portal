// RealPortalBridge against a second SoftwarePortal standing in for the real portal: the bridge
// talks to it with the real portal's commands, which SoftwarePortal answers the same way.

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "portal/figure_file.h"
#include "portal/hybrid/real_portal_bridge.h"
#include "portal/software/software_portal.h"
#include "test_util.h"

using namespace skylanders::portal;

namespace {

Report Cmd(std::initializer_list<uint8_t> bytes) {
  Report r{};
  size_t i = 0;
  for (uint8_t b : bytes) r[i++] = b;
  return r;
}

// The emulated slot holding a figure, or -1.
int FirstFigure(const SoftwarePortal& p) {
  for (int slot = 0; slot < kMaxFigures; ++slot) {
    if (p.HasFigure(slot)) return slot;
  }
  return -1;
}

void Run(RealPortalBridge& bridge, PortalDevice* real, int steps = 400) {
  for (int i = 0; i < steps; ++i) bridge.Step(real);
}

// The game writing a block to the emulated portal ('W', slot, block, 16 bytes).
void GameWrite(SoftwarePortal& emulated, int slot, int block, uint8_t fill) {
  Report w = Cmd({'W', uint8_t(slot), uint8_t(block)});
  std::fill(w.begin() + 3, w.begin() + 3 + kBlockSize, fill);
  emulated.Write(w);
}

bool BlockIs(const FigureData& data, int block, uint8_t fill) {
  return std::all_of(data.begin() + block * kBlockSize, data.begin() + (block + 1) * kBlockSize,
                     [&](uint8_t b) { return b == fill; });
}

}  // namespace

int main() {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "skylanders_bridge_test";
  std::filesystem::remove_all(dir);
  const FigureData chop = CreateBlankFigure(30, 0, {0x11, 0x22, 0x33, 0x44});

  // A figure on the real portal gets a copy in the emulated portal, from the top slot down, and
  // a backup file that is the copy's source.
  {
    SoftwarePortal real, emulated;
    std::vector<int> arrived;
    RealPortalBridge bridge(&emulated, {dir, nullptr, [&](int slot) { arrived.push_back(slot); }});
    real.PlaceFigure(0, chop);
    Run(bridge, &real);
    const int copy = FirstFigure(emulated);
    CHECK(copy == kMaxFigures - 1);
    CHECK(emulated.Figure(copy) == chop);
    CHECK(arrived.size() == 1 && arrived[0] == copy);
    CHECK(bridge.IsCopySlot(copy));
    const auto uids = bridge.RealUids();
    CHECK(uids.size() == 1 && uids[0] == RealPortalBridge::UidOf(chop));
    const auto source = emulated.Source(copy);
    CHECK(source && std::filesystem::exists(*source));
    CHECK(source && LoadFigureFile(*source) == chop);

    // What the game writes to the copy goes onto the real figure; the identity blocks and the
    // sector trailers never do.
    emulated.SetWriteCallback([&](int slot, const FigureData& data,
                                  const std::optional<std::filesystem::path>&) {
      bridge.OnEmulatedWrite(slot, data);
    });
    GameWrite(emulated, copy, 8, 0xAB);
    GameWrite(emulated, copy, 9, 0xCD);
    GameWrite(emulated, copy, 1, 0xEE);   // identity
    GameWrite(emulated, copy, 11, 0xEE);  // sector trailer
    Run(bridge, &real);
    const auto on_real = real.Figure(0);
    CHECK(on_real && BlockIs(*on_real, 8, 0xAB));
    CHECK(on_real && BlockIs(*on_real, 9, 0xCD));
    CHECK(on_real && !BlockIs(*on_real, 1, 0xEE));
    CHECK(on_real && !BlockIs(*on_real, 11, 0xEE));

    // The portal menu lifts the copy and puts it back into another slot: writes follow it.
    const FigureData copy_data = *emulated.Figure(copy);
    emulated.RemoveFigure(copy);
    Run(bridge, &real, 20);
    CHECK(!bridge.IsCopySlot(copy));
    emulated.PlaceFigure(3, copy_data, source);
    Run(bridge, &real, 20);
    CHECK(bridge.IsCopySlot(3));
    GameWrite(emulated, 3, 12, 0x5A);
    Run(bridge, &real);
    CHECK(real.Figure(0) && BlockIs(*real.Figure(0), 12, 0x5A));

    // Lifted off the real portal: the copy goes, other figures stay.
    const FigureData other = CreateBlankFigure(14, 0, {0x01, 0x02, 0x03, 0x04});
    emulated.PlaceFigure(0, other);  // put on through the menu
    real.RemoveFigure(0);
    Run(bridge, &real);
    CHECK(!emulated.HasFigure(3));
    CHECK(emulated.HasFigure(0));
    CHECK(bridge.RealUids().empty());
  }

  // Two figures on the real portal; unplugging it takes both copies off.
  {
    SoftwarePortal real, emulated;
    RealPortalBridge bridge(&emulated, {dir, nullptr, nullptr});
    real.PlaceFigure(0, chop);
    real.PlaceFigure(1, CreateBlankFigure(14, 0, {0x05, 0x06, 0x07, 0x08}));
    Run(bridge, &real);
    CHECK(emulated.HasFigure(kMaxFigures - 1) && emulated.HasFigure(kMaxFigures - 2));
    CHECK(bridge.RealUids().size() == 2);
    Run(bridge, nullptr, 1);
    CHECK(FirstFigure(emulated) == -1);
    CHECK(bridge.RealUids().empty());
    // Plugged in again: read again.
    Run(bridge, &real);
    CHECK(bridge.RealUids().size() == 2);
  }

  // The game's light command goes on to the real portal (SoftwarePortal ignores it; this only
  // checks nothing breaks and the figures stay).
  {
    SoftwarePortal real, emulated;
    RealPortalBridge bridge(&emulated, {dir, nullptr, nullptr});
    real.PlaceFigure(2, chop);
    bridge.OnLight(Cmd({'C', 0xFF, 0x00, 0x00}));
    Run(bridge, &real);
    CHECK(FirstFigure(emulated) == kMaxFigures - 1);
  }

  // A backup with other data (the figure played from the menu meanwhile) is kept, not overwritten.
  {
    SoftwarePortal real, emulated;
    RealPortalBridge bridge(&emulated, {dir, nullptr, nullptr});
    FigureData played = chop;
    played[8 * kBlockSize] ^= 0xFF;
    const std::filesystem::path backup = dir / "Chop Chop 44332211.dump";
    CHECK(SaveFigureFileAtomic(backup, played));
    real.PlaceFigure(0, chop);
    Run(bridge, &real);
    CHECK(LoadFigureFile(backup) == chop);
    CHECK(LoadFigureFile(dir / "Chop Chop 44332211 before.dump") == played);
  }

  std::filesystem::remove_all(dir);
  return Finish("real portal bridge");
}
