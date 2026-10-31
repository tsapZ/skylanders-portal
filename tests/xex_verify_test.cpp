#include "xex/xex_verify.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "xex/xex_header.h"

namespace fs = std::filesystem;
using skylanders::xex::VerifyXex;
using skylanders::xex::XexCheck;

static int g_failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

static const char kAbc[] = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
static const char kEmpty[] = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
static const char kMillionA[] = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";

static fs::path WriteTemp(const char* name, const std::string& bytes) {
  fs::path p = fs::temp_directory_path() / name;
  std::ofstream(p, std::ios::binary) << bytes;
  return p;
}

static void PutBe32(std::string& s, size_t at, uint32_t v) {
  s[at] = char(v >> 24);
  s[at + 1] = char(v >> 16);
  s[at + 2] = char(v >> 8);
  s[at + 3] = char(v);
}

// A small XEX2 header: the execution info and the security info with three page descriptors,
// read-only data (1 page), code (2 pages), data (1 page), 64 KB pages loaded at 0x82000000.
// `media` and `rdata_digest` stand for what differs between pressings, `code_digest` for the code.
static std::string FakeXex(uint32_t media, char rdata_digest, char code_digest) {
  std::string s(0x400, '\0');
  s.replace(0, 4, "XEX2");
  PutBe32(s, 0x08, 0x400);  // header size
  PutBe32(s, 0x10, 0x100);  // security info
  PutBe32(s, 0x14, 1);      // one optional header
  PutBe32(s, 0x18, 0x00040006);
  PutBe32(s, 0x1C, 0x40);   // the execution info
  PutBe32(s, 0x40, media);
  PutBe32(s, 0x44, 0x14);        // version 0.0.0.20
  PutBe32(s, 0x4C, 0x41560911);  // title
  s[0x52] = 1;                   // disc 1/1
  s[0x53] = 1;
  PutBe32(s, 0x100 + 0x110, 0x82000000);
  PutBe32(s, 0x100 + 0x178, 0x00FF0000);
  PutBe32(s, 0x100 + 0x180, 3);
  const size_t table = 0x100 + 0x184;
  PutBe32(s, table, (1 << 4) | 3);
  s.replace(table + 4, 20, 20, rdata_digest);
  PutBe32(s, table + 24, (2 << 4) | 1);
  s.replace(table + 28, 20, 20, code_digest);
  PutBe32(s, table + 48, (1 << 4) | 2);
  s.replace(table + 52, 20, 20, 'd');
  return s + "the image";
}

int main() {
  // Known SHA-256 vectors.
  CHECK(skylanders::xex::Sha256File(WriteTemp("gr_abc.bin", "abc")) == std::string(kAbc));
  CHECK(skylanders::xex::Sha256File(WriteTemp("gr_empty.bin", "")) == std::string(kEmpty));
  // 1,000,000 bytes spans many read chunks.
  CHECK(skylanders::xex::Sha256File(WriteTemp("gr_million.bin", std::string(1000000, 'a'))) ==
        std::string(kMillionA));

  // Unreadable inputs are nullopt, never a hash.
  CHECK(!skylanders::xex::Sha256File(fs::temp_directory_path() / "gr_does_not_exist.bin"));
  CHECK(!skylanders::xex::Sha256File(fs::temp_directory_path()));  // a directory

  // Paths with spaces and non-ASCII characters.
  fs::path odd = fs::temp_directory_path() / u8"gr odd éè dir";
  fs::create_directories(odd);
  {
    std::ofstream(odd / u8"défault.xex", std::ios::binary) << "abc";
  }
  CHECK(skylanders::xex::Sha256File(odd / u8"défault.xex") == std::string(kAbc));

  // VerifyXex: match, case and whitespace tolerant.
  fs::path abc = WriteTemp("gr_abc2.bin", "abc");
  CHECK(VerifyXex(abc, kAbc).status == XexCheck::Match);
  CHECK(VerifyXex(abc, std::string("  ") +
                           "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD" + "\n")
            .status == XexCheck::Match);

  // Mismatch reports the actual hash; an empty or truncated file is a mismatch, not a match.
  auto wrong = VerifyXex(abc, kEmpty);
  CHECK(wrong.status == XexCheck::Mismatch);
  CHECK(wrong.actual_sha256 == std::string(kAbc));
  CHECK(VerifyXex(WriteTemp("gr_empty2.bin", ""), kAbc).status == XexCheck::Mismatch);
  CHECK(VerifyXex(WriteTemp("gr_trunc.bin", "ab"), kAbc).status == XexCheck::Mismatch);

  // Missing file and bad expected hash.
  CHECK(VerifyXex(fs::temp_directory_path() / "gr_missing.xex", kAbc).status ==
        XexCheck::Unreadable);
  CHECK(VerifyXex(abc, "xyz").status == XexCheck::BadExpected);
  CHECK(VerifyXex(abc, "").status == XexCheck::BadExpected);
  CHECK(VerifyXex(abc, std::string(64, 'g')).status == XexCheck::BadExpected);

  // Several supported discs: a comma-separated list matches any of its hashes, in either place;
  // none matching is a mismatch with the actual hash; one malformed entry spoils the list.
  CHECK(VerifyXex(abc, std::string(kEmpty) + "," + kAbc).status == XexCheck::Match);
  CHECK(VerifyXex(abc, std::string(kAbc) + ", " + kEmpty).status == XexCheck::Match);
  auto none = VerifyXex(abc, std::string(kEmpty) + "," + kMillionA);
  CHECK(none.status == XexCheck::Mismatch);
  CHECK(none.actual_sha256 == std::string(kAbc));
  CHECK(VerifyXex(abc, std::string(kAbc) + ",xyz").status == XexCheck::BadExpected);
  CHECK(VerifyXex(abc, std::string(kAbc) + ",").status == XexCheck::BadExpected);

  // Wide-only characters (not in the ANSI code page): must not throw and must survive the round trip.
  {
    fs::path wide = fs::temp_directory_path() / L"gr 日本 dir";
    fs::create_directories(wide);
    {
      std::ofstream(wide / L"default.xex", std::ios::binary) << "abc";
    }
    CHECK(skylanders::xex::Sha256File(wide / L"default.xex") == std::string(kAbc));
    CHECK(skylanders::xex::Utf8(fs::path(L"日")) == std::string("\xE6\x97\xA5"));

    auto unreadable = VerifyXex(wide / L"missing.xex", kAbc);
    std::string msg = skylanders::xex::DescribeXexProblem(wide / L"missing.xex", unreadable, kAbc,
                                                          "Skylanders: Test");
    CHECK(msg.find(skylanders::xex::Utf8(wide / L"missing.xex")) != std::string::npos);
    CHECK(msg.find("Cannot read") != std::string::npos);
  }

  // DescribeXexProblem: empty for a match; both hashes for a mismatch; a message for a bad pin.
  {
    fs::path f = WriteTemp("gr_desc.bin", "abc");
    CHECK(skylanders::xex::DescribeXexProblem(f, VerifyXex(f, kAbc), kAbc, "Skylanders: Test").empty());
    std::string mm =
        skylanders::xex::DescribeXexProblem(f, VerifyXex(f, kEmpty), kEmpty, "Skylanders: Test");
    CHECK(mm.find(std::string(kEmpty, kEmpty + 64)) != std::string::npos);
    CHECK(mm.find(std::string(kAbc, kAbc + 64)) != std::string::npos);
    CHECK(!skylanders::xex::DescribeXexProblem(f, VerifyXex(f, "xyz"), "xyz", "Skylanders: Test").empty());
  }

  // The header and the code pin: two pressings (other media ID, other read-only data in front of
  // the code) have the same pin; other code doesn't.
  {
    const std::string a = FakeXex(0x7A74A451, 'a', 'c');
    const auto h = skylanders::xex::ParseXexHeader(reinterpret_cast<const uint8_t*>(a.data()), a.size());
    CHECK(h.has_value());
    if (h) {
      CHECK(h->title_id == 0x41560911);
      CHECK(h->code_address == 0x82010000);
      CHECK(h->code_pages_size == 48);
      CHECK(skylanders::xex::DescribeXexHeader(*h) ==
            "title 41560911, version 0.0.0.20, Europe and Australia (00FF0000), media 7A74A451, disc 1/1");
    }
    CHECK(!skylanders::xex::ParseXexHeader(reinterpret_cast<const uint8_t*>(a.data()), 0x200));  // cut short
    CHECK(!skylanders::xex::ParseXexHeader(reinterpret_cast<const uint8_t*>("abc"), 3));
    CHECK(skylanders::xex::XexRegionName(0xFFFFFFFF) == "all regions");
    CHECK(skylanders::xex::XexRegionName(0x000000FF) == "North America");

    const fs::path fa = WriteTemp("gr_xex_a.xex", a);
    const fs::path fb = WriteTemp("gr_xex_b.xex", FakeXex(0x12345678, 'b', 'c'));
    const fs::path fc = WriteTemp("gr_xex_c.xex", FakeXex(0x7A74A451, 'a', 'x'));
    const auto pin = skylanders::xex::XexCodePin(fa);
    CHECK(pin.has_value() && pin->rfind("82010000:", 0) == 0 && pin->size() == 73);
    CHECK(skylanders::xex::XexCodePin(fb) == pin);
    CHECK(skylanders::xex::XexCodePin(fc) != pin);
    CHECK(!skylanders::xex::XexCodePin(abc));  // no XEX2 header

    const std::string hash_a = *skylanders::xex::Sha256File(fa);
    const std::string list = hash_a + "," + *pin;
    CHECK(VerifyXex(fa, list).status == XexCheck::Match);
    const auto same = VerifyXex(fb, list);
    CHECK(same.status == XexCheck::SameCode);
    CHECK(same.code_pin == *pin);
    CHECK(same.header.find("media 12345678") != std::string::npos);
    CHECK(skylanders::xex::DescribeXexProblem(fb, same, list, "Skylanders: Test").empty());
    // A pin's hash in upper case and spaces around it are fine.
    std::string upper = *pin;
    for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
    CHECK(VerifyXex(fb, hash_a + " , " + upper + " ").status == XexCheck::SameCode);
    // Other code: a mismatch that names what was found, and lists the hashes without the pins.
    const auto other = VerifyXex(fc, list);
    CHECK(other.status == XexCheck::Mismatch);
    const std::string msg = skylanders::xex::DescribeXexProblem(fc, other, list, "Skylanders: Test");
    CHECK(msg.find("Found: title 41560911") != std::string::npos);
    CHECK(msg.find("Expected SHA-256: " + hash_a + "\n") != std::string::npos);
    CHECK(msg.find(*pin) == std::string::npos);
    // Without the pin the other pressing is a mismatch; a malformed pin spoils the list.
    CHECK(VerifyXex(fb, hash_a).status == XexCheck::Mismatch);
    CHECK(VerifyXex(fb, hash_a + ",82010000:abc").status == XexCheck::BadExpected);
    CHECK(VerifyXex(fb, hash_a + ",8201000:" + std::string(64, 'a')).status == XexCheck::BadExpected);
  }

  if (g_failures == 0) std::puts("all xex_verify tests passed");
  return g_failures == 0 ? 0 : 1;
}
