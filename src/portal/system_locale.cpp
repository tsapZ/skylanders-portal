#include "portal/system_locale.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace skylanders {

namespace {

std::string AsciiLower(std::string_view in) {
  std::string out(in);
  for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return out;
}

std::string AsciiUpper(std::string_view in) {
  std::string out(in);
  for (char& ch : out) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return out;
}

}  // namespace

SystemLocale ParsePosixLocale(std::string_view value) {
  // Strip ".encoding" and "@modifier" suffixes first.
  const size_t dot = value.find('.');
  const size_t at = value.find('@');
  const size_t cut = std::min(dot, at);
  value = value.substr(0, cut);

  if (value.empty() || value == "C" || value == "POSIX") return {};

  const size_t sep = value.find_first_of("_-");
  std::string lang = AsciiLower(sep == std::string_view::npos ? value : value.substr(0, sep));
  if (lang.empty() || lang == "c" || lang == "posix") return {};
  if (lang == "nb" || lang == "nn") lang = "no";  // Bokmal and Nynorsk share Norwegian game text.

  std::string region;
  if (sep != std::string_view::npos) {
    std::string_view rest = value.substr(sep + 1);
    const size_t next_sep = rest.find_first_of("_-");
    region = AsciiUpper(next_sep == std::string_view::npos ? rest : rest.substr(0, next_sep));
  }
  return {std::move(lang), std::move(region)};
}

SystemLocale CurrentPosixLocale() {
  for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
    if (const char* val = std::getenv(var); val && *val) {
      if (SystemLocale parsed = ParsePosixLocale(val); !parsed.language.empty()) return parsed;
    }
  }
  return {};
}

std::string LanguageFromLocale(const SystemLocale& locale, bool split_spanish_latam) {
  if (locale.language == "de") return "german";
  if (locale.language == "fr") return "french";
  if (locale.language == "it") return "italian";
  if (locale.language == "nl") return "dutch";
  if (locale.language == "sv") return "swedish";
  if (locale.language == "no") return "norwegian";
  if (locale.language == "da") return "danish";
  if (locale.language == "fi") return "finnish";
  if (locale.language == "pt") return "portuguese";
  if (locale.language == "es") {
    if (split_spanish_latam && !locale.region.empty() && locale.region != "ES") {
      return "spanish_latam";
    }
    return "spanish";
  }
  return "english";
}

std::string PosixSystemLanguage(bool split_spanish_latam) {
  return LanguageFromLocale(CurrentPosixLocale(), split_spanish_latam);
}

}  // namespace skylanders
