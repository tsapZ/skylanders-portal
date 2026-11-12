#include "portal/prompt_icons.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace skylanders::portal {
namespace {

struct Vec {
  float x, y;
};
Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y}; }
Vec operator*(Vec a, float s) { return {a.x * s, a.y * s}; }
float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y; }
float Len(Vec a) { return std::sqrt(Dot(a, a)); }

struct Color {
  float r, g, b, a;  // straight, 0 to 1
};
Color Rgb(int r, int g, int b, int a = 255) { return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f}; }

// The distance from p to the segment a-b.
float Segment(Vec p, Vec a, Vec b) {
  const Vec pa = p - a, ba = b - a;
  const float h = std::clamp(Dot(pa, ba) / std::max(Dot(ba, ba), 1e-6f), 0.0f, 1.0f);
  return Len(pa - ba * h);
}

// The signed distance to a box around c with half sizes `half` and corners of radius r.
float RoundBox(Vec p, Vec c, Vec half, float r) {
  const float qx = std::abs(p.x - c.x) - half.x + r, qy = std::abs(p.y - c.y) - half.y + r;
  return Len({std::max(qx, 0.0f), std::max(qy, 0.0f)}) + std::min(std::max(qx, qy), 0.0f) - r;
}

// Premultiplied pixels; shapes painted by their signed distance (negative inside) with a soft edge of one pixel.
class Canvas {
 public:
  Canvas(int size, bool flip) : size_(size), flip_(flip), px_(size_t(size) * size, Color{0, 0, 0, 0}) {}

  template <class Dist>
  void Fill(Color c, Dist dist) {
    for (int y = 0; y < size_; ++y) {
      // Upside down: the row shows what the picture has at the mirrored row.
      const float sy = (flip_ ? float(size_ - 1 - y) : float(y)) + 0.5f;
      for (int x = 0; x < size_; ++x) {
        const float cover = std::clamp(0.5f - dist(Vec{x + 0.5f, sy}), 0.0f, 1.0f) * c.a;
        if (cover <= 0.0f) continue;
        Color& o = px_[size_t(y) * size_ + x];
        o.r = c.r * cover + o.r * (1.0f - cover);
        o.g = c.g * cover + o.g * (1.0f - cover);
        o.b = c.b * cover + o.b * (1.0f - cover);
        o.a = cover + o.a * (1.0f - cover);
      }
    }
  }

  // Strokes along the points with round ends and joints, `width` wide.
  void Stroke(Color c, std::initializer_list<Vec> points, float width) {
    const std::vector<Vec> pts(points);
    Fill(c, [&](Vec p) {
      float d = 1e9f;
      for (size_t i = 0; i + 1 < pts.size(); ++i) d = std::min(d, Segment(p, pts[i], pts[i + 1]));
      return d - width / 2;
    });
  }

  std::vector<uint8_t> Rgba() const {
    std::vector<uint8_t> out(px_.size() * 4);
    for (size_t i = 0; i < px_.size(); ++i) {
      const Color& c = px_[i];
      const float a = c.a > 0.0f ? c.a : 1.0f;
      out[i * 4 + 0] = uint8_t(std::lround(std::clamp(c.r / a, 0.0f, 1.0f) * 255));
      out[i * 4 + 1] = uint8_t(std::lround(std::clamp(c.g / a, 0.0f, 1.0f) * 255));
      out[i * 4 + 2] = uint8_t(std::lround(std::clamp(c.b / a, 0.0f, 1.0f) * 255));
      out[i * 4 + 3] = uint8_t(std::lround(std::clamp(c.a, 0.0f, 1.0f) * 255));
    }
    return out;
  }

 private:
  int size_;
  bool flip_;
  std::vector<Color> px_;
};

// A round button: a dark rim, its face, a soft light on its top half.
void RoundBase(Canvas& cv, float s, Color rim, Color face) {
  const Vec c{s / 2, s / 2};
  const float r = 0.46f * s, r2 = r - 7 * s / 256;
  cv.Fill(rim, [&](Vec p) { return Len(p - c) - r; });
  cv.Fill(face, [&](Vec p) { return Len(p - c) - r2; });
  const Vec hc{s / 2, s / 2 - 0.485f * r2};
  const float rx = 0.8f * r2, ry = 0.435f * r2;
  cv.Fill(Rgb(255, 255, 255, 26), [&](Vec p) {
    const Vec q{(p.x - hc.x) / rx, (p.y - hc.y) / ry};
    return (Len(q) - 1.0f) * std::min(rx, ry);
  });
}

// A pill (PlayStation's Options and Share).
void PillBase(Canvas& cv, float s, Color rim, Color face) {
  const Vec c{s / 2, s / 2};
  const Vec half{0.43f * s, 0.26f * s};
  const float m = 7 * s / 256;
  cv.Fill(rim, [&](Vec p) { return RoundBox(p, c, half, half.y); });
  cv.Fill(face, [&](Vec p) { return RoundBox(p, c, {half.x - m, half.y - m}, half.y - m); });
}

void PlayStation(Canvas& cv, PromptButton button, float s) {
  const float c = s / 2, k = s / 256, w = 22 * k;
  const Color rim = Rgb(16, 16, 20), face = Rgb(43, 43, 51), light = Rgb(235, 235, 240);
  switch (button) {
    case PromptButton::kStart: {  // Options: three lines
      PillBase(cv, s, rim, face);
      for (const float dy : {-0.09f, 0.0f, 0.09f}) {
        cv.Stroke(light, {{c - 0.13f * s, c + dy * s}, {c + 0.13f * s, c + dy * s}}, 12 * k);
      }
      return;
    }
    case PromptButton::kBack: {  // Share: an arrow up
      PillBase(cv, s, rim, face);
      cv.Stroke(light, {{c, c + 0.10f * s}, {c, c - 0.14f * s}}, 11 * k);
      cv.Stroke(light, {{c - 0.07f * s, c - 0.07f * s}, {c, c - 0.14f * s}, {c + 0.07f * s, c - 0.07f * s}}, 11 * k);
      return;
    }
    default:
      break;
  }
  RoundBase(cv, s, rim, face);
  const Vec ctr{c, c};
  switch (button) {
    case PromptButton::kA: {  // cross
      const float d = 0.17f * s;
      const Color col = Rgb(124, 178, 236);
      cv.Stroke(col, {{c - d, c - d}, {c + d, c + d}}, w);
      cv.Stroke(col, {{c + d, c - d}, {c - d, c + d}}, w);
      break;
    }
    case PromptButton::kB: {  // circle
      const float r = 0.19f * s - w / 2;
      cv.Fill(Rgb(240, 112, 116), [&](Vec p) { return std::abs(Len(p - ctr) - r) - w / 2; });
      break;
    }
    case PromptButton::kX: {  // square
      const float h = 0.17f * s - w / 2;
      cv.Fill(Rgb(232, 146, 206), [&](Vec p) { return std::abs(RoundBox(p, ctr, {h, h}, 2 * k)) - w / 2; });
      break;
    }
    case PromptButton::kY: {  // triangle
      const float r = 0.25f * s, cy = c + 0.03f * s;
      const Vec a{c, cy - r}, b{c + 0.866f * r, cy + r / 2}, d{c - 0.866f * r, cy + r / 2};
      cv.Stroke(Rgb(80, 206, 162), {a, b, d, a}, 0.85f * w);
      break;
    }
    default:
      break;
  }
}

// Nintendo's letters sit by position: the bottom button is B, the right one A, the left Y, the top X.
void Nintendo(Canvas& cv, PromptButton button, float s) {
  RoundBase(cv, s, Rgb(22, 22, 26), Rgb(58, 58, 66));
  const float c = s / 2, k = s / 256, w = 21 * k, h = 0.36f * s;
  const float top = c - h / 2, bot = c + h / 2;
  const Color ink = Rgb(245, 245, 248);
  switch (button) {
    case PromptButton::kA: {  // "B"
      const float hb = 0.44f * s, wb = 0.8f * w;
      const float tb = c - hb / 2, bb = c + hb / 2, x0 = c - 0.27f * hb, mid = c - 0.01f * hb;
      struct Bowl {
        float y0, y1, bulge;  // its top and bottom stroke, how far right it reaches (of the letter's height)
      };
      for (const Bowl& bowl : {Bowl{tb, mid, 0.40f}, Bowl{mid, bb, 0.47f}}) {
        const float r = (bowl.y1 - bowl.y0) / 2, xr = x0 + bowl.bulge * hb - r;
        cv.Stroke(ink, {{x0, bowl.y0}, {xr, bowl.y0}}, wb);
        cv.Stroke(ink, {{x0, bowl.y1}, {xr, bowl.y1}}, wb);
        const Vec cc{xr, (bowl.y0 + bowl.y1) / 2};
        cv.Fill(ink, [&](Vec p) { return std::max(std::abs(Len(p - cc) - r) - wb / 2, xr - p.x); });
      }
      cv.Stroke(ink, {{x0, tb}, {x0, bb}}, wb);
      break;
    }
    case PromptButton::kB: {  // "A"
      cv.Stroke(ink, {{c - 0.42f * h, bot}, {c, top}, {c + 0.42f * h, bot}}, w);
      cv.Stroke(ink, {{c - 0.24f * h, c + 0.14f * h}, {c + 0.24f * h, c + 0.14f * h}}, w);
      break;
    }
    case PromptButton::kX: {  // "Y"
      const float d = 0.42f * h;
      cv.Stroke(ink, {{c - d, top}, {c, c}, {c + d, top}}, w);
      cv.Stroke(ink, {{c, c}, {c, bot}}, w);
      break;
    }
    case PromptButton::kY: {  // "X"
      const float d = 0.40f * h;
      cv.Stroke(ink, {{c - d, top}, {c + d, bot}}, w);
      cv.Stroke(ink, {{c + d, top}, {c - d, bot}}, w);
      break;
    }
    case PromptButton::kStart: {  // "+"
      const float d = 0.42f * h;
      cv.Stroke(ink, {{c - d, c}, {c + d, c}}, w);
      cv.Stroke(ink, {{c, c - d}, {c, c + d}}, w);
      break;
    }
    case PromptButton::kBack: {  // "-"
      const float d = 0.42f * h;
      cv.Stroke(ink, {{c - d, c}, {c + d, c}}, w);
      break;
    }
  }
}

// --- DXT5 ---

uint16_t To565(const int* c) { return uint16_t((c[0] >> 3) << 11 | (c[1] >> 2) << 5 | (c[2] >> 3)); }
void From565(uint16_t v, int* c) {
  const int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
  c[0] = (r << 3) | (r >> 2);
  c[1] = (g << 2) | (g >> 4);
  c[2] = (b << 3) | (b >> 2);
}

// One 4x4 block (16 RGBA pixels): alpha from its extremes in 8 steps, colour from the lightest and the darkest of the
// pixels that show (a simple fit: the icons are flat colours with soft edges).
void EncodeBlock(const uint8_t (&px)[16][4], uint8_t* out) {
  int a0 = 0, a1 = 255;
  for (const auto& p : px) {
    a0 = std::max(a0, int(p[3]));
    a1 = std::min(a1, int(p[3]));
  }
  uint64_t alpha_bits = 0;
  if (a0 != a1) {
    int steps[8] = {a0, a1};
    for (int i = 1; i <= 6; ++i) steps[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    for (int i = 0; i < 16; ++i) {
      int best = 0;
      for (int j = 1; j < 8; ++j) {
        if (std::abs(px[i][3] - steps[j]) < std::abs(px[i][3] - steps[best])) best = j;
      }
      alpha_bits |= uint64_t(best) << (3 * i);
    }
  }
  out[0] = uint8_t(a0);
  out[1] = uint8_t(a1);
  for (int i = 0; i < 6; ++i) out[2 + i] = uint8_t(alpha_bits >> (8 * i));

  int hi = -1, lo = -1, hi_l = -1, lo_l = 1 << 30;
  for (int i = 0; i < 16; ++i) {
    if (px[i][3] <= 8) continue;
    const int l = 2 * px[i][0] + 4 * px[i][1] + px[i][2];
    if (l > hi_l) hi_l = l, hi = i;
    if (l < lo_l) lo_l = l, lo = i;
  }
  int chi[3] = {0, 0, 0}, clo[3] = {0, 0, 0};
  if (hi >= 0) {
    for (int k = 0; k < 3; ++k) chi[k] = px[hi][k], clo[k] = px[lo][k];
  }
  uint16_t c0 = To565(chi), c1 = To565(clo);
  if (c0 < c1) std::swap(c0, c1);
  uint32_t color_bits = 0;
  if (c0 != c1) {
    int pal[4][3];
    From565(c0, pal[0]);
    From565(c1, pal[1]);
    for (int k = 0; k < 3; ++k) {
      pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
      pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
    }
    for (int i = 0; i < 16; ++i) {
      int best = 0, best_d = 1 << 30;
      for (int j = 0; j < 4; ++j) {
        int d = 0;
        for (int k = 0; k < 3; ++k) d += (px[i][k] - pal[j][k]) * (px[i][k] - pal[j][k]);
        if (d < best_d) best_d = d, best = j;
      }
      color_bits |= uint32_t(best) << (2 * i);
    }
  }
  std::memcpy(out + 8, &c0, 2);
  std::memcpy(out + 10, &c1, 2);
  std::memcpy(out + 12, &color_bits, 4);
}

void Put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i)));
}

}  // namespace

std::vector<uint8_t> DrawPromptIcon(PromptStyle style, PromptButton button, int size, bool flip) {
  if (size < 4 || size > 4096) return {};
  Canvas cv(size, flip);
  if (style == PromptStyle::kPlayStation) {
    PlayStation(cv, button, float(size));
  } else {
    Nintendo(cv, button, float(size));
  }
  return cv.Rgba();
}

std::vector<uint8_t> EncodeDdsDxt5(const std::vector<uint8_t>& rgba, int size) {
  if (size < 1 || (size & (size - 1)) != 0 || rgba.size() != size_t(size) * size * 4) return {};
  int mips = 1;
  for (int s = size; s > 1; s /= 2) ++mips;
  std::vector<uint8_t> out = {'D', 'D', 'S', ' '};
  Put32(out, 124);
  Put32(out, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000);  // caps, height, width, pixel format, mips, linear size
  Put32(out, uint32_t(size));
  Put32(out, uint32_t(size));
  Put32(out, uint32_t(std::max(1, size / 4) * std::max(1, size / 4) * 16));
  Put32(out, 0);
  Put32(out, uint32_t(mips));
  out.resize(out.size() + 44, 0);
  Put32(out, 32);   // pixel format
  Put32(out, 0x4);  // FourCC
  out.insert(out.end(), {'D', 'X', 'T', '5'});
  out.resize(out.size() + 20, 0);
  Put32(out, 0x8 | 0x1000 | 0x400000);  // complex, texture, mipmap
  out.resize(out.size() + 16, 0);

  // Each level from the one above, averaged with the colours weighted by their alpha (no dark fringes).
  std::vector<uint8_t> level = rgba;
  for (int s = size; s >= 1; s /= 2) {
    const int blocks = std::max(1, s / 4);
    for (int by = 0; by < blocks; ++by) {
      for (int bx = 0; bx < blocks; ++bx) {
        uint8_t px[16][4] = {};
        for (int y = 0; y < 4; ++y) {
          for (int x = 0; x < 4; ++x) {
            const int sx = bx * 4 + x, sy = by * 4 + y;
            if (sx >= s || sy >= s) continue;
            std::memcpy(px[y * 4 + x], &level[(size_t(sy) * s + sx) * 4], 4);
          }
        }
        uint8_t block[16];
        EncodeBlock(px, block);
        out.insert(out.end(), block, block + 16);
      }
    }
    if (s == 1) break;
    const int n = s / 2;
    std::vector<uint8_t> next(size_t(n) * n * 4);
    for (int y = 0; y < n; ++y) {
      for (int x = 0; x < n; ++x) {
        float sum[4] = {0, 0, 0, 0};
        for (int dy = 0; dy < 2; ++dy) {
          for (int dx = 0; dx < 2; ++dx) {
            const uint8_t* p = &level[(size_t(2 * y + dy) * s + 2 * x + dx) * 4];
            const float a = p[3] / 255.0f;
            for (int k = 0; k < 3; ++k) sum[k] += p[k] * a;
            sum[3] += a;
          }
        }
        uint8_t* q = &next[(size_t(y) * n + x) * 4];
        for (int k = 0; k < 3; ++k) q[k] = uint8_t(sum[3] > 0 ? std::lround(sum[k] / sum[3]) : 0);
        q[3] = uint8_t(std::lround(sum[3] / 4 * 255));
      }
    }
    level = std::move(next);
  }
  return out;
}

}  // namespace skylanders::portal
