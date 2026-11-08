#include <atomic>
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "portal/software/software_portal.h"
#include "test_util.h"

using namespace skylanders::portal;

static Report Cmd(std::initializer_list<uint8_t> bytes) {
  Report r{};
  size_t i = 0;
  for (uint8_t b : bytes) r[i++] = b;
  return r;
}

int main() {
  // Ready ('R') is answered with 52 02 1B, then idle reads are status reports.
  {
    SoftwarePortal p;
    p.Write(Cmd({'R', 0x00}));
    Report reply = p.Read();
    CHECK(reply[0] == 0x52 && reply[1] == 0x02 && reply[2] == 0x1B);
    Report status = p.Read();
    CHECK(status[0] == 0x53);
    CHECK(status[6] == 0x00);  // the active flag is clear before 'A'
  }

  // Activate ('A') echoes its argument: 41 <arg> FF 77. The status report then shows active.
  {
    SoftwarePortal p;
    p.Write(Cmd({'A', 0x00}));
    p.Write(Cmd({'A', 0x01}));
    Report a0 = p.Read();
    Report a1 = p.Read();
    CHECK(a0[0] == 0x41 && a0[1] == 0x00 && a0[2] == 0xFF && a0[3] == 0x77);
    CHECK(a1[0] == 0x41 && a1[1] == 0x01 && a1[2] == 0xFF && a1[3] == 0x77);
    Report status = p.Read();
    CHECK(status[0] == 0x53);
    CHECK(status[6] == 0x01);
  }

  // Version ('M') echoes its argument: 4D <arg> 00 19.
  {
    SoftwarePortal p;
    p.Write(Cmd({'M', 0x01}));
    Report r = p.Read();
    CHECK(r[0] == 0x4D && r[1] == 0x01 && r[2] == 0x00 && r[3] == 0x19);
  }

  // Replies come back in the order the commands were sent.
  {
    SoftwarePortal p;
    p.Write(Cmd({'R'}));
    p.Write(Cmd({'A', 0x00}));
    p.Write(Cmd({'M', 0x01}));
    CHECK(p.Read()[0] == 0x52);
    CHECK(p.Read()[0] == 0x41);
    CHECK(p.Read()[0] == 0x4D);
    CHECK(p.Read()[0] == 0x53);  // then status
  }

  // Status ('S'), LED commands ('C', 'L') and unknown bytes queue no reply and do not crash.
  {
    SoftwarePortal p;
    p.Write(Cmd({'S'}));
    p.Write(Cmd({'V'}));
    p.Write(Cmd({'C', 0xE8, 0x10, 0x00}));
    p.Write(Cmd({'L', 0x02, 0x00, 0xFF, 0x00}));
    p.Write(Cmd({0x00}));
    p.Write(Cmd({0xFF, 0xFF, 0xFF}));
    p.Write(Cmd({'z'}));
    CHECK(p.Read()[0] == 0x53);
    CHECK(p.Read()[0] == 0x53);
  }

  // The status counter is byte 5 and increments on every status report, wrapping at 256.
  {
    SoftwarePortal p;
    uint8_t previous = p.Read()[5];
    for (int i = 0; i < 300; ++i) {
      uint8_t next = p.Read()[5];
      CHECK(next == static_cast<uint8_t>(previous + 1));
      previous = next;
    }
  }

  // Slot state bits in a status report: two bits per slot, slot 0 lowest, little-endian at bytes 1-4.
  auto slot_state = [](const Report& status, int slot) {
    uint32_t bits = status[1] | (status[2] << 8) | (status[3] << 16) | (uint32_t(status[4]) << 24);
    return (bits >> (2 * slot)) & 0x3u;
  };
  auto pattern = [] {
    FigureData d{};
    for (size_t i = 0; i < d.size(); ++i) d[i] = static_cast<uint8_t>(i * 7 + 1);
    return d;
  };

  // A figure placed before activation shows as empty until 'A', then "added" (3) for 8 status
  // reports, then "ready" (1).
  {
    SoftwarePortal p;
    CHECK(p.PlaceFigure(0, pattern()));
    CHECK(slot_state(p.Read(), 0) == 0);
    p.Write(Cmd({'A', 0x01}));
    p.Read();  // the 'A' reply
    for (int i = 0; i < 8; ++i) CHECK(slot_state(p.Read(), 0) == 3);
    CHECK(slot_state(p.Read(), 0) == 1);
    CHECK(slot_state(p.Read(), 0) == 1);
  }

  // A figure placed after activation goes straight to "added". Other slots stay empty.
  {
    SoftwarePortal p;
    p.Write(Cmd({'A', 0x01}));
    p.Read();
    CHECK(p.PlaceFigure(3, pattern()));
    Report s = p.Read();
    CHECK(slot_state(s, 3) == 3);
    CHECK(slot_state(s, 0) == 0);
    CHECK(slot_state(s, 15) == 0);
  }

  // Q returns the block's 16 bytes with the "present" flag; block 0 and the last block work.
  {
    SoftwarePortal p;
    FigureData d = pattern();
    p.PlaceFigure(0, d);
    for (uint8_t block : {uint8_t(0), uint8_t(1), uint8_t(0x3F)}) {
      p.Write(Cmd({'Q', 0x00, block}));
      Report r = p.Read();
      CHECK(r[0] == 0x51 && r[1] == 0x10 && r[2] == block);
      for (size_t i = 0; i < kBlockSize; ++i) CHECK(r[3 + i] == d[block * kBlockSize + i]);
    }
  }

  // The high nibble of Q's second byte is ignored; the low nibble selects the slot.
  {
    SoftwarePortal p;
    p.PlaceFigure(2, pattern());
    p.Write(Cmd({'Q', 0xF2, 0x00}));
    Report r = p.Read();
    CHECK(r[1] == 0x12);
    p.Write(Cmd({'Q', 0xF0, 0x00}));  // slot 0 is empty
    Report empty = p.Read();
    CHECK(empty[1] == 0x00 && empty[2] == 0x00 && empty[3] == 0x00);
  }

  // Q for an empty slot has no "present" flag and zero data. Block 64 and up never reads out of bounds.
  {
    SoftwarePortal p;
    p.PlaceFigure(0, pattern());
    p.Write(Cmd({'Q', 0x05, 0x02}));  // slot 5 empty
    Report r = p.Read();
    CHECK(r[1] == 0x05 && r[2] == 0x02);
    for (size_t i = 3; i < 3 + kBlockSize; ++i) CHECK(r[i] == 0);
    for (int block : {64, 65, 127, 200, 255}) {
      p.Write(Cmd({'Q', 0x00, static_cast<uint8_t>(block)}));
      Report o = p.Read();
      CHECK(o[0] == 0x51 && o[1] == 0x00 && o[2] == block);
      for (size_t i = 3; i < 3 + kBlockSize; ++i) CHECK(o[i] == 0);
    }
  }

  // W writes 16 bytes, replies 57 <flag|slot> <block>, and Q reads them back. Out-of-range blocks
  // and empty slots are ignored without touching any figure.
  {
    SoftwarePortal p;
    FigureData d = pattern();
    p.PlaceFigure(1, d);
    Report w = Cmd({'W', 0x01, 0x05});
    for (size_t i = 0; i < kBlockSize; ++i) w[3 + i] = static_cast<uint8_t>(0xA0 + i);
    p.Write(w);
    Report reply = p.Read();
    CHECK(reply[0] == 0x57 && reply[1] == 0x11 && reply[2] == 0x05);
    p.Write(Cmd({'Q', 0x01, 0x05}));
    Report q = p.Read();
    for (size_t i = 0; i < kBlockSize; ++i) CHECK(q[3 + i] == static_cast<uint8_t>(0xA0 + i));
    auto after = p.Figure(1);
    CHECK(after.has_value());
    CHECK((*after)[4 * kBlockSize] == d[4 * kBlockSize]);  // neighbouring block untouched

    Report bad = w;
    bad[2] = 64;  // out of range
    p.Write(bad);
    CHECK(p.Read()[1] == 0x01);  // no "present" flag
    Report empty_slot = w;
    empty_slot[1] = 0x07;  // slot 7 has no figure
    p.Write(empty_slot);
    CHECK(p.Read()[1] == 0x07);
    CHECK(*p.Figure(1) == *after);
  }

  // Out-of-range slots are rejected and change nothing.
  {
    SoftwarePortal p;
    CHECK(!p.PlaceFigure(-1, pattern()));
    CHECK(!p.PlaceFigure(16, pattern()));
    CHECK(!p.PlaceFigure(1000, pattern()));
    CHECK(!p.RemoveFigure(-1));
    CHECK(!p.RemoveFigure(16));
    CHECK(!p.HasFigure(-1) && !p.HasFigure(16));
    CHECK(!p.Figure(16).has_value());
    for (int i = 0; i < kMaxFigures; ++i) CHECK(!p.HasFigure(i));
  }

  // RemoveFigure: false for an empty slot; when active the slot shows "removing" (2) once, then empty.
  {
    SoftwarePortal p;
    CHECK(!p.RemoveFigure(0));
    p.PlaceFigure(0, pattern());
    p.Write(Cmd({'A', 0x01}));
    p.Read();
    for (int i = 0; i < 10; ++i) p.Read();  // settle to ready
    CHECK(p.HasFigure(0));
    CHECK(p.RemoveFigure(0));
    CHECK(!p.HasFigure(0));
    CHECK(!p.RemoveFigure(0));
    CHECK(slot_state(p.Read(), 0) == 2);
    CHECK(slot_state(p.Read(), 0) == 0);
  }

  // Slot 3's state uses bits 6-7 of the status word.
  {
    SoftwarePortal p;
    p.Write(Cmd({'A', 0x01}));
    p.Read();
    p.PlaceFigure(3, pattern());
    Report s = p.Read();
    CHECK((s[1] & 0xC0) == 0xC0);
  }

  // A repeated 'A 01' while the portal is already active must NOT re-announce figures. The game
  // sends it about every 10 seconds; re-announcing made the figure look taken off and put back.
  {
    SoftwarePortal p;
    p.PlaceFigure(0, pattern());
    p.Write(Cmd({'A', 0x00}));
    p.Write(Cmd({'A', 0x01}));
    p.Read();
    p.Read();  // the two 'A' replies
    for (int i = 0; i < 12; ++i) p.Read();  // added, then settled to ready
    CHECK(slot_state(p.Read(), 0) == 1);
    for (int repeat = 0; repeat < 3; ++repeat) {
      p.Write(Cmd({'A', 0x01}));
      CHECK(p.Read()[0] == 0x41);  // the reply
      for (int i = 0; i < 12; ++i) CHECK(slot_state(p.Read(), 0) == 1);  // still ready, never "added"
    }
    // The figure is unchanged and still readable.
    p.Write(Cmd({'Q', 0x00, 0x01}));
    CHECK(p.Read()[1] == 0x10);
  }

  // 'A 00' deactivates: no slot states and the active flag clear. A later 'A 01' announces figures
  // again, once.
  {
    SoftwarePortal p;
    p.PlaceFigure(0, pattern());
    p.Write(Cmd({'A', 0x01}));
    p.Read();
    for (int i = 0; i < 12; ++i) p.Read();  // ready
    p.Write(Cmd({'A', 0x00}));
    CHECK(p.Read()[0] == 0x41);
    Report off = p.Read();
    CHECK(off[0] == 0x53);
    CHECK(off[6] == 0x00);  // inactive
    CHECK(slot_state(off, 0) == 0);
    CHECK(p.HasFigure(0));  // the figure itself is still there
    p.Write(Cmd({'A', 0x01}));
    p.Read();
    for (int i = 0; i < 8; ++i) CHECK(slot_state(p.Read(), 0) == 3);
    CHECK(slot_state(p.Read(), 0) == 1);
  }

  // SetWriteCallback fires with (slot, full figure data, source path) exactly when a 'W' actually
  // writes, and not for out-of-range blocks or empty slots. Removing the callback stops further
  // notifications. A figure placed with no source (matching --portal_test_figure) gives nullopt.
  {
    SoftwarePortal p;
    p.PlaceFigure(2, pattern());
    struct Call {
      int slot;
      FigureData data;
      std::optional<std::filesystem::path> source;
    };
    std::vector<Call> calls;
    p.SetWriteCallback([&](int slot, const FigureData& data,
                          const std::optional<std::filesystem::path>& source) {
      calls.push_back({slot, data, source});
    });

    Report w = Cmd({'W', 0x02, 0x03});
    for (size_t i = 0; i < kBlockSize; ++i) w[3 + i] = static_cast<uint8_t>(0x50 + i);
    p.Write(w);
    p.Read();  // the reply
    CHECK(calls.size() == 1);
    CHECK(calls[0].slot == 2);
    CHECK(calls[0].data[3 * kBlockSize] == 0x50);
    CHECK(!calls[0].source.has_value());

    Report bad = w;
    bad[2] = 64;  // out of range: no callback
    p.Write(bad);
    p.Read();
    CHECK(calls.size() == 1);

    Report empty_slot = w;
    empty_slot[1] = 0x09;  // slot 9 has no figure: no callback
    p.Write(empty_slot);
    p.Read();
    CHECK(calls.size() == 1);

    p.SetWriteCallback(nullptr);
    p.Write(w);
    p.Read();
    CHECK(calls.size() == 1);
  }

  // Swapping a slot's figure: the source path a write saves to always matches the figure actually
  // live in the slot, because PlaceFigure sets the data and its source together under one lock.
  // (A fresh review found that portal_hook.cpp previously tracked the source in a *second*,
  // separately-locked map, updated after PlaceFigure returned — a window where a concurrent write
  // could be attributed to the figure that used to be in the slot, corrupting the wrong file.)
  {
    SoftwarePortal p;
    std::vector<std::optional<std::filesystem::path>> sources_seen;
    p.SetWriteCallback([&](int, const FigureData&, const std::optional<std::filesystem::path>& source) {
      sources_seen.push_back(source);
    });
    const std::filesystem::path path_a = "figure_a.dump";
    const std::filesystem::path path_b = "figure_b.dump";
    Report w = Cmd({'W', 0x00, 0x01});

    p.PlaceFigure(0, pattern(), path_a);
    p.Write(w);
    p.Read();
    CHECK(sources_seen.size() == 1);
    CHECK(sources_seen[0] == path_a);

    p.PlaceFigure(0, pattern(), path_b);  // swap, no explicit Remove first
    p.Write(w);
    p.Read();
    CHECK(sources_seen.size() == 2);
    CHECK(sources_seen[1] == path_b);  // never path_a

    p.PlaceFigure(0, pattern());  // swap to a figure with no source
    p.Write(w);
    p.Read();
    CHECK(sources_seen.size() == 3);
    CHECK(!sources_seen[2].has_value());

    p.PlaceFigure(0, pattern(), path_a);
    p.RemoveFigure(0);
    p.PlaceFigure(0, pattern(), path_b);
    p.Write(w);
    p.Read();
    CHECK(sources_seen.size() == 4);
    CHECK(sources_seen[3] == path_b);
  }

  // A swap while the game watches the portal (Core of Light #60): the old figure is reported as
  // removing for one status report, a late write still goes to the old figure's own file, then the
  // new figure comes in as added and takes the writes.
  {
    SoftwarePortal p;
    std::vector<std::optional<std::filesystem::path>> sources_seen;
    p.SetWriteCallback([&](int, const FigureData&, const std::optional<std::filesystem::path>& source) {
      sources_seen.push_back(source);
    });
    const std::filesystem::path path_a = "figure_a.dump";
    const std::filesystem::path path_b = "figure_b.dump";
    p.Write(Cmd({'A', 0x01}));
    p.Read();  // the 'A' reply
    p.PlaceFigure(0, pattern(), path_a);
    for (int i = 0; i < 20; ++i) p.Read();  // past the "added" reports
    p.PlaceFigure(0, pattern(), path_b);  // swap
    CHECK(p.Source(0) == path_a);         // still the old figure until it was reported gone
    p.Write(Cmd({'W', 0x00, 0x01}));      // a late write meant for the old figure
    p.Read();                             // its reply
    CHECK(sources_seen.size() == 1 && sources_seen[0] == path_a);
    Report gone = p.Read();
    CHECK(gone[0] == 0x53 && (gone[1] & 0x03) == 0x02);  // slot 0 removing
    Report added = p.Read();
    CHECK(added[0] == 0x53 && (added[1] & 0x03) == 0x03);  // slot 0 added: the new figure
    CHECK(p.Source(0) == path_b);
    p.Write(Cmd({'W', 0x00, 0x01}));
    p.Read();
    CHECK(sources_seen.size() == 2 && sources_seen[1] == path_b);
    // The same file can't go into a second slot, not even while a swap to it is pending.
    p.PlaceFigure(1, pattern(), path_a);
    for (int i = 0; i < 20; ++i) p.Read();
    p.PlaceFigure(1, pattern(), path_b);  // path_b is in slot 0
    CHECK(p.Source(1) == path_a);
    CHECK(!p.PlaceFigure(2, pattern(), path_a));
  }

  // The overlay thread and the game thread use the portal at the same time.
  {
    SoftwarePortal p;
    std::atomic<bool> stop{false};
    std::thread overlay([&] {
      FigureData d = pattern();
      while (!stop) {
        p.PlaceFigure(0, d);
        p.PlaceFigure(5, d);
        p.RemoveFigure(0);
        p.RemoveFigure(5);
        (void)p.HasFigure(0);
        (void)p.Figure(5);
      }
    });
    std::thread game([&] {
      p.Write(Cmd({'R'}));
      p.Write(Cmd({'A', 0x01}));
      while (!stop) {
        p.Write(Cmd({'Q', 0x00, 0x00}));
        p.Write(Cmd({'W', 0x05, 0x01}));
        (void)p.Read();
        (void)p.Read();
      }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop = true;
    overlay.join();
    game.join();
    CHECK(p.Read()[0] != 0x00);  // still answers coherently
  }

  // Source(slot): the path a slot was placed with, or nullopt (no source, or empty/out of range).
  {
    SoftwarePortal p;
    CHECK(!p.Source(0).has_value());   // empty slot
    CHECK(!p.Source(-1).has_value());  // out of range
    CHECK(!p.Source(16).has_value());  // out of range
    const std::filesystem::path path_a = "figure_a.dump";
    p.PlaceFigure(3, pattern(), path_a);
    CHECK(p.Source(3) == path_a);
    p.PlaceFigure(3, pattern());  // swap to no source
    CHECK(!p.Source(3).has_value());
    p.PlaceFigure(3, pattern(), path_a);
    p.RemoveFigure(3);
    CHECK(!p.Source(3).has_value());  // removed: forgotten, not just the figure
  }

  // 'J' (Trap Team portal sync) is answered with a 4A report; 'L' (side lights) gets none.
  {
    SoftwarePortal portal;
    portal.Write(Cmd({'J'}));
    CHECK(portal.Read()[0] == 0x4A);
    portal.Write(Cmd({'L'}));
    CHECK(portal.Read()[0] == 0x53);  // no queued reply: status frame
  }

  // 'M' without an audio sink: version 00 19 (no audio). With a sink: audio-capable version.
  {
    struct Sink : PortalAudioSink {
      std::vector<int16_t> got;
      void Submit(std::span<const int16_t> s) override { got.insert(got.end(), s.begin(), s.end()); }
    } sink;
    SoftwarePortal portal;
    portal.Write(Cmd({'M', 0x01}));
    Report r = portal.Read();
    CHECK(r[0] == 0x4D && r[1] == 0x01 && r[2] == 0x00 && r[3] == 0x19);
    portal.SetAudioSink(&sink);
    portal.Write(Cmd({'M', 0x01}));
    r = portal.Read();
    CHECK(r[0] == 0x4D && r[1] == 0x01 && r[2] == kAudioCapableVersion[0] &&
          r[3] == kAudioCapableVersion[1]);
    // Speaker audio reaches the sink unchanged.
    std::array<int16_t, 60> pcm{};
    pcm[0] = 1234;
    pcm[59] = -4321;
    portal.WriteAudio(pcm);
    CHECK(sink.got.size() == 60 && sink.got[0] == 1234 && sink.got[59] == -4321);
    // No sink: audio is dropped without error.
    portal.SetAudioSink(nullptr);
    portal.WriteAudio(pcm);
    CHECK(sink.got.size() == 60);
  }

  return Finish("software_portal");
}
