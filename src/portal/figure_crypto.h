#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "portal/portal_device.h"

namespace skylanders::portal {

using FigureBlock = std::array<uint8_t, kBlockSize>;

// A figure's save data (blocks 8 and up, except each sector's trailer block) is AES-128 encrypted,
// each block with its own key: MD5 of the tag's first 0x20 bytes + the block index + a constant.
// The key only depends on blocks 0-1, which are never encrypted.
//
// nullopt when the crypto provider fails (a digest or cipher operation errored). A failed
// operation is never reported as a zero key or an all-zero block: callers must refuse the parse or
// the figure write instead of using the block.
std::optional<FigureBlock> DecryptFigureBlock(const FigureData& data, uint8_t block_index);
std::optional<FigureBlock> EncryptFigureBlock(const FigureData& data, uint8_t block_index,
                                              const FigureBlock& plain);

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF), the checksum figure save data uses.
uint16_t FigureCrc16(const uint8_t* data, size_t size);

}  // namespace skylanders::portal
