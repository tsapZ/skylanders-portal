#include "alchemy/xenos_dxt.h"

#include <algorithm>
#include <cstring>

namespace skylanders::alchemy {
namespace {

// Xbox 360 2D tiling (XGAddress2DTiledOffset), in blocks.
uint32_t TiledOffset2D(uint32_t x, uint32_t y, uint32_t pitch, uint32_t log2_bpb) {
  pitch = (pitch + 31) & ~31u;
  const uint32_t macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (log2_bpb + 7);
  const uint32_t micro = ((x & 7) + ((y & 0xE) << 2)) << log2_bpb;
  const uint32_t off = macro + ((micro & ~0xFu) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return ((off & ~0x1FFu) << 3) + ((y & 16) << 7) + ((off & 0x1C0) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (off & 0x3F);
}

void Rgb565(uint16_t c, uint8_t out[3]) {
  out[0] = uint8_t((((c >> 11) & 31) * 255 + 15) / 31);
  out[1] = uint8_t((((c >> 5) & 63) * 255 + 31) / 63);
  out[2] = uint8_t(((c & 31) * 255 + 15) / 31);
}

// One 4x4 colour block (little-endian after the byte swap).
void DecodeColorBlock(const uint8_t* b, bool dxt1, RgbaImage& img, int x0, int y0) {
  const uint16_t c0 = uint16_t(b[0] | (b[1] << 8)), c1 = uint16_t(b[2] | (b[3] << 8));
  const uint32_t indices = b[4] | (b[5] << 8) | (b[6] << 16) | (uint32_t(b[7]) << 24);
  uint8_t pal[4][4];
  Rgb565(c0, pal[0]);
  Rgb565(c1, pal[1]);
  pal[0][3] = pal[1][3] = 255;
  if (c0 > c1 || !dxt1) {
    for (int k = 0; k < 3; ++k) {
      pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k]) / 3);
      pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3);
    }
    pal[2][3] = pal[3][3] = 255;
  } else {
    for (int k = 0; k < 3; ++k) pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2);
    pal[2][3] = 255;
    std::memset(pal[3], 0, 4);
  }
  for (int i = 0; i < 16; ++i) {
    const int x = x0 + i % 4, y = y0 + i / 4;
    if (x >= img.width || y >= img.height) continue;
    std::memcpy(&img.pixels[(size_t(y) * img.width + x) * 4], pal[(indices >> (2 * i)) & 3], 4);
  }
}

void DecodeAlphaBlock(const uint8_t* b, Dxt kind, RgbaImage& img, int x0, int y0) {
  uint8_t alpha[16];
  if (kind == Dxt::kDxt5) {
    const uint32_t a0 = b[0], a1 = b[1];
    uint8_t table[8] = {uint8_t(a0), uint8_t(a1)};
    if (a0 > a1) {
      for (int i = 0; i < 6; ++i) table[2 + i] = uint8_t(((6 - i) * a0 + (1 + i) * a1) / 7);
    } else {
      for (int i = 0; i < 4; ++i) table[2 + i] = uint8_t(((4 - i) * a0 + (1 + i) * a1) / 5);
      table[6] = 0;
      table[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) alpha[i] = table[(bits >> (3 * i)) & 7];
  } else {
    for (int i = 0; i < 16; ++i) alpha[i] = uint8_t(((b[i / 2] >> (4 * (i & 1))) & 0xF) * 17);
  }
  for (int i = 0; i < 16; ++i) {
    const int x = x0 + i % 4, y = y0 + i / 4;
    if (x >= img.width || y >= img.height) continue;
    img.pixels[(size_t(y) * img.width + x) * 4 + 3] = alpha[i];
  }
}

}  // namespace

void FlipVertically(RgbaImage& image) {
  const size_t row = size_t(image.width) * 4;
  for (int y = 0; y < image.height / 2; ++y) {
    std::swap_ranges(image.pixels.begin() + ptrdiff_t(y * row),
                     image.pixels.begin() + ptrdiff_t((y + 1) * row),
                     image.pixels.begin() + ptrdiff_t((image.height - 1 - y) * row));
  }
}

std::optional<RgbaImage> DecodeXenosDxt(const uint8_t* data, size_t size, int width, int height,
                                        Dxt kind, bool tiled) {
  if (width <= 0 || height <= 0) return std::nullopt;
  const uint32_t bpb = kind == Dxt::kDxt1 ? 8 : 16;
  const uint32_t bw = (uint32_t(width) + 3) / 4, bh = (uint32_t(height) + 3) / 4;
  const uint32_t pitch = (bw + 31) & ~31u;  // rows padded to 32 blocks
  RgbaImage img;
  img.width = width;
  img.height = height;
  img.pixels.assign(size_t(width) * height * 4, 0);
  uint8_t block[16];
  for (uint32_t by = 0; by < bh; ++by) {
    for (uint32_t bx = 0; bx < bw; ++bx) {
      const uint32_t src =
          tiled ? TiledOffset2D(bx, by, bw, bpb == 8 ? 3 : 4) : (by * pitch + bx) * bpb;
      if (src + bpb > size) return std::nullopt;
      // 16-bit words are big-endian.
      for (uint32_t i = 0; i < bpb; ++i) block[i] = data[src + (i ^ 1)];
      const int x0 = int(bx * 4), y0 = int(by * 4);
      if (kind == Dxt::kDxt1) {
        DecodeColorBlock(block, true, img, x0, y0);
      } else {
        DecodeColorBlock(block + 8, false, img, x0, y0);
        DecodeAlphaBlock(block, kind, img, x0, y0);
      }
    }
  }
  return img;
}

}  // namespace skylanders::alchemy
