#pragma once

// Writing and reading a picture as a PNG file (png_file.cpp, Windows' WIC), for the figure picture
// cache.

#include <cstdint>
#include <filesystem>
#include <vector>

namespace skylanders {

// `rgba`: width * height * 4 bytes, rows top to bottom. Writes to a temporary file and renames it,
// so a reader never sees half a file. False if it couldn't (or not on Windows).
bool WritePng(const std::filesystem::path& path, int width, int height, const uint8_t* rgba);

// The picture in a PNG (or any image WIC reads) as `rgba`, width * height * 4 bytes, rows top to
// bottom. False if it couldn't (or not on Windows).
bool ReadPng(const std::filesystem::path& path, int& width, int& height, std::vector<uint8_t>& rgba);

}  // namespace skylanders
