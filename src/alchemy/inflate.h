#pragma once

// Raw deflate (no zlib header), the second compression the later games' .pak files use.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace skylanders::alchemy {

// Decodes exactly `out_size` bytes, or nullopt for a stream that is corrupt or comes out shorter.
std::optional<std::vector<uint8_t>> InflateRaw(const uint8_t* data, size_t size, size_t out_size);

}  // namespace skylanders::alchemy
