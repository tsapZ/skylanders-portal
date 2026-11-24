#pragma once

// Picking the fallback TTF files whose contents ImGui can load, shared by the menu fonts
// (menu_draw.cpp) and its unit test. No ImGui dependency: the caller passes these paths to
// ImFontAtlas::AddFontFromFileTTF.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

namespace skylanders::menu_draw {

// True when `path` is a regular file ImGui can load: larger than the 100-byte minimum its
// AddFontFromMemoryTTF asserts on, and starting with an sfnt or TrueType collection header. A short
// or non-font file beside the exe would otherwise trip that assert before the next candidate is
// tried.
inline bool IsLoadableFontFile(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) return false;
  const std::uintmax_t size = std::filesystem::file_size(path, ec);
  if (ec || size <= 100) return false;
  std::ifstream in(path, std::ios::binary);
  unsigned char magic[4] = {};
  if (!in.read(reinterpret_cast<char*>(magic), sizeof(magic))) return false;
  const std::uint32_t tag = (std::uint32_t(magic[0]) << 24) | (std::uint32_t(magic[1]) << 16) |
                            (std::uint32_t(magic[2]) << 8) | std::uint32_t(magic[3]);
  return tag == 0x00010000u || tag == 0x74727565u /* 'true' */ ||
         tag == 0x74746366u /* 'ttcf' */ || tag == 0x4F54544Fu /* 'OTTO' */;
}

// The existing fallback TTFs from `candidates`, in order, probing exe-adjacent relative paths first.
inline std::vector<std::string> FindFallbackFontPaths(std::initializer_list<const char*> candidates) {
  std::filesystem::path exe_dir;
#if defined(__linux__)
  std::error_code sym_ec;
  const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", sym_ec);
  if (!sym_ec && !exe.empty()) exe_dir = exe.parent_path();
#endif
  std::vector<std::string> paths;
  for (const char* candidate : candidates) {
    if (!candidate || !candidate[0]) continue;
    const std::filesystem::path p(candidate);
    if (p.is_relative() && !exe_dir.empty()) {
      const std::filesystem::path next_to_exe = exe_dir / p;
      if (IsLoadableFontFile(next_to_exe)) paths.push_back(next_to_exe.string());
    }
    if (IsLoadableFontFile(p)) paths.push_back(p.string());
  }
  return paths;
}

}  // namespace skylanders::menu_draw
