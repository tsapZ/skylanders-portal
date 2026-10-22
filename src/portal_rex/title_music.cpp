#include "portal_rex/title_music.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/audio/output_tap.h>
#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(capture_title_music, "", "Launcher",
                      "Record the title music into this WAV file and quit; for the Core of Light launcher");

namespace skylanders {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kRate = 48000;
constexpr double kFrom = 28.0, kTo = 58.0;  // seconds after the window opened
constexpr double kFadeIn = 1.0, kFadeOut = 2.5;

struct Capture {
  Clock::time_point start = Clock::now();
  std::mutex mutex;
  // Each audio client's stream (the game may play on several), interleaved stereo; summed at the
  // end, all from the window's start.
  std::map<const void*, std::vector<float>> streams;
  bool done = false;
};
Capture* g_capture = nullptr;

int16_t ToPcm(float v) { return int16_t(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); }

// The audio thread's blocks, folded to stereo, kept per stream while the window is open.
void Tap(const void* source, const float* samples, uint32_t frames, uint32_t channels, void* user) {
  auto* c = static_cast<Capture*>(user);
  const double t = std::chrono::duration<double>(Clock::now() - c->start).count();
  if (t < kFrom || t > kTo || channels < 2) return;
  std::lock_guard lock(c->mutex);
  if (c->done) return;
  std::vector<float>& out = c->streams[source];
  for (uint32_t f = 0; f < frames; ++f) {
    const float* s = samples + size_t(f) * channels;
    float l = s[0], r = s[1];
    if (channels >= 6) {  // FL FR FC LFE BL BR
      l += 0.707f * s[2] + 0.707f * s[4];
      r += 0.707f * s[2] + 0.707f * s[5];
    }
    out.push_back(l);
    out.push_back(r);
  }
}

void Put32(std::ofstream& out, uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); }
void Put16(std::ofstream& out, uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); }

bool WriteWav(const std::filesystem::path& path, std::vector<int16_t>& samples) {
  // As loud as the music allows (the games mix it at about a sixth of full scale; at most 6x), and
  // a fade at both ends, so the launcher's loop doesn't click.
  int peak = 1;
  for (const int16_t s : samples) peak = std::max(peak, std::abs(int(s)));
  const double gain = std::min(6.0, 24000.0 / double(peak));
  const size_t frames = samples.size() / 2;
  const size_t fade_in = size_t(kFadeIn * kRate), fade_out = size_t(kFadeOut * kRate);
  for (size_t f = 0; f < frames; ++f) {
    double fade = 1.0;
    if (f < fade_in) fade = double(f) / double(fade_in);
    if (frames - f < fade_out) fade = std::min(fade, double(frames - f) / double(fade_out));
    const double g = gain * fade;
    for (int ch = 0; ch < 2; ++ch) {
      const long v = std::lround(samples[f * 2 + ch] * g);
      samples[f * 2 + ch] = int16_t(std::clamp(v, -32767L, 32767L));
    }
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::filesystem::path temp = path;
  temp += ".new";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    const uint32_t data_size = uint32_t(samples.size() * 2);
    out.write("RIFF", 4);
    Put32(out, 36 + data_size);
    out.write("WAVEfmt ", 8);
    Put32(out, 16);
    Put16(out, 1);  // PCM
    Put16(out, 2);
    Put32(out, kRate);
    Put32(out, kRate * 4);
    Put16(out, 4);
    Put16(out, 16);
    out.write("data", 4);
    Put32(out, data_size);
    out.write(reinterpret_cast<const char*>(samples.data()), std::streamsize(data_size));
    if (!out) return false;
  }
  std::filesystem::rename(temp, path, ec);
  return !ec;
}

}  // namespace

void StartTitleMusicCaptureIfRequested() {
  const std::string target = REXCVAR_GET(capture_title_music);
  if (target.empty() || g_capture) return;
  g_capture = new Capture();  // lives until the process ends
  rex::audio::SetOutputTap(Tap, g_capture);
  REXLOG_INFO("Title music: recording {:.0f}-{:.0f} s into {}", kFrom, kTo, target);
  std::thread([target] {
    std::this_thread::sleep_until(g_capture->start + std::chrono::milliseconds(int(kTo * 1000) + 500));
    std::vector<int16_t> samples;
    {
      std::lock_guard lock(g_capture->mutex);
      g_capture->done = true;
      // The streams summed, as the PC mixes them.
      size_t length = 0;
      for (const auto& [source, stream] : g_capture->streams) length = std::max(length, stream.size());
      std::vector<float> mix(length, 0.0f);
      for (const auto& [source, stream] : g_capture->streams) {
        for (size_t i = 0; i < stream.size(); ++i) mix[i] += stream[i];
      }
      samples.reserve(length);
      for (const float v : mix) samples.push_back(ToPcm(v));
      REXLOG_INFO("Title music: {} audio stream(s)", g_capture->streams.size());
    }
    rex::audio::SetOutputTap(nullptr, nullptr);
    // Nearly silent: the game wasn't at a title with music (it stays without a file then).
    int peak = 0;
    for (const int16_t s : samples) peak = std::max(peak, std::abs(int(s)));
    const std::u8string u8(target.begin(), target.end());
    const bool ok = samples.size() >= size_t(kRate) * 2 * 10 && peak > 600 &&
                    WriteWav(std::filesystem::path(u8), samples);
    REXLOG_INFO("Title music: {} s, peak {}, {}", samples.size() / 2 / kRate, peak, ok ? "written" : "not written");
    rex::FlushLogging();
    std::_Exit(ok ? 0 : 2);  // a recording run only: no shutdown needed
  }).detach();
}

}  // namespace skylanders
