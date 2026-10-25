#include "portal_rex/sdl_audio_sink.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>

#include <SDL3/SDL.h>
#include <rex/logging.h>

namespace skylanders {

std::unique_ptr<SdlPortalAudioSink> SdlPortalAudioSink::Open() {
  if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    REXLOG_WARN("Portal: cannot start audio for the portal speaker: {}", SDL_GetError());
    return nullptr;
  }
  SDL_AudioSpec spec{SDL_AUDIO_S16, 1, portal::kAudioSampleRate};
  SDL_AudioStream* stream =
      SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (!stream) {
    REXLOG_WARN("Portal: cannot open an audio device for the portal speaker: {}", SDL_GetError());
    return nullptr;
  }
  SDL_ResumeAudioStreamDevice(stream);
  return std::unique_ptr<SdlPortalAudioSink>(new SdlPortalAudioSink(stream));
}

SdlPortalAudioSink::~SdlPortalAudioSink() { SDL_DestroyAudioStream(stream_); }

float SdlPortalAudioSink::Level() const {
  const double now =
      std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  return now - last_submit_.load() > 0.25 ? 0.0f : level_.load();
}

void SdlPortalAudioSink::Submit(std::span<const int16_t> samples) {
  // The level, and a log line when a sound starts and ends (the game sends silence in between).
  int peak = 0;
  for (const int16_t s : samples) peak = std::max(peak, std::abs(int(s)));
  const double now =
      std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  last_submit_.store(now);
  const float loud = std::min(1.0f, float(peak) / 12000.0f);
  const float falling = level_.load() * 0.92f;
  level_.store(std::max(loud, falling));
  if (peak > 1500) {
    if (!sounding_) {
      sounding_ = true;
      sound_start_ = now;
      REXLOG_INFO("Portal speaker: sound");
    }
    last_loud_ = now;
  } else if (sounding_ && now - last_loud_ > 1.0) {
    sounding_ = false;
    REXLOG_INFO("Portal speaker: quiet again after {:.1f} s", last_loud_ - sound_start_);
  }
  const float v = std::clamp(volume_.load(), 0.0f, 1.0f);
  std::array<int16_t, 64> scaled{};
  while (!samples.empty()) {
    const size_t n = std::min(samples.size(), scaled.size());
    for (size_t i = 0; i < n; i++) scaled[i] = static_cast<int16_t>(samples[i] * v);
    SDL_PutAudioStreamData(stream_, scaled.data(), int(n * sizeof(int16_t)));  // non-blocking
    samples = samples.subspan(n);
  }
}

}  // namespace skylanders
