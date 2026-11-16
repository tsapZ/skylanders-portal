#include "portal/system_locale.h"

#include "test_util.h"

namespace {

void TestParsing() {
  const auto c_loc = skylanders::ParsePosixLocale("C.UTF-8");
  CHECK(c_loc.language.empty());
  CHECK(skylanders::ParsePosixLocale("POSIX").language.empty());

  const auto de = skylanders::ParsePosixLocale("de_DE.UTF-8");
  CHECK(de.language == "de");
  CHECK(de.region == "DE");
  CHECK(skylanders::LanguageFromLocale(de) == "german");

  const auto es_es = skylanders::ParsePosixLocale("es_ES.UTF-8@euro");
  CHECK(skylanders::LanguageFromLocale(es_es, true) == "spanish");

  const auto es_mx = skylanders::ParsePosixLocale("es_MX.UTF-8");
  CHECK(skylanders::LanguageFromLocale(es_mx, true) == "spanish_latam");
  CHECK(skylanders::LanguageFromLocale(es_mx, false) == "spanish");

  CHECK(skylanders::LanguageFromLocale(skylanders::ParsePosixLocale("nb_NO.UTF-8")) == "norwegian");
  CHECK(skylanders::LanguageFromLocale(skylanders::ParsePosixLocale("nn_NO")) == "norwegian");
  CHECK(skylanders::LanguageFromLocale(skylanders::ParsePosixLocale("nl_NL.UTF-8")) == "dutch");
  CHECK(skylanders::LanguageFromLocale(skylanders::ParsePosixLocale("pt_BR.UTF-8")) == "portuguese");
  CHECK(skylanders::LanguageFromLocale(skylanders::ParsePosixLocale("sv_SE.UTF-8")) == "swedish");
  CHECK(skylanders::LanguageFromLocale(skylanders::ParsePosixLocale("da_DK.UTF-8")) == "danish");
  CHECK(skylanders::LanguageFromLocale(skylanders::ParsePosixLocale("fi_FI.UTF-8")) == "finnish");
  CHECK(skylanders::LanguageFromLocale(skylanders::ParsePosixLocale("ja_JP.UTF-8")) == "english");
}

}  // namespace

int main() {
  TestParsing();
  return g_failures == 0 ? 0 : 1;
}
