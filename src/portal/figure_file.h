#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

#include "portal/portal_device.h"

namespace skylanders::portal {

// Reads a raw figure dump: exactly kFigureSize (1024) bytes, 64 blocks of 16. Returns nullopt if the
// path is not a regular file or its size is anything else. The file is only read, never modified.
std::optional<FigureData> LoadFigureFile(const std::filesystem::path& path);

// Atomically writes `data` to `path`: writes to a temporary file next to it, then renames the
// temporary file over `path`. A crash or power loss mid-save leaves the original file untouched
// rather than a half-written one. Returns false, and leaves `path` unchanged, if either step fails
// (for example the parent directory does not exist); the temporary file is cleaned up either way.
bool SaveFigureFileAtomic(const std::filesystem::path& path, const FigureData& data);

// Backups of figure files, so a bad write or a mistake doesn't cost a figure's progress (level,
// gold, upgrades live in the file). The folder of `file`'s backups: Backups\Figures in the data
// folder, followed by the file's path under the data folder's Figures without ".dump" (Giants\Tree
// Rex), or Backups\Figures\Other\<name> for a file elsewhere. Outside Figures on purpose: the
// portal menu lists everything under Figures.
inline constexpr int kFigureBackups = 10;
std::filesystem::path FigureBackupDir(const std::filesystem::path& file, const std::filesystem::path& data_folder);
// Copies `file` as it is now into `backup_dir` as <name> (default: the local date and time,
// "2026-11-02 15-04-05.dump") and deletes the oldest copies there beyond `keep`. Returns the copy's
// path, or empty when `file` doesn't exist or the copy failed. Never throws.
std::filesystem::path BackUpFigureFile(const std::filesystem::path& file, const std::filesystem::path& backup_dir,
                                       int keep = kFigureBackups, std::string_view name = {});

// Reads the Skylander id / variant a figure's bytes encode: little-endian uint16 at a fixed
// offset (0x10 for id, 0x1C for variant). Never fails -- any 1024-byte figure has these bytes.
uint16_t ReadFigureId(const FigureData& data);
uint16_t ReadFigureVariant(const FigureData& data);

// Builds a blank, valid figure for `id`/`variant`: correct manufacturer bytes, id, variant, CRC,
// and Mifare sector-trailer access bits (see docs/architecture.md, "Figures"). The 4-byte serial is normally
// random; the explicit-serial overload exists so callers (tests) can get a deterministic result.
FigureData CreateBlankFigure(uint16_t id, uint16_t variant);
FigureData CreateBlankFigure(uint16_t id, uint16_t variant, std::array<uint8_t, 4> serial);

// Returns a path under `dir` for `name` that does not currently exist: "<name>.dump", or
// "<name> (2).dump", "<name> (3).dump", ... on collision. Never creates `dir`, the returned path,
// or anything else -- it only picks a name.
std::filesystem::path UniqueFigurePath(const std::filesystem::path& dir, std::string_view name);

}  // namespace skylanders::portal
