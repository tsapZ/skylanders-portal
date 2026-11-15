#pragma once

#include <string>
#include <string_view>

namespace skylanders {

struct SystemLocale {
  std::string language;  // lower-case ISO 639 code, e.g. "de", "nb" -> "no"
  std::string region;    // upper-case region code if present, e.g. "ES", "MX", "419"
};

// Parses a POSIX locale string such as "de_DE.UTF-8" or "es_419@latin" into language + region.
// Returns empty fields for "C", "POSIX", or unparseable values.
SystemLocale ParsePosixLocale(std::string_view value);

// Reads LC_ALL, LC_MESSAGES, and LANG in priority order.
SystemLocale CurrentPosixLocale();

// Canonical game language name ("english", "german", "french", "spanish", "spanish_latam",
// "italian", "dutch", "swedish", "norwegian", "danish", "finnish", "portuguese") for a parsed
// locale, falling back to "english".
std::string LanguageFromLocale(const SystemLocale& locale, bool split_spanish_latam = false);

// Convenience wrapper calling CurrentPosixLocale() -> LanguageFromLocale().
std::string PosixSystemLanguage(bool split_spanish_latam = false);

}  // namespace skylanders
