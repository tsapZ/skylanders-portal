#include "xex/xex_verify.h"

#include <crypto/sha256.h>

#include "xex/xex_header.h"

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

// A code pin ("82450000:" and a SHA-256), upper-case address, lower-case hash.
std::optional<std::string> NormalizePin(std::string_view s) {
  size_t begin = 0;
  size_t end = s.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
  s = s.substr(begin, end - begin);
  if (s.size() != 9 + 64 || s[8] != ':') return std::nullopt;
  std::string out;
  for (size_t i = 0; i < 8; ++i) {
    if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return std::nullopt;
    out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(s[i]))));
  }
  const auto hash = NormalizeHash(s.substr(9));
  if (!hash) return std::nullopt;
  return out + ":" + *hash;
}

// The SHA-256 entries of an expected list, without the code pins, comma separated.
std::string ExpectedHashes(std::string_view list, size_t& count) {
  std::string out;
  count = 0;
  for (size_t start = 0;;) {
    const size_t comma = list.find(',', start);
    const auto hash = NormalizeHash(list.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                                                      : comma - start));
    if (hash) out += (count++ ? "," : "") + *hash;
    if (comma == std::string_view::npos) break;
    start = comma + 1;
  }
  return out;
}

struct HeaderFacts {
  std::string header;
  std::string code_pin;
};

// The XEX2 header of the file (up to 1 MB of it), described, and its code pin.
std::optional<HeaderFacts> ReadHeaderFacts(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::vector<uint8_t> head(64 * 1024);
  in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
  head.resize(static_cast<size_t>(in.gcount()));
  if (head.size() >= 12) {
    const size_t header_size = detail::Be32(head.data() + 8);
    if (header_size > head.size() && header_size <= (1u << 20)) {
      const size_t have = head.size();
      head.resize(header_size);
      in.read(reinterpret_cast<char*>(head.data() + have), static_cast<std::streamsize>(header_size - have));
      head.resize(have + static_cast<size_t>(in.gcount()));
    }
  }
  const auto h = ParseXexHeader(head.data(), head.size());
  if (!h) return std::nullopt;
  HeaderFacts facts{DescribeXexHeader(*h), {}};
  if (h->code_pages_size) {
    sha256::SHA256 hasher;
    hasher.add(head.data() + h->code_pages_offset, h->code_pages_size);
    facts.code_pin = CodePin(*h, hasher.getHash());
  }
  return facts;
}

}  // namespace

std::optional<std::string> XexCodePin(const std::filesystem::path& path) {
  const auto facts = ReadHeaderFacts(path);
  if (!facts || facts->code_pin.empty()) return std::nullopt;
  return facts->code_pin;
}

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
  // One hash per supported disc (docs/game/discs.md) and the code pins, comma separated.
  std::vector<std::string> expected;
  std::vector<std::string> pins;
  for (size_t start = 0;;) {
    const size_t comma = expected_list.find(',', start);
    const auto entry =
        expected_list.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
    if (const auto hash = NormalizeHash(entry)) {
      expected.push_back(*hash);
    } else if (const auto pin = NormalizePin(entry)) {
      pins.push_back(*pin);
    } else {
      return {XexCheck::BadExpected, {}};
    }
    if (comma == std::string_view::npos) break;
    start = comma + 1;
  }
  const auto actual = Sha256File(xex);
  if (!actual) return {XexCheck::Unreadable, {}};
  XexCheckResult result{XexCheck::Mismatch, *actual};
  if (const auto facts = ReadHeaderFacts(xex)) {
    result.header = facts->header;
    result.code_pin = facts->code_pin;
  }
  for (const auto& hash : expected) {
    if (*actual == hash) result.status = XexCheck::Match;
  }
  if (result.status == XexCheck::Mismatch && !result.code_pin.empty()) {
    for (const auto& pin : pins) {
      if (result.code_pin == pin) result.status = XexCheck::SameCode;
    }
  }
  return result;
}

std::string Utf8(const std::filesystem::path& p) {
  const auto u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::string DescribeXexProblem(const std::filesystem::path& xex, const XexCheckResult& result,
                               std::string_view expected_sha256, std::string_view game) {
  switch (result.status) {
    case XexCheck::Match:
    case XexCheck::SameCode:
      return {};
    case XexCheck::Unreadable:
      // The path is the one the game reads (rom\default.xex in the game's folder): saying "the game
      // folder" alone had people put the disc next to the exe (seen on a fresh Windows, 2026-11-01).
      return "Cannot read " + Utf8(xex) +
             "\nThe extracted " + std::string(game) +
             " disc belongs in the rom folder in the game's folder, with default.xex directly in it."
             "\nRun the setup again and pick your disc, or set the game up in Core of Light.";
    case XexCheck::Mismatch: {
      size_t count = 0;
      const std::string hashes = ExpectedHashes(expected_sha256, count);
      std::string text = "default.xex is not a supported build.\nExpected SHA-256" +
                         std::string(count == 1 ? ": " : " (one of): ") + hashes +
                         "\nFound SHA-256:    " + result.actual_sha256;
      if (!result.header.empty()) {
        text += "\nFound: " + result.header + "\nIts code isn't the supported disc's, so this port can't run it.";
      }
      return text;
    }
    case XexCheck::BadExpected:
      break;
  }
  return "Internal error: the pinned SHA-256 is malformed.";
}

}  // namespace skylanders::xex
