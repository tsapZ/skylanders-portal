#include "portal_rex/title_music.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <string_view>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>

extern "C" {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
#include "libavcodec/avcodec.h"
#include "libavutil/mem.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

REXCVAR_DEFINE_STRING(export_title_music, "", "Launcher",
                      "Write the title music from the disc into this WAV file and quit; for the Core of Light launcher");

namespace skylanders {
namespace {

namespace fs = std::filesystem;

// An XMA2 stream as FFmpeg's xma2 decoder takes it: 2048-byte packets and the XMA2WAVEFORMATEX's
// 34 bytes after its WAVEFORMATEX (little-endian).
struct XmaTrack {
  int channels = 0;
  int rate = 0;
  uint64_t samples = 0;  // to keep of the decoded ones (the track's length or its loop's end)
  std::vector<uint8_t> extradata;
  std::vector<uint8_t> data;
};

uint32_t Le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
uint64_t Le64(const uint8_t* p) { return Le32(p) | (uint64_t(Le32(p + 4)) << 32); }
uint32_t Be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
uint16_t Be16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }

void Put16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(uint8_t(v));
  out.push_back(uint8_t(v >> 8));
}
void Put32(std::vector<uint8_t>& out, uint32_t v) {
  Put16(out, uint16_t(v));
  Put16(out, uint16_t(v >> 16));
}

bool ReadAt(std::ifstream& in, uint64_t offset, size_t size, std::vector<uint8_t>& out) {
  out.resize(size);
  in.clear();
  in.seekg(std::streamoff(offset));
  in.read(reinterpret_cast<char*>(out.data()), std::streamsize(size));
  return size_t(in.gcount()) == size;
}

// The XMA2WAVEFORMATEX tail FFmpeg needs (it reads only the size and the channels; the rest as
// vgmstream fills it for FSB5).
std::vector<uint8_t> Xma2Extradata(int channels, uint64_t samples, size_t bytes) {
  constexpr uint32_t kBlock = 0x8000;
  static constexpr std::array<uint32_t, 9> kMask = {0, 0x4, 0x3, 0x7, 0x33, 0x37, 0x3F, 0x13F, 0x63F};
  std::vector<uint8_t> e;
  Put16(e, uint16_t((channels + 1) / 2));
  Put32(e, channels < int(kMask.size()) ? kMask[size_t(channels)] : 0);
  Put32(e, uint32_t(samples));
  Put32(e, kBlock);
  for (int i = 0; i < 4; ++i) Put32(e, 0);  // play and loop ranges
  e.push_back(0);                           // loop count
  e.push_back(4);                           // encoder version
  Put16(e, uint16_t((bytes + kBlock - 1) / kBlock));
  return e;
}

// FMOD: the FSB5 bank in `file` whose first sample is called `name` (each streamed track is a bank
// of its own in these games' archives). Header fields as vgmstream's fsb5.c reads them.
std::optional<XmaTrack> FindFsb5Sample(const fs::path& file, std::string_view name) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return std::nullopt;
  constexpr size_t kChunk = size_t(4) << 20;
  std::vector<uint8_t> buffer(kChunk + 4), head;
  static constexpr std::array<int, 11> kRates = {4000, 8000, 11000, 11025, 16000, 22050,
                                                 24000, 32000, 44100, 48000, 96000};
  for (uint64_t base = 0;; base += kChunk) {
    in.clear();
    in.seekg(std::streamoff(base));
    in.read(reinterpret_cast<char*>(buffer.data()), std::streamsize(buffer.size()));
    const size_t got = size_t(in.gcount());
    if (got < 4) return std::nullopt;
    for (size_t i = 0; i + 4 <= got && i < kChunk; ++i) {
      if (std::memcmp(&buffer[i], "FSB5", 4) != 0) continue;
      const uint64_t at = base + i;
      if (!ReadAt(in, at, 0x40, head)) continue;
      const uint32_t version = Le32(&head[4]), count = Le32(&head[8]), sample_headers = Le32(&head[12]),
                     names = Le32(&head[16]), data_size = Le32(&head[20]), mode = Le32(&head[24]);
      if (version > 1 || count == 0 || count > 100000 || names == 0 || mode != 10) continue;  // 10: XMA
      const uint32_t header = version == 1 ? 0x3C : 0x40;
      if (!ReadAt(in, at, size_t(header) + sample_headers + names, head)) continue;
      // The first sample's name.
      const uint8_t* name_table = &head[header + sample_headers];
      const uint32_t name_at = Le32(name_table);
      if (name_at >= names) continue;
      const char* first = reinterpret_cast<const char*>(name_table + name_at);
      if (std::string_view(first, strnlen(first, names - name_at)) != name) continue;
      // Its header: rate, channels, data offset, length, then optional chunks.
      const uint8_t* p = &head[header];
      const uint64_t bits = Le64(p);
      XmaTrack track;
      track.rate = kRates[std::min<size_t>((bits >> 1) & 0xF, kRates.size() - 1)];
      static constexpr std::array<int, 4> kChannels = {1, 2, 6, 8};
      track.channels = kChannels[(bits >> 5) & 3];
      const uint64_t data_offset = ((bits >> 7) & 0x07FFFFFF) << 5;
      track.samples = (bits >> 34) & 0x3FFFFFFF;
      bool more = bits & 1;
      p += 8;
      while (more && p + 4 <= name_table) {
        const uint32_t chunk = Le32(p);
        more = chunk & 1;
        const uint32_t size = (chunk >> 1) & 0xFFFFFF, type = chunk >> 25;
        p += 4;
        if (type == 1 && size >= 1) track.channels = p[0];
        if (type == 2 && size >= 4) track.rate = int(Le32(p));
        if (type == 3 && size >= 8 && Le32(p + 4) > 0) {  // loop: keep up to its end
          track.samples = std::min<uint64_t>(track.samples, uint64_t(Le32(p + 4)) + 1);
        }
        p += size;
      }
      // Up to the next sample's data (its header follows this one's chunks), or the bank's end.
      uint64_t data_end = data_size;
      if (count > 1 && p + 8 <= name_table) data_end = ((Le64(p) >> 7) & 0x07FFFFFF) << 5;
      if (data_end <= data_offset) return std::nullopt;
      const uint64_t data_at = at + header + sample_headers + names + data_offset;
      if (!ReadAt(in, data_at, size_t(data_end - data_offset), track.data)) return std::nullopt;
      track.extradata = Xma2Extradata(track.channels, track.samples, track.data.size());
      return track;
    }
  }
}

// Wwise: the streamed file `id` in the file package (AKPK, big-endian on the 360), a RIFX wem with
// an XMA2WAVEFORMATEX.
std::optional<XmaTrack> FindWwiseStream(const fs::path& file, uint32_t id) {
  std::ifstream in(file, std::ios::binary);
  std::vector<uint8_t> head, table;
  if (!in || !ReadAt(in, 0, 28, head) || std::memcmp(head.data(), "AKPK", 4) != 0) return std::nullopt;
  const uint32_t header_size = Be32(&head[4]);
  if (Be32(&head[8]) != 1 || header_size > (64u << 20) || !ReadAt(in, 8, header_size, table)) {
    return std::nullopt;
  }
  const uint32_t languages = Be32(&table[4]), banks = Be32(&table[8]), streams = Be32(&table[12]);
  uint64_t start = 0, size = 0;
  // The bank table, then the stream table: a count, then entries of 20 bytes (32-bit size) or 24
  // (64-bit size): id, block size, size, first block, language.
  size_t at = 16 + size_t(languages);
  for (const uint32_t part : {banks, streams}) {
    if (at + part > table.size() || part < 4) break;
    const uint32_t count = Be32(&table[at]);
    const size_t entry = count ? (part - 4) / count : 20;
    for (uint32_t i = 0; i < count && !size && (entry == 20 || entry == 24); ++i) {
      const uint8_t* e = &table[at + 4 + i * entry];
      if (Be32(e) != id) continue;
      size = entry == 20 ? Be32(e + 8) : (uint64_t(Be32(e + 8)) << 32) | Be32(e + 12);
      start = uint64_t(Be32(e + 4)) * Be32(e + entry - 8);
    }
    at += part;
  }
  std::vector<uint8_t> wem;
  if (!size || size > (512u << 20) || !ReadAt(in, start, size_t(size), wem)) return std::nullopt;
  if (wem.size() < 12 || std::memcmp(wem.data(), "RIFX", 4) != 0) return std::nullopt;
  XmaTrack track;
  for (size_t p = 12; p + 8 <= wem.size();) {
    const uint32_t chunk_size = Be32(&wem[p + 4]);
    const uint8_t* body = &wem[p + 8];
    if (p + 8 + chunk_size > wem.size()) break;
    if (std::memcmp(&wem[p], "fmt ", 4) == 0 && chunk_size >= 52 && Be16(body) == 0x166) {
      track.channels = Be16(body + 2);
      track.rate = int(Be32(body + 4));
      // The XMA2 fields from 18 on, swapped to little-endian.
      const uint8_t* x = body + 18;
      std::vector<uint8_t>& e = track.extradata;
      Put16(e, Be16(x));
      for (int i = 0; i < 7; ++i) Put32(e, Be32(x + 2 + 4 * i));
      e.push_back(x[30]);
      e.push_back(x[31]);
      Put16(e, Be16(x + 32));
      const uint32_t play_begin = Be32(x + 14), play_length = Be32(x + 18);
      track.samples = play_length ? uint64_t(play_begin) + play_length : Be32(x + 6);
    } else if (std::memcmp(&wem[p], "data", 4) == 0) {
      track.data.assign(body, body + chunk_size);
    }
    p += 8 + chunk_size + (chunk_size & 1);
  }
  if (!track.channels || track.extradata.size() != 34 || track.data.empty()) return std::nullopt;
  return track;
}

// The track as stereo floats at its own rate: 5.1 folded down, other layouts (the interactive
// music's stems) summed pair by pair.
bool DecodeXma(const XmaTrack& track, std::vector<float>& stereo) {
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_XMA2);
  if (!codec) return false;
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  bool ok = ctx && packet && frame;
  if (ok) {
    ctx->channels = track.channels;
    ctx->sample_rate = track.rate;
    ctx->block_align = 2048;
    ctx->extradata = static_cast<uint8_t*>(av_mallocz(track.extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    ok = ctx->extradata != nullptr;
    if (ok) {
      std::memcpy(ctx->extradata, track.extradata.data(), track.extradata.size());
      ctx->extradata_size = int(track.extradata.size());
      ok = avcodec_open2(ctx, codec, nullptr) >= 0;
    }
  }
  auto drain = [&] {
    while (avcodec_receive_frame(ctx, frame) == 0) {
      if (frame->format != AV_SAMPLE_FMT_FLTP) continue;
      const int ch = frame->channels;
      for (int i = 0; i < frame->nb_samples; ++i) {
        auto s = [&](int c) { return reinterpret_cast<const float*>(frame->extended_data[c])[i]; };
        float l = 0.0f, r = 0.0f;
        if (ch == 1) {
          l = r = s(0);
        } else if (ch == 6) {  // FL FR FC LFE BL BR
          l = s(0) + 0.707f * (s(2) + s(4));
          r = s(1) + 0.707f * (s(2) + s(5));
        } else {
          for (int c = 0; c + 1 < ch; c += 2) {
            l += s(c);
            r += s(c + 1);
          }
        }
        stereo.push_back(l);
        stereo.push_back(r);
      }
    }
  };
  std::vector<uint8_t> piece(2048 + AV_INPUT_BUFFER_PADDING_SIZE, 0);
  int errors = 0;
  for (size_t at = 0; ok && at + 2048 <= track.data.size(); at += 2048) {
    std::memcpy(piece.data(), &track.data[at], 2048);
    packet->data = piece.data();
    packet->size = 2048;
    for (int tries = 0; tries < 64; ++tries) {
      const int r = avcodec_send_packet(ctx, packet);
      if (r == AVERROR(EAGAIN)) {
        drain();
        continue;
      }
      if (r < 0) ++errors;
      break;
    }
    drain();
  }
  if (ok) {
    avcodec_send_packet(ctx, nullptr);
    drain();
  }
  if (frame) av_frame_free(&frame);
  if (packet) av_packet_free(&packet);
  if (ctx) avcodec_free_context(&ctx);
  // The decoder's delay: its output starts 576 samples late (measured against FFmpeg 8.1's own
  // reading of the same track, which skips them), so the loop's start and end line up again.
  constexpr size_t kDelay = 576;
  stereo.erase(stereo.begin(), stereo.begin() + ptrdiff_t(std::min(stereo.size(), kDelay * 2)));
  if (track.samples && stereo.size() > track.samples * 2) stereo.resize(size_t(track.samples * 2));
  if (errors) REXLOG_INFO("Title music: {} XMA packets the decoder refused", errors);
  return ok && !stereo.empty();
}

std::optional<XmaTrack> FindTrack(const fs::path& disc, const TitleMusicTrack& t) {
  const fs::path file = disc / fs::path(std::u8string(t.file.begin(), t.file.end()));
  if (file.extension() == ".pck") {
    char* end = nullptr;
    const unsigned long id = std::strtoul(t.name.c_str(), &end, 10);
    return end && *end == '\0' ? FindWwiseStream(file, uint32_t(id)) : std::nullopt;
  }
  return FindFsb5Sample(file, t.name);
}

bool WriteWav(const fs::path& path, int rate, const std::vector<float>& stereo) {
  // As loud as it allows (peak at -1 dB, at most 4x) and 10 ms ramps at both ends, so the
  // launcher's loop doesn't click.
  float peak = 1e-6f;
  for (const float v : stereo) peak = std::max(peak, std::abs(v));
  const float gain = std::min(4.0f, 0.89f / peak);
  const size_t frames = stereo.size() / 2, ramp = size_t(rate / 100);
  std::vector<uint8_t> out;
  out.reserve(44 + stereo.size() * 2);
  const uint32_t data_size = uint32_t(frames * 4);
  out.insert(out.end(), {'R', 'I', 'F', 'F'});
  Put32(out, 36 + data_size);
  out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  Put32(out, 16);
  Put16(out, 1);  // PCM
  Put16(out, 2);
  Put32(out, uint32_t(rate));
  Put32(out, uint32_t(rate) * 4);
  Put16(out, 4);
  Put16(out, 16);
  out.insert(out.end(), {'d', 'a', 't', 'a'});
  Put32(out, data_size);
  for (size_t f = 0; f < frames; ++f) {
    float g = gain;
    if (f < ramp) g *= float(f) / float(ramp);
    if (frames - f < ramp) g *= float(frames - f) / float(ramp);
    for (int c = 0; c < 2; ++c) {
      Put16(out, uint16_t(int16_t(std::lround(std::clamp(stereo[f * 2 + c] * g, -1.0f, 1.0f) * 32767.0f))));
    }
  }
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  fs::path temp = path;
  temp += ".new";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    if (!file) return false;
  }
  fs::rename(temp, path, ec);
  return !ec;
}

}  // namespace

bool WriteTitleMusic(const fs::path& disc, const TitleMusicSource& source, const fs::path& wav) {
  std::vector<float> mix;
  int rate = 0;
  for (const TitleMusicTrack& t : source) {
    const auto track = FindTrack(disc, t);
    std::vector<float> stereo;
    if (!track || !DecodeXma(*track, stereo)) {
      REXLOG_WARN("Title music: {} in {} not found or not decoded", t.name, t.file);
      return false;
    }
    // Every track at the first one's rate (linear; the titles' tracks share theirs anyway).
    if (!rate) rate = track->rate;
    if (track->rate != rate) {
      const double step = double(track->rate) / double(rate);
      const size_t frames = size_t(double(stereo.size() / 2) / step);
      std::vector<float> resampled(frames * 2);
      for (size_t f = 0; f < frames; ++f) {
        const double x = double(f) * step;
        const size_t i = std::min(size_t(x), stereo.size() / 2 - 1), j = std::min(i + 1, stereo.size() / 2 - 1);
        const float k = float(x - double(i));
        for (int c = 0; c < 2; ++c) resampled[f * 2 + c] = stereo[i * 2 + c] * (1 - k) + stereo[j * 2 + c] * k;
      }
      stereo.swap(resampled);
    }
    // Mixed in, the shorter of the two looped to the longer.
    if (stereo.size() > mix.size()) {
      std::vector<float> longer(stereo.size(), 0.0f);
      for (size_t i = 0; i < longer.size() && !mix.empty(); ++i) longer[i] = mix[i % mix.size()];
      mix.swap(longer);
    }
    for (size_t i = 0; i < mix.size(); ++i) mix[i] += t.gain * stereo[i % stereo.size()];
    REXLOG_INFO("Title music: {} ({} ch, {} Hz, {:.1f} s)", t.name, track->channels, track->rate,
                double(stereo.size() / 2) / double(rate));
  }
  return !mix.empty() && WriteWav(wav, rate, mix);
}

void ExportTitleMusicIfRequested(const fs::path& disc, const TitleMusicSource& source) {
  const std::string target = REXCVAR_GET(export_title_music);
  if (target.empty()) return;
  const bool ok = WriteTitleMusic(disc, source, fs::path(std::u8string(target.begin(), target.end())));
  REXLOG_INFO("Title music: {} {}", target, ok ? "written" : "not written");
  rex::FlushLogging();
  std::_Exit(ok ? 0 : 2);  // an export run only: nothing else started yet
}

void StartTitleMusicExport(const fs::path& disc, const TitleMusicSource& source, const fs::path& pictures_dir) {
  if (pictures_dir.empty() || source.empty()) return;
  const fs::path wav = pictures_dir / "title_music.wav";
  std::error_code ec;
  if (fs::is_regular_file(wav, ec)) return;
  std::thread([disc, source, wav] { WriteTitleMusic(disc, source, wav); }).detach();
}

}  // namespace skylanders
