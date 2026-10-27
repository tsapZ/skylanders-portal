#pragma once

// Xbox 360 (Xenos) DXT textures to RGBA, for the games' own pictures (UI sprites, portraits,
// hats, upgrade poses), as Swap Force's IGZ texture reader (igz_file.cpp) decodes them.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace skylanders::alchemy {

enum class Dxt { kDxt1, kDxt3, kDxt5 };

struct RgbaImage {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;  // width * height * 4
};

// Decodes `width` x `height` texels of DXT data stored as the GPU reads it: 16-bit words
// big-endian, tiled (or linear, rows padded to 32 blocks). nullopt if `size` is too small.
std::optional<RgbaImage> DecodeXenosDxt(const uint8_t* data, size_t size, int width, int height,
                                        Dxt kind, bool tiled);

// Turns the picture upside down. Giants and Trap Team store their model textures (upgrade poses,
// HUD portraits, element symbols) bottom row first; their UI sprites are the right way up.
void FlipVertically(RgbaImage& image);

}  // namespace skylanders::alchemy
