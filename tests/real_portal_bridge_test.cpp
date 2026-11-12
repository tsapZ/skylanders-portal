// RealPortalBridge against a second SoftwarePortal standing in for the real portal: the bridge
// talks to it with the real portal's commands, which SoftwarePortal answers the same way.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <thread>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "portal/audio.h"
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

// The real portal with a few of a real one's habits (Core of Light #62): it can miss the first A,
// send a stale "figure present" status before it is set up, and it keeps what was sent to it.
class Habits final : public PortalDevice {
 public:
  explicit Habits(SoftwarePortal* inner) : inner_(inner) {}
  void Write(const Report& report) override {
    sent.push_back(report);
    if (report[0] == 'A' && miss_first_activate) {
      miss_first_activate = false;
      return;
    }
    if (report[0] == 'Q' && refuse_reads > 0) {
      // "Couldn't read that block": the reply without the figure flag (0x10), as a real portal
      // gives it for a figure just put on.
      --refuse_reads;
      Report refused{};
      refused[0] = 0x51;
      refused[1] = report[1] & 0x0F;
      refused[2] = report[2];
      fake_.push_back(refused);
      return;
    }
    if (report[0] == 'W' && (refuse_writes > 0 || report[2] == refuse_write_block)) {
      // "Couldn't write that block": the reply without the written flag (0x10).
      if (refuse_writes > 0) --refuse_writes;
      Report refused{};
      refused[0] = 0x57;
      refused[1] = report[1] & 0x0F;
      refused[2] = report[2];
      fake_.push_back(refused);
      return;
    }
    inner_->Write(report);
  }
  Report Read() override {
    if (!fake_.empty()) {
      const Report r = fake_.front();
      fake_.pop_front();
      return r;
    }
    if (stale_status) {
      stale_status = false;
      Report status{};
      status[0] = 0x53;
      status[1] = 0x01;  // slot 0 present
      status[6] = 0x01;  // active
      return status;
    }
    return inner_->Read();
  }
  std::vector<Report> sent;
  bool miss_first_activate = false;
  bool stale_status = false;
  int refuse_reads = 0;  // this many block reads are answered "couldn't read"
  int refuse_writes = 0;  // this many block writes are answered "couldn't write"
  int refuse_write_block = -1;  // writes to this block are always answered "couldn't write"

 private:
  SoftwarePortal* inner_;
  std::deque<Report> fake_;
};

// Steps for a while, for the bridge's time-based rules. Not sleeping between them: a 1 ms sleep is
// about 15 ms on Windows' default timer, too few steps to read a figure (one block a step).
void RunFor(RealPortalBridge& bridge, PortalDevice* real, std::chrono::milliseconds total) {
  const auto end = std::chrono::steady_clock::now() + total;
  while (std::chrono::steady_clock::now() < end) {
    bridge.Step(real);
    std::this_thread::yield();
  }
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

  // #62 1: a portal that misses the first A stays switched off; with no sign of an active portal the
  // bridge sets it up again, and the figure is read.
  {
    SoftwarePortal inner, emulated;
    Habits real(&inner);
    real.miss_first_activate = true;
    RealPortalBridge::Options options{dir, nullptr, nullptr};
    options.restart_after = std::chrono::milliseconds(20);
    RealPortalBridge bridge(&emulated, options);
    inner.PlaceFigure(0, chop);
    RunFor(bridge, &real, std::chrono::milliseconds(300));
    CHECK(FirstFigure(emulated) == kMaxFigures - 1);
    // A goes out only after R is answered, and again after the restart.
    const auto activates = std::count_if(real.sent.begin(), real.sent.end(),
                                         [](const Report& r) { return r[0] == 'A'; });
    CHECK(activates >= 2);
    CHECK(real.sent.size() >= 2 && real.sent[0][0] == 'R' && real.sent[1][0] == 'A');
  }

  // #62 2: a stale "figure present" status before the portal is set up doesn't start a read (the
  // portal refused it three times and the figure waited until it was lifted).
  {
    SoftwarePortal inner, emulated;
    Habits real(&inner);
    real.stale_status = true;
    real.miss_first_activate = true;  // so the portal isn't on when the stale status comes
    RealPortalBridge::Options options{dir, nullptr, nullptr};
    options.restart_after = std::chrono::milliseconds(20);
    RealPortalBridge bridge(&emulated, options);
    inner.PlaceFigure(0, chop);
    RunFor(bridge, &real, std::chrono::milliseconds(300));
    CHECK(FirstFigure(emulated) == kMaxFigures - 1);
    // The first block read goes out only after the second (answered) A.
    std::vector<size_t> activates;
    size_t first_read = real.sent.size();
    for (size_t i = 0; i < real.sent.size(); ++i) {
      if (real.sent[i][0] == 'A') activates.push_back(i);
      if (real.sent[i][0] == 'Q' && first_read == real.sent.size()) first_read = i;
    }
    CHECK(activates.size() >= 2 && first_read > activates[1]);
  }

  // #62 3: C, J and L go to the real portal once it is on, the newest per command and side.
  {
    SoftwarePortal inner, emulated;
    Habits real(&inner);
    RealPortalBridge bridge(&emulated, {dir, nullptr, nullptr});
    bridge.OnLight(Cmd({'J', 0x00, 0x10, 0x00, 0x00}));  // left, replaced below
    bridge.OnLight(Cmd({'J', 0x00, 0xFF, 0xC8, 0x00}));  // left, the newest
    bridge.OnLight(Cmd({'J', 0x01, 0x00, 0xFF, 0x00}));  // right
    bridge.OnLight(Cmd({'L', 0x00, 0x01}));              // trap light
    bridge.OnLight(Cmd({'C', 0x00, 0x00, 0xFF}));
    RunFor(bridge, &real, std::chrono::milliseconds(700));
    std::vector<Report> lights;
    size_t activate_at = real.sent.size();
    for (size_t i = 0; i < real.sent.size(); ++i) {
      const Report& r = real.sent[i];
      if (r[0] == 'A' && activate_at == real.sent.size()) activate_at = i;
      if (r[0] == 'C' || r[0] == 'J' || r[0] == 'L') {
        CHECK(i > activate_at);  // never before the portal is on
        lights.push_back(r);
      }
    }
    CHECK(lights.size() == 4);
    CHECK(std::count_if(lights.begin(), lights.end(),
                        [](const Report& r) { return r[0] == 'J' && r[1] == 0 && r[2] == 0xFF; }) == 1);
    CHECK(std::none_of(lights.begin(), lights.end(),
                       [](const Report& r) { return r[0] == 'J' && r[1] == 0 && r[2] == 0x10; }));
  }

  // #62 4: a toy whose backup file is already on the emulated portal through the menu is left out,
  // and that file isn't renamed or saved over.
  {
    SoftwarePortal real, emulated;
    std::vector<std::string> left_out;
    RealPortalBridge::Options options{dir, nullptr, nullptr};
    options.on_backup_in_use = [&](const std::string& name) { left_out.push_back(name); };
    RealPortalBridge bridge(&emulated, options);
    FigureData played = chop;
    played[8 * kBlockSize] ^= 0x55;
    const std::filesystem::path backup = dir / "Chop Chop 44332211.dump";
    std::filesystem::remove(dir / "Chop Chop 44332211 before.dump");
    CHECK(SaveFigureFileAtomic(backup, played));
    emulated.PlaceFigure(2, played, backup);  // loaded through the menu
    real.PlaceFigure(0, chop);
    Run(bridge, &real);
    CHECK(left_out.size() == 1 && left_out[0] == "Chop Chop");
    CHECK(!emulated.HasFigure(kMaxFigures - 1));
    CHECK(emulated.Source(2) == backup);
    CHECK(LoadFigureFile(backup) == played);
    CHECK(!std::filesystem::exists(dir / "Chop Chop 44332211 before.dump"));
    CHECK(bridge.RealUids().empty());
  }

  // TrapTeamRecomp #22: a figure the real portal can't read for a moment (just put on, not lying
  // flat) is asked for again after a pause instead of being given up after three quick tries.
  {
    SoftwarePortal inner, emulated;
    Habits real(&inner);
    RealPortalBridge::Options options{dir, nullptr, nullptr};
    options.read_retry_pause = std::chrono::milliseconds(5);
    RealPortalBridge bridge(&emulated, options);
    inner.PlaceFigure(0, chop);
    real.refuse_reads = 6;  // more than the old three tries, fewer than twelve
    RunFor(bridge, &real, std::chrono::milliseconds(500));
    CHECK(real.refuse_reads == 0);
    CHECK(FirstFigure(emulated) == kMaxFigures - 1);
  }

  // A write the real portal refuses a few times in a row is sent again after a pause until it is
  // taken (the owner's test 2026-11-12: three quick tries were used up within a moment); one it
  // never takes is given up with a log line, and the writes behind it still go out.
  {
    SoftwarePortal inner, emulated;
    Habits real(&inner);
    std::vector<std::string> lines;
    RealPortalBridge::Options options{dir, [&](const std::string& l) { lines.push_back(l); }, nullptr};
    options.write_retry_pause = std::chrono::milliseconds(5);
    RealPortalBridge bridge(&emulated, options);
    inner.PlaceFigure(0, chop);
    RunFor(bridge, &real, std::chrono::milliseconds(300));
    const int copy = FirstFigure(emulated);
    CHECK(copy == kMaxFigures - 1);
    emulated.SetWriteCallback([&](int slot, const FigureData& data,
                                  const std::optional<std::filesystem::path>&) {
      bridge.OnEmulatedWrite(slot, data);
    });
    real.refuse_writes = 4;  // more than the old three tries, fewer than six
    GameWrite(emulated, copy, 8, 0xA1);
    RunFor(bridge, &real, std::chrono::milliseconds(300));
    CHECK(real.refuse_writes == 0);
    auto on_real = inner.Figure(0);
    CHECK(on_real && BlockIs(*on_real, 8, 0xA1));
    // The tries were spread out, not sent back to back: between two W of block 8 something else
    // went out or time passed (the pause), checked by the count of W sent for it: 4 refused + 1.
    CHECK(std::count_if(real.sent.begin(), real.sent.end(),
                        [](const Report& r) { return r[0] == 'W' && r[2] == 8; }) == 5);

    real.refuse_write_block = 9;  // never taken
    GameWrite(emulated, copy, 9, 0xB2);
    GameWrite(emulated, copy, 10, 0xC3);
    RunFor(bridge, &real, std::chrono::milliseconds(300));
    CHECK(std::any_of(lines.begin(), lines.end(), [](const std::string& l) {
      return l.find("refused block 9") != std::string::npos;
    }));
    CHECK(std::count_if(real.sent.begin(), real.sent.end(),
                        [](const Report& r) { return r[0] == 'W' && r[2] == 9; }) == 6);
    on_real = inner.Figure(0);
    CHECK(on_real && !BlockIs(*on_real, 9, 0xB2));
    CHECK(on_real && BlockIs(*on_real, 10, 0xC3));  // the write behind it still went out
  }

  // One it can't read at all is given up with a log line, and taken once it is put on again.
  {
    SoftwarePortal inner, emulated;
    Habits real(&inner);
    std::vector<std::string> lines;
    RealPortalBridge::Options options{dir, [&](const std::string& l) { lines.push_back(l); }, nullptr};
    options.read_retry_pause = std::chrono::milliseconds(1);
    RealPortalBridge bridge(&emulated, options);
    inner.PlaceFigure(0, chop);
    real.refuse_reads = 1000;
    RunFor(bridge, &real, std::chrono::milliseconds(300));
    CHECK(FirstFigure(emulated) < 0);
    CHECK(std::any_of(lines.begin(), lines.end(), [](const std::string& l) {
      return l.find("could not read the figure in real slot 0") != std::string::npos;
    }));
    real.refuse_reads = 0;
    inner.RemoveFigure(0);
    RunFor(bridge, &real, std::chrono::milliseconds(200));
    inner.PlaceFigure(0, chop);
    RunFor(bridge, &real, std::chrono::milliseconds(500));
    CHECK(FirstFigure(emulated) == kMaxFigures - 1);
  }

  // The game's speaker switch reaches the real portal; a portal with a speaker (a SoftwarePortal with
  // an audio sink answers 4D xx 01 19, as a Trap Team portal does) is told apart from one without.
  {
    struct NullSink final : PortalAudioSink {
      void Submit(std::span<const int16_t>) override {}
    } sink;
    SoftwarePortal inner, emulated;
    inner.SetAudioSink(&sink);
    Habits real(&inner);
    std::vector<std::string> lines;
    RealPortalBridge bridge(&emulated, {dir, [&](const std::string& l) { lines.push_back(l); }, nullptr});
    CHECK(!bridge.RealHasSpeaker());
    bridge.OnSpeakerSwitch(Cmd({'M', 0x01}));
    Run(bridge, &real);
    CHECK(bridge.RealHasSpeaker());
    CHECK(std::count_if(real.sent.begin(), real.sent.end(), [](const Report& r) { return r[0] == 'M'; }) == 1);
    CHECK(std::any_of(lines.begin(), lines.end(), [](const std::string& l) {
      return l.find("has a speaker") != std::string::npos;
    }));
    // Unplugged: unknown again; plugged back in: the switch goes out again once it is set up.
    bridge.Step(nullptr);
    CHECK(!bridge.RealHasSpeaker());
    Run(bridge, &real);
    CHECK(bridge.RealHasSpeaker());
    CHECK(std::count_if(real.sent.begin(), real.sent.end(), [](const Report& r) { return r[0] == 'M'; }) == 2);
  }
  {
    SoftwarePortal real, emulated;  // no audio sink: 4D xx 00 19
    RealPortalBridge bridge(&emulated, {dir, nullptr, nullptr});
    bridge.OnSpeakerSwitch(Cmd({'M', 0x01}));
    real.PlaceFigure(0, chop);
    Run(bridge, &real);
    CHECK(!bridge.RealHasSpeaker());
    CHECK(FirstFigure(emulated) == kMaxFigures - 1);  // figures still come through
  }
  // A portal that doesn't answer M at all (one from before Trap Team): no speaker after the wait, and
  // a figure on it is still read.
  {
    class NoSpeakerCommand final : public PortalDevice {
     public:
      explicit NoSpeakerCommand(SoftwarePortal* inner) : inner_(inner) {}
      void Write(const Report& report) override {
        if (report[0] != 'M') inner_->Write(report);
      }
      Report Read() override { return inner_->Read(); }

     private:
      SoftwarePortal* inner_;
    };
    SoftwarePortal inner, emulated;
    NoSpeakerCommand real(&inner);
    RealPortalBridge bridge(&emulated, {dir, nullptr, nullptr});
    bridge.OnSpeakerSwitch(Cmd({'M', 0x01}));
    inner.PlaceFigure(0, chop);
    RunFor(bridge, &real, std::chrono::milliseconds(600));
    CHECK(!bridge.RealHasSpeaker());
    CHECK(FirstFigure(emulated) == kMaxFigures - 1);
  }

  // The faster hybrid (TheBiemGamer's idea, Core of Light #62; TrapTeamRecomp #22): the copy goes
  // onto the emulated portal after the toy's first 8 blocks and the rest is read while the game reads
  // the copy. A block the game asks for is read next; its reply, and everything else from the
  // portal, waits for it.
  const auto place_early = [](RealPortalBridge& bridge, PortalDevice* real, SoftwarePortal& emulated) {
    for (int i = 0; i < 400; ++i) {
      bridge.Step(real);
      if (FirstFigure(emulated) >= 0) return FirstFigure(emulated);
    }
    return -1;
  };
  const auto reads = [](const Habits& real) {
    std::vector<int> blocks;
    for (const Report& r : real.sent) {
      if (r[0] == 'Q') blocks.push_back(r[2]);
    }
    return blocks;
  };
  const std::filesystem::path chop_backup = dir / "Chop Chop 44332211.dump";
  {
    SoftwarePortal inner, emulated;
    Habits real(&inner);
    RealPortalBridge bridge(&emulated, {dir, nullptr, nullptr});
    FigureData marked = chop;  // every data block its own number, to see which one came where
    for (size_t b = 2; b < kBlockCount; ++b) {
      if (b % 4 != 3) std::fill_n(marked.begin() + b * kBlockSize, kBlockSize, uint8_t(b));
    }
    inner.PlaceFigure(0, marked);
    const int copy = place_early(bridge, &real, emulated);
    REQUIRE_OR_RETURN(copy == kMaxFigures - 1);
    CHECK(emulated.IsLoading(copy));
    CHECK(reads(real).size() < 12);  // the first 8 and the one after them, not all 64
    emulated.Write(Cmd({'Q', uint8_t(0x20 | copy), 40}));
    CHECK(emulated.Read()[0] == 0);  // busy: nothing at all, not even a status
    CHECK(emulated.WantedBlock(copy) == 40);
    for (int i = 0; i < 4; ++i) bridge.Step(&real);
    const std::vector<int> asked = reads(real);
    CHECK(asked.size() >= 10 && asked[9] == 40);  // right after the read that was out already
    const Report reply = emulated.Read();
    CHECK(reply[0] == 0x51 && (reply[1] & 0x10) && reply[2] == 40 && reply[3] == 40);
    Run(bridge, &real);
    CHECK(!emulated.IsLoading(copy));
    CHECK(emulated.Figure(copy) == marked);
    CHECK(LoadFigureFile(chop_backup) == marked);  // the backup once the copy is whole
    CHECK(bridge.IsCopySlot(copy));
  }

  // The game's write to a block not read yet stays (the toy's older block doesn't replace it) and
  // goes onto the toy; nothing is saved to the backup file before the copy is whole.
  {
    SoftwarePortal inner, emulated;
    Habits real(&inner);
    RealPortalBridge bridge(&emulated, {dir, nullptr, nullptr});
    std::vector<bool> saved_with_source;
    emulated.SetWriteCallback([&](int slot, const FigureData& data,
                                  const std::optional<std::filesystem::path>& source) {
      saved_with_source.push_back(source.has_value());
      bridge.OnEmulatedWrite(slot, data);
    });
    inner.PlaceFigure(0, chop);
    const int copy = place_early(bridge, &real, emulated);
    REQUIRE_OR_RETURN(copy >= 0 && emulated.IsLoading(copy));
    GameWrite(emulated, copy, 50, 0x77);
    Run(bridge, &real);
    CHECK(saved_with_source.size() == 1 && !saved_with_source[0]);
    CHECK(emulated.Figure(copy) && BlockIs(*emulated.Figure(copy), 50, 0x77));
    CHECK(inner.Figure(0) && BlockIs(*inner.Figure(0), 50, 0x77));
    CHECK(!emulated.IsLoading(copy));
    const auto backup = LoadFigureFile(chop_backup);
    CHECK(backup && BlockIs(*backup, 50, 0x77));
    // Whole now: the game's writes save to the backup file again.
    GameWrite(emulated, copy, 54, 0x66);
    CHECK(saved_with_source.size() == 2 && saved_with_source[1]);
  }

  // Lifted off while its copy loads: the copy goes, and the game's waiting read is answered as one
  // that found no figure.
  {
    SoftwarePortal inner, emulated;
    Habits real(&inner);
    RealPortalBridge bridge(&emulated, {dir, nullptr, nullptr});
    inner.PlaceFigure(0, chop);
    const int copy = place_early(bridge, &real, emulated);
    REQUIRE_OR_RETURN(copy >= 0);
    emulated.Write(Cmd({'Q', uint8_t(0x20 | copy), 60}));
    CHECK(emulated.Read()[0] == 0);
    inner.RemoveFigure(0);
    Run(bridge, &real, 20);
    CHECK(!emulated.HasFigure(copy));
    const Report reply = emulated.Read();
    CHECK(reply[0] == 0x51 && !(reply[1] & 0x10) && reply[2] == 60);
    CHECK(bridge.RealUids().empty());
  }

  // A toy that can't be read any more while its copy loads: given up, and the copy goes with it.
  {
    SoftwarePortal inner, emulated;
    Habits real(&inner);
    std::vector<std::string> lines;
    RealPortalBridge::Options options{dir, [&](const std::string& l) { lines.push_back(l); }, nullptr};
    options.read_retry_pause = std::chrono::milliseconds(1);
    RealPortalBridge bridge(&emulated, options);
    inner.PlaceFigure(0, chop);
    const int copy = place_early(bridge, &real, emulated);
    REQUIRE_OR_RETURN(copy >= 0);
    real.refuse_reads = 1000;
    RunFor(bridge, &real, std::chrono::milliseconds(300));
    CHECK(!emulated.HasFigure(copy));
    CHECK(bridge.RealUids().empty());
    CHECK(std::any_of(lines.begin(), lines.end(), [](const std::string& l) {
      return l.find("could not read the figure in real slot 0") != std::string::npos;
    }));
  }

  std::filesystem::remove_all(dir);
  return Finish("real portal bridge");
}
