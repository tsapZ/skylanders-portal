#include "alchemy/inflate.h"

// stb_image's zlib decoder (vendored with the SDK), compiled here with every function static, so
// it can't clash with another copy and nothing but the decoder is used.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include <stb_image.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace skylanders::alchemy {

std::optional<std::vector<uint8_t>> InflateRaw(const uint8_t* data, size_t size, size_t out_size) {
  std::vector<uint8_t> out(out_size);
  const int written =
      stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(out.data()), int(out.size()),
                                       reinterpret_cast<const char*>(data), int(size));
  if (written < 0 || size_t(written) != out_size) return std::nullopt;
  return out;
}

}  // namespace skylanders::alchemy
