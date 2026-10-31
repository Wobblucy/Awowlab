// Translations are handed to printf as the format string (ImGui::Text(L(key),
// ...) and LF()), so a translation whose conversions differ from the English
// text reads the wrong arguments - a %s where the code passes an int is a
// crash. The loader drops such translations; these tests pin that, and check
// every shipped language file against en_US.

#include <catch2/catch_test_macros.hpp>

#include "Core/LocalizationManager.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Raw key -> value from a shipped CSV, read the same way the loader reads it
// but without the specifier check, so the test sees what is in the file.
std::map<std::string, std::string> readRawCsv(const std::string& path) {
    std::map<std::string, std::string> out;
    std::ifstream file(path, std::ios::binary);
    std::string line;
    bool first = true;
    while (std::getline(file, line)) {
        if (first && line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3);
        }
        first = false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t comma = line.find(',');
        if (comma == std::string::npos) continue;

        std::string key = line.substr(0, comma);
        std::string value;
        size_t pos = comma + 1;
        if (pos < line.size() && line[pos] == '"') {
            ++pos;
            while (pos < line.size()) {
                if (line[pos] == '"') {
                    if (pos + 1 < line.size() && line[pos + 1] == '"') {
                        value += '"';
                        pos += 2;
                        continue;
                    }
                    break;
                }
                value += line[pos++];
            }
        } else {
            value = line.substr(pos);
        }
        if (key == "key") continue;  // Header row
        out[key] = value;
    }
    return out;
}

// Translations in the shipped files whose specifiers do not match en_US. The
// loader shows English for these. Fix a translation and remove it here; a new
// mismatch fails the test by name.
//
// The English "200% crit" reads to printf as "% c" (space flag on %c), so
// every translation of raw_tooltip_crit2 differs. That key is only ever shown
// through "%s", so the cost is just the English line in place of the
// translated one.
const std::set<std::string> kKnownMismatches = {
    "de_DE:breakdown.raw_tooltip_crit2",
    "es_MX:breakdown.raw_tooltip_crit2",
    "fr_FR:breakdown.raw_tooltip_crit2",
    "ko_KR:breakdown.raw_tooltip_crit2",
    "pt_BR:breakdown.raw_tooltip_crit2",
    "ru_RU:breakdown.raw_tooltip_crit2",
    "zh_CN:breakdown.raw_tooltip_crit2",
    "zh_TW:breakdown.raw_tooltip_crit2",
};

} // namespace

TEST_CASE("formatSignature reads conversions the way printf does",
          "[localization][format]") {
    using LM = LocalizationManager;
    CHECK(LM::formatSignature("no conversions") == "");
    CHECK(LM::formatSignature("%s (%s DPS)") == "s s");
    CHECK(LM::formatSignature("%.0f%% of the time") == "f");
    CHECK(LM::formatSignature("%d and %s and %.1f") == "i s f");
    CHECK(LM::formatSignature("%zu of %zu") == "zi zi");
    CHECK(LM::formatSignature("%llu / %lu") == "lli li");
    CHECK(LM::formatSignature("%-8s|%*d") == "s i i");
    CHECK(LM::formatSignature("%u vs %d vs %x") == "i i i");
    // printf has no notion of prose: "% d" is the space flag on %d
    CHECK(LM::formatSignature("50% done") == "i");
    // A conversion printf does not know only matches itself
    CHECK(LM::formatSignature("%1$s") == "!1$");
    CHECK(LM::formatSignature("trailing %") == "!end");
}

TEST_CASE("translations with different specifiers fall back to English",
          "[localization][format]") {
    const std::string en =
        "key,value\n"
        "fmt.swapped,%s has %d stacks\n"
        "fmt.ok,%s has %d stacks\n"
        "fmt.missing,%s took %.1f%%\n"
        "fmt.wider,%d hits\n"
        "plain.text,Hello\n";
    const std::string de =
        "key,value\n"
        "fmt.swapped,%d Stapel hat %s\n"
        "fmt.ok,%s hat %d Stapel\n"
        "fmt.missing,%s hat genommen\n"
        "fmt.wider,%lld Treffer\n"
        "plain.text,Hallo\n";

    std::vector<LocalizationManager::LocaleData> locales = {
        {"en_US", en},
        {"de_DE", de},
    };
    auto& mgr = LocalizationManager::instance();
    REQUIRE(mgr.loadTranslationsFromMemory(locales));
    mgr.setLocale(Locale::de_DE);

    CHECK(std::string(mgr.get("fmt.swapped")) == "%s has %d stacks");
    CHECK(std::string(mgr.get("fmt.missing")) == "%s took %.1f%%");
    CHECK(std::string(mgr.get("fmt.wider")) == "%d hits");
    CHECK(std::string(mgr.get("fmt.ok")) == "%s hat %d Stapel");
    CHECK(std::string(mgr.get("plain.text")) == "Hallo");

    mgr.setLocale(Locale::en_US);
}

TEST_CASE("shipped translations use the same specifiers as en_US",
          "[localization][format][shipped]") {
    const auto english = readRawCsv(LANG_DIR "/en_US.csv");
    REQUIRE_FALSE(english.empty());

    auto& mgr = LocalizationManager::instance();
    REQUIRE(mgr.loadTranslations(LANG_DIR));

    std::vector<std::string> unexpected;
    std::set<std::string> seen;

    for (size_t i = 1; i < static_cast<size_t>(Locale::COUNT); ++i) {
        const Locale locale = static_cast<Locale>(i);
        const std::string code = LocalizationManager::getLocaleCode(locale);
        const auto translated = readRawCsv(std::string(LANG_DIR) + "/" + code + ".csv");

        mgr.setLocale(locale);
        for (const auto& [key, value] : translated) {
            const auto en = english.find(key);
            if (en == english.end() || en->second.empty()) continue;
            if (LocalizationManager::formatSignature(value) ==
                LocalizationManager::formatSignature(en->second)) {
                continue;
            }

            const std::string id = code + ":" + key;
            seen.insert(id);
            if (!kKnownMismatches.count(id)) {
                unexpected.push_back(id + "  en=\"" + en->second + "\"  " + code +
                                     "=\"" + value + "\"");
            }

            // Whatever the file says, the loader must not hand it to printf
            INFO(id);
            CHECK(std::string(mgr.get(key)) == en->second);
        }
    }
    mgr.setLocale(Locale::en_US);

    std::ostringstream report;
    for (const auto& line : unexpected) report << "\n  " << line;
    INFO("translations whose specifiers differ from en_US:" << report.str());
    CHECK(unexpected.empty());

    // A fixed translation should come off the known list
    std::vector<std::string> stale;
    for (const auto& id : kKnownMismatches) {
        if (!seen.count(id)) stale.push_back(id);
    }
    std::ostringstream staleReport;
    for (const auto& id : stale) staleReport << "\n  " << id;
    INFO("listed as mismatched but now match:" << staleReport.str());
    CHECK(stale.empty());
}
