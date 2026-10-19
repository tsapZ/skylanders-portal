#include "portal/portal_mode.h"

#include <cctype>
#include <string>

namespace skylanders::portal {

std::optional<PortalMode> ParsePortalMode(std::string_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
  std::string word;
  for (char c : text.substr(begin, end - begin)) {
    word.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  if (word == "none") return PortalMode::kNone;
  if (word == "software") return PortalMode::kSoftware;
  if (word == "usb") return PortalMode::kUsb;
  if (word == "hybrid") return PortalMode::kHybrid;
  return std::nullopt;
}

}  // namespace skylanders::portal
