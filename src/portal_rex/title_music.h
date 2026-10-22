#pragma once

// A game's title music for the Core of Light launcher, which plays it while the game's card is
// chosen (the owner's wish, 2026-10-22). It comes straight from the disc, not from a recording of
// the running game (the owner, 2026-10-22: a minute of recording was too much): the tracks the
// title screen streams, found once per game with a run that logged the game's file reads
// (--log_level=trace --log_noisy=true), decoded from XMA2 with the SDK's FFmpeg and written as a
// 16-bit stereo WAV, as loud as it allows. Decoding takes about a second.

#include <filesystem>
#include <string>
#include <vector>

namespace skylanders {

// One track on the disc: in an FMOD sound bank file (FSB5 banks, Giants and later) by its sample
// name, or in a Wwise file package (.pck, Spyro's Adventure) by its streamed file's id in decimal.
struct TitleMusicTrack {
  std::string file;   // relative to the disc folder, e.g. "archives/INTER_Streamed_Launch_bank.pak"
  std::string name;   // e.g. "INTER_Results_Screen"
  float gain = 1.0f;  // its level in the mix when the title plays several at once
};
using TitleMusicSource = std::vector<TitleMusicTrack>;

// Decodes the tracks (mixed, the shorter ones looped to the longest) into `wav`.
bool WriteTitleMusic(const std::filesystem::path& disc, const TitleMusicSource& source,
                     const std::filesystem::path& wav);

// From the game's OnPreSetup: with --export_title_music=<file.wav> (the launcher), writes that
// file and quits before a window opens. Without the flag nothing happens.
void ExportTitleMusicIfRequested(const std::filesystem::path& disc, const TitleMusicSource& source);

// From OnPostSetup: writes title_music.wav into the game's picture folder for the launcher
// (SharedPicturesDir) once, in the background, if it isn't there yet.
void StartTitleMusicExport(const std::filesystem::path& disc, const TitleMusicSource& source,
                           const std::filesystem::path& pictures_dir);

}  // namespace skylanders
