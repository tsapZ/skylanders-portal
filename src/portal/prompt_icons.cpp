#include "portal/prompt_icons.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string_view>

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

// A rectangle of the picture to draw in, in pixels.
struct Box {
  float x, y, w, h;
  Vec c() const { return {x + w / 2, y + h / 2}; }
  float s() const { return std::min(w, h); }  // the side of the square centred in it
};

// Premultiplied pixels; shapes painted by their signed distance (negative inside) with a soft edge of one pixel.
class Canvas {
 public:
  Canvas(int width, int height, bool flip)
      : width_(width), height_(height), flip_(flip), px_(size_t(width) * height, Color{0, 0, 0, 0}) {}

  // Paints the shape `dist` in `c`, looking only at `bounds` (and 2 px around it).
  template <class Dist>
  void Fill(Color c, const Box& bounds, Dist dist) {
    const int x0 = std::max(0, int(std::floor(bounds.x)) - 2);
    const int x1 = std::min(width_, int(std::ceil(bounds.x + bounds.w)) + 2);
    const int y0 = std::max(0, int(std::floor(bounds.y)) - 2);
    const int y1 = std::min(height_, int(std::ceil(bounds.y + bounds.h)) + 2);
    for (int row = y0; row < y1; ++row) {
      // Upside down: the picture's row `row` lands on the mirrored row.
      const int y = flip_ ? height_ - 1 - row : row;
      for (int x = x0; x < x1; ++x) {
        const float cover = std::clamp(0.5f - dist(Vec{x + 0.5f, row + 0.5f}), 0.0f, 1.0f) * c.a;
        if (cover <= 0.0f) continue;
        Color& o = px_[size_t(y) * width_ + x];
        o.r = c.r * cover + o.r * (1.0f - cover);
        o.g = c.g * cover + o.g * (1.0f - cover);
        o.b = c.b * cover + o.b * (1.0f - cover);
        o.a = cover + o.a * (1.0f - cover);
      }
    }
  }

  // Strokes along the points with round ends and joints, `width` wide.
  void Stroke(Color c, const std::vector<Vec>& pts, float width) {
    if (pts.empty()) return;
    float x0 = pts[0].x, x1 = x0, y0 = pts[0].y, y1 = y0;
    for (const Vec& p : pts) {
      x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
    }
    const Box bounds{x0 - width, y0 - width, x1 - x0 + 2 * width, y1 - y0 + 2 * width};
    Fill(c, bounds, [&](Vec p) {
      float d = Len(p - pts[0]);
      for (size_t i = 0; i + 1 < pts.size(); ++i) d = std::min(d, Segment(p, pts[i], pts[i + 1]));
      return d - width / 2;
    });
  }
  void Stroke(Color c, std::initializer_list<Vec> pts, float width) { Stroke(c, std::vector<Vec>(pts), width); }

  // Makes the box see-through.
  void Clear(const Box& b) {
    const int x0 = std::max(0, int(b.x)), x1 = std::min(width_, int(std::ceil(b.x + b.w)));
    const int y0 = std::max(0, int(b.y)), y1 = std::min(height_, int(std::ceil(b.y + b.h)));
    for (int y = y0; y < y1; ++y) {
      for (int x = x0; x < x1; ++x) px_[size_t(y) * width_ + x] = Color{0, 0, 0, 0};
    }
  }

  // The B, G, R, A picture `bgra` (w x h) scaled up `scale` times, bilinear on premultiplied colours.
  void LoadScaled(const uint8_t* bgra, int w, int h, int scale) {
    const auto texel = [&](int x, int y) {
      x = std::clamp(x, 0, w - 1);
      y = std::clamp(y, 0, h - 1);
      const uint8_t* t = bgra + (size_t(y) * w + x) * 4;
      const float a = t[3] / 255.0f;
      return Color{t[2] / 255.0f * a, t[1] / 255.0f * a, t[0] / 255.0f * a, a};
    };
    for (int y = 0; y < height_; ++y) {
      const float v = (y + 0.5f) / scale - 0.5f;
      const int ty = int(std::floor(v));
      const float fy = v - ty;
      for (int x = 0; x < width_; ++x) {
        const float u = (x + 0.5f) / scale - 0.5f;
        const int tx = int(std::floor(u));
        const float fx = u - tx;
        const Color c00 = texel(tx, ty), c10 = texel(tx + 1, ty), c01 = texel(tx, ty + 1), c11 = texel(tx + 1, ty + 1);
        const auto mix = [&](float a, float b, float c, float d) {
          return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
        };
        px_[size_t(y) * width_ + x] = Color{mix(c00.r, c10.r, c01.r, c11.r), mix(c00.g, c10.g, c01.g, c11.g),
                                            mix(c00.b, c10.b, c01.b, c11.b), mix(c00.a, c10.a, c01.a, c11.a)};
      }
    }
  }

  // Straight bytes, R, G, B, A (or B, G, R, A).
  std::vector<uint8_t> Bytes(bool bgra) const {
    std::vector<uint8_t> out(px_.size() * 4);
    for (size_t i = 0; i < px_.size(); ++i) {
      const Color& c = px_[i];
      const float a = c.a > 0.0f ? c.a : 1.0f;
      const uint8_t r = uint8_t(std::lround(std::clamp(c.r / a, 0.0f, 1.0f) * 255));
      const uint8_t g = uint8_t(std::lround(std::clamp(c.g / a, 0.0f, 1.0f) * 255));
      const uint8_t b = uint8_t(std::lround(std::clamp(c.b / a, 0.0f, 1.0f) * 255));
      out[i * 4 + 0] = bgra ? b : r;
      out[i * 4 + 1] = g;
      out[i * 4 + 2] = bgra ? r : b;
      out[i * 4 + 3] = uint8_t(std::lround(std::clamp(c.a, 0.0f, 1.0f) * 255));
    }
    return out;
  }

 private:
  int width_, height_;
  bool flip_;
  std::vector<Color> px_;
};

// --- Letters for the labels (L1, R2, ZL, ...): strokes in a box one unit high (y down), round ends. ---

void Arc(std::vector<Vec>& pts, Vec c, float r, float from_deg, float to_deg) {
  const int n = std::max(4, int(std::abs(to_deg - from_deg) / 15));
  for (int i = 0; i <= n; ++i) {
    const float a = (from_deg + (to_deg - from_deg) * i / n) * 3.14159265f / 180.0f;
    pts.push_back({c.x + r * std::cos(a), c.y + r * std::sin(a)});
  }
}

struct Glyph {
  float width;
  std::vector<std::vector<Vec>> strokes;
};

Glyph GlyphOf(char ch) {
  switch (ch) {
    case 'L':
      return {0.52f, {{{0, 0}, {0, 1}, {0.52f, 1}}}};
    case 'R': {
      std::vector<Vec> bowl = {{0, 1}, {0, 0}, {0.3f, 0}};
      Arc(bowl, {0.3f, 0.26f}, 0.26f, 270, 450);
      bowl.push_back({0, 0.52f});
      return {0.58f, {bowl, {{0.26f, 0.52f}, {0.58f, 1}}}};
    }
    case 'Z':
      return {0.58f, {{{0, 0}, {0.58f, 0}, {0, 1}, {0.58f, 1}}}};
    case '1':
      return {0.34f, {{{0.02f, 0.2f}, {0.26f, 0}, {0.26f, 1}}}};
    case '2': {
      std::vector<Vec> s;
      Arc(s, {0.29f, 0.29f}, 0.29f, 200, 395);
      s.push_back({0, 1});
      s.push_back({0.6f, 1});
      return {0.6f, {s}};
    }
    case '3': {
      std::vector<Vec> s;
      Arc(s, {0.27f, 0.25f}, 0.25f, 205, 450);
      Arc(s, {0.27f, 0.75f}, 0.25f, 270, 520);
      return {0.54f, {s}};
    }
    default:
      return {0.5f, {}};
  }
}

// `text` centred on `c`, `height` high, in strokes `stroke` wide (pixels).
void Label(Canvas& cv, Color ink, std::string_view text, Vec c, float height, float stroke) {
  constexpr float kGap = 0.28f;
  float width = 0;
  for (size_t i = 0; i < text.size(); ++i) width += GlyphOf(text[i]).width + (i ? kGap : 0);
  float x = c.x - width * height / 2;
  const float top = c.y - height / 2;
  for (const char ch : text) {
    const Glyph g = GlyphOf(ch);
    for (const auto& stroke_pts : g.strokes) {
      std::vector<Vec> pts;
      for (const Vec& p : stroke_pts) pts.push_back({x + p.x * height, top + p.y * height});
      cv.Stroke(ink, pts, stroke);
    }
    x += (g.width + kGap) * height;
  }
}

// --- Bases ---

struct Palette {
  Color rim, face, ink;
};
Palette PaletteOf(PromptStyle style) {
  if (style == PromptStyle::kPlayStation) return {Rgb(16, 16, 20), Rgb(43, 43, 51), Rgb(235, 235, 240)};
  return {Rgb(22, 22, 26), Rgb(58, 58, 66), Rgb(245, 245, 248)};
}

// A round button: a dark rim, its face, a soft light on its top half.
void RoundBase(Canvas& cv, const Box& b, const Palette& pal) {
  const float s = b.s();
  const Vec c = b.c();
  const float r = 0.46f * s, r2 = r - 7 * s / 256;
  cv.Fill(pal.rim, b, [&](Vec p) { return Len(p - c) - r; });
  cv.Fill(pal.face, b, [&](Vec p) { return Len(p - c) - r2; });
  const Vec hc{c.x, c.y - 0.485f * r2};
  const float rx = 0.8f * r2, ry = 0.435f * r2;
  cv.Fill(Rgb(255, 255, 255, 26), b, [&](Vec p) {
    const Vec q{(p.x - hc.x) / rx, (p.y - hc.y) / ry};
    return (Len(q) - 1.0f) * std::min(rx, ry);
  });
}

// A rounded box (a pill when `radius` is half its height): rim, face and a soft light on its top.
void BoxBase(Canvas& cv, const Box& b, Vec half, float radius, const Palette& pal) {
  const Vec c = b.c();
  const float m = std::max(1.0f, 0.06f * 2 * std::min(half.x, half.y));
  cv.Fill(pal.rim, b, [&](Vec p) { return RoundBox(p, c, half, radius); });
  const Vec inner{half.x - m, half.y - m};
  const float r_in = std::max(0.0f, radius - m);
  cv.Fill(pal.face, b, [&](Vec p) { return RoundBox(p, c, inner, r_in); });
  const Vec lc{c.x, c.y - inner.y * 0.5f};
  const Vec lhalf{inner.x * 0.85f, inner.y * 0.42f};
  cv.Fill(Rgb(255, 255, 255, 22), b, [&](Vec p) { return RoundBox(p, lc, lhalf, std::min(lhalf.x, lhalf.y)); });
}

// The pill of Options / Share and the shoulder buttons: across a wide box, else the shape of a square icon's.
Vec PillHalf(const Box& b) {
  if (b.w >= 1.2f * b.h) return {b.w / 2 * 0.95f, b.h / 2 * 0.88f};
  return {0.43f * b.s(), 0.26f * b.s()};
}

// --- The icons ---

void PlayStationFace(Canvas& cv, PromptButton button, const Box& b) {
  const Palette pal = PaletteOf(PromptStyle::kPlayStation);
  RoundBase(cv, b, pal);
  const float s = b.s(), w = 22 * s / 256;
  const Vec c = b.c();
  switch (button) {
    case PromptButton::kA: {  // cross
      const float d = 0.17f * s;
      const Color col = Rgb(124, 178, 236);
      cv.Stroke(col, {{c.x - d, c.y - d}, {c.x + d, c.y + d}}, w);
      cv.Stroke(col, {{c.x + d, c.y - d}, {c.x - d, c.y + d}}, w);
      break;
    }
    case PromptButton::kB: {  // circle
      const float r = 0.19f * s - w / 2;
      cv.Fill(Rgb(240, 112, 116), b, [&](Vec p) { return std::abs(Len(p - c) - r) - w / 2; });
      break;
    }
    case PromptButton::kX: {  // square
      const float h = 0.17f * s - w / 2;
      cv.Fill(Rgb(232, 146, 206), b, [&](Vec p) { return std::abs(RoundBox(p, c, {h, h}, 2 * s / 256)) - w / 2; });
      break;
    }
    case PromptButton::kY: {  // triangle
      const float r = 0.25f * s, cy = c.y + 0.03f * s;
      const Vec a{c.x, cy - r}, bb{c.x + 0.866f * r, cy + r / 2}, d{c.x - 0.866f * r, cy + r / 2};
      cv.Stroke(Rgb(80, 206, 162), {a, bb, d, a}, 0.85f * w);
      break;
    }
    default:
      break;
  }
}

// Nintendo's letters sit by position: the bottom button is B, the right one A, the left Y, the top X.
void NintendoFace(Canvas& cv, PromptButton button, const Box& b) {
  const Palette pal = PaletteOf(PromptStyle::kNintendo);
  RoundBase(cv, b, pal);
  const float s = b.s(), k = s / 256, w = 21 * k, h = 0.36f * s;
  const Vec c = b.c();
  const float top = c.y - h / 2, bot = c.y + h / 2;
  const Color ink = pal.ink;
  switch (button) {
    case PromptButton::kA: {  // "B"
      const float hb = 0.44f * s, wb = 0.8f * w;
      const float tb = c.y - hb / 2, bb = c.y + hb / 2, x0 = c.x - 0.27f * hb, mid = c.y - 0.01f * hb;
      struct Bowl {
        float y0, y1, bulge;  // its top and bottom stroke, how far right it reaches (of the letter's height)
      };
      for (const Bowl& bowl : {Bowl{tb, mid, 0.40f}, Bowl{mid, bb, 0.47f}}) {
        const float r = (bowl.y1 - bowl.y0) / 2, xr = x0 + bowl.bulge * hb - r;
        cv.Stroke(ink, {{x0, bowl.y0}, {xr, bowl.y0}}, wb);
        cv.Stroke(ink, {{x0, bowl.y1}, {xr, bowl.y1}}, wb);
        const Vec cc{xr, (bowl.y0 + bowl.y1) / 2};
        cv.Fill(ink, b, [&](Vec p) { return std::max(std::abs(Len(p - cc) - r) - wb / 2, xr - p.x); });
      }
      cv.Stroke(ink, {{x0, tb}, {x0, bb}}, wb);
      break;
    }
    case PromptButton::kB: {  // "A"
      cv.Stroke(ink, {{c.x - 0.42f * h, bot}, {c.x, top}, {c.x + 0.42f * h, bot}}, w);
      cv.Stroke(ink, {{c.x - 0.24f * h, c.y + 0.14f * h}, {c.x + 0.24f * h, c.y + 0.14f * h}}, w);
      break;
    }
    case PromptButton::kX: {  // "Y"
      const float d = 0.42f * h;
      cv.Stroke(ink, {{c.x - d, top}, {c.x, c.y}, {c.x + d, top}}, w);
      cv.Stroke(ink, {{c.x, c.y}, {c.x, bot}}, w);
      break;
    }
    case PromptButton::kY: {  // "X"
      const float d = 0.40f * h;
      cv.Stroke(ink, {{c.x - d, top}, {c.x + d, bot}}, w);
      cv.Stroke(ink, {{c.x + d, top}, {c.x - d, bot}}, w);
      break;
    }
    default:
      break;
  }
}

// Start and Back: PlayStation's Options (three lines) and Share (an arrow up) on a pill; Nintendo's + and - on a round
// button.
void StartBack(Canvas& cv, PromptStyle style, PromptButton button, const Box& b) {
  const Palette pal = PaletteOf(style);
  const Vec c = b.c();
  if (style == PromptStyle::kPlayStation) {
    const Vec half = PillHalf(b);
    BoxBase(cv, b, half, half.y, pal);
    const float u = 2 * half.y / 0.52f;  // the size of the square icon this pill would belong to
    if (button == PromptButton::kStart) {
      for (const float dy : {-0.09f, 0.0f, 0.09f}) {
        cv.Stroke(pal.ink, {{c.x - 0.13f * u, c.y + dy * u}, {c.x + 0.13f * u, c.y + dy * u}}, 12 * u / 256);
      }
    } else {
      cv.Stroke(pal.ink, {{c.x, c.y + 0.10f * u}, {c.x, c.y - 0.14f * u}}, 11 * u / 256);
      cv.Stroke(pal.ink, {{c.x - 0.07f * u, c.y - 0.07f * u}, {c.x, c.y - 0.14f * u}, {c.x + 0.07f * u, c.y - 0.07f * u}},
                11 * u / 256);
    }
    return;
  }
  RoundBase(cv, b, pal);
  const float s = b.s(), w = 21 * s / 256, d = 0.42f * 0.36f * s;
  cv.Stroke(pal.ink, {{c.x - d, c.y}, {c.x + d, c.y}}, w);
  if (button == PromptButton::kStart) cv.Stroke(pal.ink, {{c.x, c.y - d}, {c.x, c.y + d}}, w);
}

// The shoulders (L1 / R1, L / R) on a pill, the triggers (L2 / R2, ZL / ZR) on a rounded box.
void Shoulder(Canvas& cv, PromptStyle style, PromptButton button, const Box& b) {
  const Palette pal = PaletteOf(style);
  const bool ps = style == PromptStyle::kPlayStation;
  const bool trigger = button == PromptButton::kLT || button == PromptButton::kRT;
  const bool left = button == PromptButton::kLB || button == PromptButton::kLT;
  const char* text = trigger ? (ps ? (left ? "L2" : "R2") : (left ? "ZL" : "ZR"))
                             : (ps ? (left ? "L1" : "R1") : (left ? "L" : "R"));
  Vec half;
  float radius;
  if (trigger) {
    half = b.w >= 1.2f * b.h ? Vec{b.w / 2 * 0.95f, b.h / 2 * 0.88f} : Vec{b.w / 2 * 0.9f, b.h / 2 * 0.92f};
    radius = 0.35f * std::min(half.x, half.y);
  } else {
    half = PillHalf(b);
    radius = half.y;
  }
  BoxBase(cv, b, half, radius, pal);
  const float height = std::min(2 * half.y * 0.5f, 2 * half.x * 0.42f);
  Label(cv, pal.ink, text, b.c(), height, std::max(1.0f, 0.17f * height));
}

// The sticks pressed (L3 / R3, or L / R in a ring like a stick's top for Nintendo).
void Stick(Canvas& cv, PromptStyle style, PromptButton button, const Box& b) {
  const Palette pal = PaletteOf(style);
  RoundBase(cv, b, pal);
  const float s = b.s();
  const Vec c = b.c();
  const bool left = button == PromptButton::kLS;
  if (style == PromptStyle::kNintendo) {
    const float r = 0.30f * s, w = 10 * s / 256;
    cv.Fill(pal.ink, b, [&](Vec p) { return std::abs(Len(p - c) - r) - w / 2; });
    Label(cv, pal.ink, left ? "L" : "R", c, 0.3f * s, 0.05f * s);
  } else {
    Label(cv, pal.ink, left ? "L3" : "R3", c, 0.3f * s, 0.05f * s);
  }
}

// The d-pad: a cross on a round button.
void Dpad(Canvas& cv, PromptStyle style, const Box& b) {
  const Palette pal = PaletteOf(style);
  RoundBase(cv, b, pal);
  const float s = b.s();
  const Vec c = b.c();
  const float arm = 0.30f * s, thick = 0.095f * s, r = 0.03f * s;
  const Color light = style == PromptStyle::kPlayStation ? Rgb(190, 190, 200) : Rgb(205, 205, 212);
  cv.Fill(light, b, [&](Vec p) {
    return std::min(RoundBox(p, c, {arm, thick}, r), RoundBox(p, c, {thick, arm}, r));
  });
  cv.Fill(pal.face, b, [&](Vec p) { return Len(p - c) - 0.045f * s; });
}

void DrawButton(Canvas& cv, PromptStyle style, PromptButton button, const Box& b) {
  switch (button) {
    case PromptButton::kA:
    case PromptButton::kB:
    case PromptButton::kX:
    case PromptButton::kY:
      if (style == PromptStyle::kPlayStation) {
        PlayStationFace(cv, button, b);
      } else {
        NintendoFace(cv, button, b);
      }
      return;
    case PromptButton::kStart:
    case PromptButton::kBack:
      StartBack(cv, style, button, b);
      return;
    case PromptButton::kLB:
    case PromptButton::kRB:
    case PromptButton::kLT:
    case PromptButton::kRT:
      Shoulder(cv, style, button, b);
      return;
    case PromptButton::kLS:
    case PromptButton::kRS:
      Stick(cv, style, button, b);
      return;
    case PromptButton::kDpad:
      Dpad(cv, style, b);
      return;
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

// The next mip level of 4-byte texels (any byte order with alpha last), averaged with the colours weighted by their
// alpha (no dark fringes); an odd side drops its last texel, as D3D sizes mips.
std::vector<uint8_t> HalfSize(const std::vector<uint8_t>& level, int w, int h, int& nw, int& nh) {
  nw = std::max(1, w / 2);
  nh = std::max(1, h / 2);
  std::vector<uint8_t> next(size_t(nw) * nh * 4);
  for (int y = 0; y < nh; ++y) {
    for (int x = 0; x < nw; ++x) {
      float sum[4] = {0, 0, 0, 0};
      for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
          const int sx = std::min(2 * x + dx, w - 1), sy = std::min(2 * y + dy, h - 1);
          const uint8_t* p = &level[(size_t(sy) * w + sx) * 4];
          const float a = p[3] / 255.0f;
          for (int k = 0; k < 3; ++k) sum[k] += p[k] * a;
          sum[3] += a;
        }
      }
      uint8_t* q = &next[(size_t(y) * nw + x) * 4];
      for (int k = 0; k < 3; ++k) q[k] = uint8_t(sum[3] > 0 ? std::lround(sum[k] / sum[3]) : 0);
      q[3] = uint8_t(std::lround(sum[3] / 4 * 255));
    }
  }
  return next;
}

}  // namespace

std::vector<uint8_t> DrawPromptIcon(PromptStyle style, PromptButton button, int size, bool flip) {
  if (size < 4 || size > 4096) return {};
  Canvas cv(size, size, flip);
  DrawButton(cv, style, button, Box{0, 0, float(size), float(size)});
  return cv.Bytes(false);
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
    int nw, nh;
    level = HalfSize(level, s, s, nw, nh);
  }
  return out;
}

std::vector<uint8_t> PatchButtonAtlas(const uint8_t* bgra, int width, int height, std::span<const AtlasCell> cells,
                                      PromptStyle style, int scale) {
  if (!bgra || width < 1 || height < 1 || width > 4096 || height > 4096 || scale < 1 || scale > 8) return {};
  const int w = width * scale, h = height * scale;
  Canvas cv(w, h, false);
  cv.LoadScaled(bgra, width, height, scale);
  for (const AtlasCell& cell : cells) {
    // Out with the game's button and two texels around it: its soft edge, and the faint shadow line some keep a
    // texel apart (Swap Force's RT). The atlases' other symbols sit further away.
    cv.Clear(Box{float((cell.x - 2) * scale), float((cell.y - 2) * scale), float((cell.w + 4) * scale),
                 float((cell.h + 4) * scale)});
    DrawButton(cv, style, cell.button,
               Box{float(cell.x * scale), float(cell.y * scale), float(cell.w * scale), float(cell.h * scale)});
  }
  int mips = 1;
  for (int s = std::max(w, h); s > 1; s /= 2) ++mips;
  std::vector<uint8_t> out = {'D', 'D', 'S', ' '};
  Put32(out, 124);
  Put32(out, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000);  // caps, height, width, pitch, pixel format, mips
  Put32(out, uint32_t(h));
  Put32(out, uint32_t(w));
  Put32(out, uint32_t(w * 4));
  Put32(out, 0);
  Put32(out, uint32_t(mips));
  out.resize(out.size() + 44, 0);
  Put32(out, 32);          // pixel format
  Put32(out, 0x40 | 0x1);  // RGB, alpha
  Put32(out, 0);
  Put32(out, 32);
  Put32(out, 0x00FF0000);
  Put32(out, 0x0000FF00);
  Put32(out, 0x000000FF);
  Put32(out, 0xFF000000);
  Put32(out, 0x8 | 0x1000 | 0x400000);  // complex, texture, mipmap
  out.resize(out.size() + 16, 0);
  std::vector<uint8_t> level = cv.Bytes(true);
  int lw = w, lh = h;
  for (int m = 0; m < mips; ++m) {
    out.insert(out.end(), level.begin(), level.end());
    if (m + 1 < mips) {
      int nw, nh;
      level = HalfSize(level, lw, lh, nw, nh);
      lw = nw;
      lh = nh;
    }
  }
  return out;
}

}  // namespace skylanders::portal
