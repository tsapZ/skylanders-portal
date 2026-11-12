#include "portal/prompt_icons.h"
#include "portal/prompt_tables.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "test_util.h"

using namespace skylanders::portal;

namespace {

uint32_t Get32(const std::vector<uint8_t>& v, size_t at) {
  return uint32_t(v[at]) | uint32_t(v[at + 1]) << 8 | uint32_t(v[at + 2]) << 16 | uint32_t(v[at + 3]) << 24;
}

// One DXT5 level of `s` x `s` from `at` back to RGBA, as a GPU reads it.
std::vector<uint8_t> DecodeDxt5(const std::vector<uint8_t>& dds, size_t at, int s) {
  std::vector<uint8_t> out(size_t(s) * s * 4);
  const int blocks = std::max(1, s / 4);
  for (int by = 0; by < blocks; ++by) {
    for (int bx = 0; bx < blocks; ++bx, at += 16) {
      const uint8_t* b = &dds[at];
      int alpha[8] = {b[0], b[1]};
      if (alpha[0] > alpha[1]) {
        for (int i = 1; i <= 6; ++i) alpha[i + 1] = ((7 - i) * alpha[0] + i * alpha[1]) / 7;
      } else {
        for (int i = 1; i <= 4; ++i) alpha[i + 1] = ((5 - i) * alpha[0] + i * alpha[1]) / 5;
        alpha[6] = 0;
        alpha[7] = 255;
      }
      uint64_t alpha_bits = 0;
      for (int i = 0; i < 6; ++i) alpha_bits |= uint64_t(b[2 + i]) << (8 * i);
      int pal[4][3];
      for (int k = 0; k < 2; ++k) {
        const int c = b[8 + 2 * k] | b[9 + 2 * k] << 8;
        const int r = (c >> 11) & 31, g = (c >> 5) & 63, bl = c & 31;
        pal[k][0] = (r << 3) | (r >> 2);
        pal[k][1] = (g << 2) | (g >> 4);
        pal[k][2] = (bl << 3) | (bl >> 2);
      }
      for (int k = 0; k < 3; ++k) {
        pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
        pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
      }
      const uint32_t color_bits = Get32(dds, at + 12);
      for (int i = 0; i < 16; ++i) {
        const int x = bx * 4 + i % 4, y = by * 4 + i / 4;
        if (x >= s || y >= s) continue;
        uint8_t* p = &out[(size_t(y) * s + x) * 4];
        for (int k = 0; k < 3; ++k) p[k] = uint8_t(pal[(color_bits >> (2 * i)) & 3][k]);
        p[3] = uint8_t(alpha[(alpha_bits >> (3 * i)) & 7]);
      }
    }
  }
  return out;
}

const uint8_t* Pixel(const std::vector<uint8_t>& rgba, int size, int x, int y) {
  return &rgba[(size_t(y) * size + x) * 4];
}

bool Near(const uint8_t* p, int r, int g, int b) {
  return std::abs(p[0] - r) <= 2 && std::abs(p[1] - g) <= 2 && std::abs(p[2] - b) <= 2 && p[3] == 255;
}

}  // namespace

int main() {
  const PromptStyle styles[] = {PromptStyle::kPlayStation, PromptStyle::kNintendo};
  const PromptButton buttons[] = {PromptButton::kA,  PromptButton::kB,     PromptButton::kX,  PromptButton::kY,
                                  PromptButton::kStart, PromptButton::kBack, PromptButton::kLB, PromptButton::kRB,
                                  PromptButton::kLT, PromptButton::kRT,    PromptButton::kLS, PromptButton::kRS,
                                  PromptButton::kDpad};

  // Every icon: its size, see-through corners, an opaque middle, its own picture, upside down exactly when asked.
  for (const PromptStyle style : styles) {
    std::vector<std::vector<uint8_t>> seen;
    for (const PromptButton button : buttons) {
      const int s = 64;
      const auto icon = DrawPromptIcon(style, button, s, false);
      REQUIRE_OR_RETURN(icon.size() == size_t(s) * s * 4);
      CHECK(Pixel(icon, s, 0, 0)[3] == 0);
      CHECK(Pixel(icon, s, s - 1, 0)[3] == 0);
      CHECK(Pixel(icon, s, 0, s - 1)[3] == 0);
      CHECK(Pixel(icon, s, s - 1, s - 1)[3] == 0);
      CHECK(Pixel(icon, s, s / 2, s / 2)[3] == 255);
      const auto flipped = DrawPromptIcon(style, button, s, true);
      REQUIRE_OR_RETURN(flipped.size() == icon.size());
      bool mirrored = true;
      for (int y = 0; y < s; ++y) {
        mirrored = mirrored &&
                   std::memcmp(&flipped[size_t(y) * s * 4], &icon[size_t(s - 1 - y) * s * 4], size_t(s) * 4) == 0;
      }
      CHECK(mirrored);
      if (button == PromptButton::kY) CHECK(flipped != icon);  // the triangle points the other way
      for (const auto& other : seen) CHECK(other != icon);
      seen.push_back(icon);
    }
  }
  CHECK(DrawPromptIcon(PromptStyle::kNintendo, PromptButton::kA, 2, false).empty());

  // The PlayStation symbols in their colours: the cross over the middle, the circle's ring, the button's face inside it.
  {
    const int s = 256, c = s / 2;
    const auto cross = DrawPromptIcon(PromptStyle::kPlayStation, PromptButton::kA, s, false);
    CHECK(Near(Pixel(cross, s, c, c), 124, 178, 236));
    const auto circle = DrawPromptIcon(PromptStyle::kPlayStation, PromptButton::kB, s, false);
    CHECK(Near(Pixel(circle, s, c, c), 43, 43, 51));
    CHECK(Near(Pixel(circle, s, c + int(0.19f * s) - 11, c), 240, 112, 116));
    const auto square = DrawPromptIcon(PromptStyle::kPlayStation, PromptButton::kX, s, false);
    CHECK(Near(Pixel(square, s, c, c + int(0.17f * s) - 11), 232, 146, 206));
    const auto plus = DrawPromptIcon(PromptStyle::kNintendo, PromptButton::kStart, s, false);
    CHECK(Near(Pixel(plus, s, c, c), 245, 245, 248));
  }

  // The DDS: a DXT5 header, the whole mip chain, blocks that decode back to the picture, a last level that keeps its
  // coverage.
  {
    const int s = 256;
    const auto icon = DrawPromptIcon(PromptStyle::kPlayStation, PromptButton::kY, s, false);
    const auto dds = EncodeDdsDxt5(icon, s);
    REQUIRE_OR_RETURN(dds.size() > 128);
    CHECK(std::memcmp(dds.data(), "DDS ", 4) == 0);
    CHECK(Get32(dds, 4) == 124);
    CHECK(Get32(dds, 12) == 256 && Get32(dds, 16) == 256);
    CHECK(Get32(dds, 28) == 9);  // 256 down to 1
    CHECK(Get32(dds, 76) == 32);
    CHECK(std::memcmp(&dds[84], "DXT5", 4) == 0);
    size_t blocks = 0;
    for (int m = s; m >= 1; m /= 2) blocks += size_t(std::max(1, m / 4)) * std::max(1, m / 4);
    REQUIRE_OR_RETURN(dds.size() == 128 + blocks * 16);

    const auto back = DecodeDxt5(dds, 128, s);
    double alpha_error = 0, color_error = 0;
    size_t opaque = 0;
    for (size_t i = 0; i < size_t(s) * s; ++i) {
      alpha_error += std::abs(back[i * 4 + 3] - icon[i * 4 + 3]);
      if (icon[i * 4 + 3] < 255) continue;
      ++opaque;
      for (int k = 0; k < 3; ++k) color_error += std::abs(back[i * 4 + k] - icon[i * 4 + k]);
    }
    alpha_error /= double(s) * s;
    color_error /= 3.0 * std::max<size_t>(opaque, 1);
    std::printf("DXT5 round trip: mean alpha error %.2f, mean colour error %.2f\n", alpha_error, color_error);
    CHECK(opaque > size_t(s) * s / 2);
    CHECK(alpha_error < 1.5);
    CHECK(color_error < 4.0);

    const auto last = DecodeDxt5(dds, dds.size() - 16, 1);
    const int coverage = last[3];  // the round button covers about pi * 0.46^2 of the square
    CHECK(coverage > 150 && coverage < 190);
    CHECK(last[0] > 30 && last[0] < 90);  // mostly the dark face

    CHECK(EncodeDdsDxt5(icon, 128).empty());
    CHECK(EncodeDdsDxt5(std::vector<uint8_t>(48 * 48 * 4), 48).empty());
  }

  // A button atlas: the rest (here a red block) scaled up as it is, the button's cell cleared and redrawn, an 8888
  // DDS (A8R8G8B8 masks, B, G, R, A bytes) at 4x with every mip.
  {
    const int w = 32, h = 16, scale = 4;
    std::vector<uint8_t> atlas(size_t(w) * h * 4, 0);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        uint8_t* t = &atlas[(size_t(y) * w + x) * 4];
        if (x < 17) {
          t[0] = 0, t[1] = 0, t[2] = 255, t[3] = 255;  // red (B, G, R, A), its last column two texels from the cell
        } else if (x >= 18 && x < 30 && y >= 2 && y < 14) {
          t[0] = 0, t[1] = 200, t[2] = 0, t[3] = 255;  // the game's green A
        }
      }
    }
    const AtlasCell cells[] = {{PromptButton::kA, 18, 2, 12, 12}};
    const auto dds = PatchButtonAtlas(atlas.data(), w, h, cells, PromptStyle::kPlayStation, scale);
    const int W = w * scale, H = h * scale;
    size_t texels = 0;
    int mips = 0;
    for (int mw = W, mh = H;; mw = std::max(1, mw / 2), mh = std::max(1, mh / 2)) {
      texels += size_t(mw) * mh;
      ++mips;
      if (mw == 1 && mh == 1) break;
    }
    REQUIRE_OR_RETURN(dds.size() == 128 + texels * 4);
    CHECK(std::memcmp(dds.data(), "DDS ", 4) == 0);
    CHECK(Get32(dds, 12) == uint32_t(H) && Get32(dds, 16) == uint32_t(W));
    CHECK(Get32(dds, 20) == uint32_t(W * 4));
    CHECK(Get32(dds, 28) == uint32_t(mips));
    CHECK(Get32(dds, 80) == 0x41 && Get32(dds, 88) == 32);
    CHECK(Get32(dds, 92) == 0x00FF0000 && Get32(dds, 96) == 0x0000FF00 && Get32(dds, 100) == 0x000000FF);
    const auto texel = [&](int x, int y) { return &dds[128 + (size_t(y) * W + x) * 4]; };
    const uint8_t* red = texel(4 * 4, 8 * 4);
    CHECK(red[0] == 0 && red[1] == 0 && red[2] == 255 && red[3] == 255);
    const uint8_t* cross = texel(4 * 24, 4 * 8);  // the cell's middle: our cross (124, 178, 236)
    CHECK(std::abs(cross[2] - 124) <= 2 && std::abs(cross[1] - 178) <= 2 && std::abs(cross[0] - 236) <= 2);
    CHECK(texel(4 * 17 + 1, 4 * 8)[3] == 0);  // the game's button's edge, cleared
    CHECK(texel(4 * 16 + 2, 4 * 8)[3] == 0);    // two texels around the cell go too (here the red block's last)
    CHECK(texel(4 * 15 + 2, 4 * 8)[3] == 255);  // the third one stays
    CHECK(PatchButtonAtlas(atlas.data(), w, h, cells, PromptStyle::kPlayStation, 9).empty());
  }

  // The tables: the games with prompts we know, every cell inside its atlas, no two cells overlapping.
  {
    CHECK(PromptTexturesOf("Spyro's Adventure").size() == 3);
    for (const char* game : {"Swap Force", "SuperChargers", "Imaginators"}) {
      const auto atlases = PromptAtlasesOf(game);
      CHECK(atlases.size() == 2);
      for (const PromptAtlas& atlas : atlases) {
        CHECK(!atlas.cells.empty());
        for (size_t i = 0; i < atlas.cells.size(); ++i) {
          const AtlasCell& a = atlas.cells[i];
          CHECK(a.w > 0 && a.h > 0 && a.x + a.w <= atlas.width && a.y + a.h <= atlas.height);
          for (size_t j = i + 1; j < atlas.cells.size(); ++j) {
            const AtlasCell& b = atlas.cells[j];
            const bool apart = a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
            CHECK(apart);
          }
        }
      }
    }
    for (const char* game : {"Giants", "Trap Team", "Spyro's Adventure"}) CHECK(PromptAtlasesOf(game).empty());
    CHECK(PromptTexturesOf("Swap Force").empty());
  }

  return Finish("prompt_icons");
}
