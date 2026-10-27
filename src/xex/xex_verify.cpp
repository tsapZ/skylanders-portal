#include "xex/xex_verify.h"

#include <crypto/sha256.h>

#include <cctype>
#include <fstream>
#include <vector>

namespace skylanders::xex {

namespace {

constexpr size_t kChunkBytes = 64 * 1024;

std::optional<std::string> NormalizeHash(std::string_view s) {
  size_t begin = 0;
  size_t end = s.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
  s = s.substr(begin, end - begin);
  if (s.size() != 64) return std::nullopt;
  std::string out;
  out.reserve(64);
  for (char c : s) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

}  // namespace

std::optional<std::string> Sha256File(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;

  sha256::SHA256 hasher;
  std::vector<char> buf(kChunkBytes);
  while (in) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    const std::streamsize n = in.gcount();
    if (n > 0) hasher.add(buf.data(), static_cast<size_t>(n));
  }
  if (in.bad()) return std::nullopt;

  return hasher.getHash();
}

XexCheckResult VerifyXex(const std::filesystem::path& xex, std::string_view expected_list) {
  // One hash per supported disc (docs/game/discs.md), comma separated; the file is read once.
  std::vector<std::string> expected;
  for (size_t start = 0;;) {
    const size_t comma = expected_list.find(',', start);
    const auto one = NormalizeHash(expected_list.substr(
        start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
    if (!one) return {XexCheck::BadExpected, {}};
    expected.push_back(*one);
    if (comma == std::string_view::npos) break;
    start = comma + 1;
  }
  const auto actual = Sha256File(xex);
  if (!actual) return {XexCheck::Unreadable, {}};
  for (const auto& hash : expected) {
    if (*actual == hash) return {XexCheck::Match, *actual};
  }
  return {XexCheck::Mismatch, *actual};
}

std::string Utf8(const std::filesystem::path& p) {
  const auto u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::string DescribeXexProblem(const std::filesystem::path& xex, const XexCheckResult& result,
                               std::string_view expected_sha256, std::string_view game) {
  switch (result.status) {
    case XexCheck::Match:
      return {};
    case XexCheck::Unreadable:
      return "Cannot read " + Utf8(xex) +
             "\nPut your extracted " + std::string(game) +
             " disc contents in the game folder (default.xex must be at its top level).";
    case XexCheck::Mismatch:
      return "default.xex is not a supported build.\nExpected SHA-256" +
             std::string(expected_sha256.find(',') == std::string_view::npos ? ": " : " (one of): ") +
             std::string(expected_sha256) +
             "\nFound SHA-256:    " + result.actual_sha256;
    case XexCheck::BadExpected:
      break;
  }
  return "Internal error: the pinned SHA-256 is malformed.";
}

}  // namespace skylanders::xex
