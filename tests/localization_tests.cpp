// Tests for LocalizationManager's in-memory CSV loading matching the file
// loader byte for byte.

#include <catch2/catch_test_macros.hpp>

#include "Core/LocalizationManager.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Memory vs file CSV loading equivalence

namespace {

// A CSV exercising everything loadLocaleFile handles: BOM, header row,
// comments, blank lines, quoted values, escaped quotes, embedded commas.
const std::string kEnUsCsv =
    "\xEF\xBB\xBFkey,value\n"
    "# section comment\n"
    "\n"
    "menu.file,File\n"
    "menu.quoted,\"Hello, world\"\n"
    "menu.escaped,\"She said \"\"hi\"\"\"\n"
    "format.total_dps,%s (%s DPS)\n"
    "menu.plain_tail,Rest of line, unquoted\n";

const std::string kDeDeCsv =
    "key,value\r\n"
    "menu.file,Datei\r\n"
    "menu.quoted,\"Hallo, Welt\"\r\n";

const std::vector<std::string> kProbeKeys = {
    "menu.file", "menu.quoted", "menu.escaped",
    "format.total_dps", "menu.plain_tail",
};

// Capture what the manager currently returns for every probe key in both
// locales, so two loading paths can be compared.
std::map<std::string, std::string> snapshotTranslations() {
    auto& mgr = LocalizationManager::instance();
    std::map<std::string, std::string> got;
    for (Locale loc : {Locale::en_US, Locale::de_DE}) {
        mgr.setLocale(loc);
        for (const auto& key : kProbeKeys) {
            got[std::string(LocalizationManager::getLocaleCode(loc)) + ":" + key] =
                mgr.get(key);
        }
    }
    mgr.setLocale(Locale::en_US);
    return got;
}

} // anonymous namespace

TEST_CASE("memory loading matches file loading byte for byte", "[localization]") {
    fs::path langDir = fs::temp_directory_path() / "awowlab_locale_test_lang";
    fs::remove_all(langDir);
    fs::create_directories(langDir);
    {
        std::ofstream en(langDir / "en_US.csv", std::ios::binary);
        en << kEnUsCsv;
        std::ofstream de(langDir / "de_DE.csv", std::ios::binary);
        de << kDeDeCsv;
    }

    auto& mgr = LocalizationManager::instance();

    REQUIRE(mgr.loadTranslations(langDir.string()));
    auto fromFiles = snapshotTranslations();

    std::vector<LocalizationManager::LocaleData> locales = {
        {"en_US", kEnUsCsv},
        {"de_DE", kDeDeCsv},
    };
    REQUIRE(mgr.loadTranslationsFromMemory(locales));
    auto fromMemory = snapshotTranslations();

    CHECK(fromFiles == fromMemory);

    // Spot-check the parsed values themselves so both paths failing the
    // same way can't slip through
    CHECK(fromMemory.at("en_US:menu.quoted") == "Hello, world");
    CHECK(fromMemory.at("en_US:menu.escaped") == "She said \"hi\"");
    CHECK(fromMemory.at("en_US:menu.plain_tail") == "Rest of line, unquoted");
    CHECK(fromMemory.at("de_DE:menu.file") == "Datei");
    // de_DE has no entry for this key; it must fall back to en_US
    CHECK(fromMemory.at("de_DE:menu.escaped") == "She said \"hi\"");

    fs::remove_all(langDir);
}

TEST_CASE("memory loading requires the en_US fallback", "[localization]") {
    auto& mgr = LocalizationManager::instance();

    std::vector<LocalizationManager::LocaleData> noFallback = {
        {"de_DE", kDeDeCsv},
    };
    CHECK_FALSE(mgr.loadTranslationsFromMemory(noFallback));

    // Unknown locale codes are skipped without failing the load
    std::vector<LocalizationManager::LocaleData> withUnknown = {
        {"en_US", kEnUsCsv},
        {"xx_XX", kDeDeCsv},
    };
    CHECK(mgr.loadTranslationsFromMemory(withUnknown));
    CHECK(std::string(mgr.get("menu.file")) == "File");
}

TEST_CASE("missing keys keep independent backing storage", "[localization]") {
    auto& mgr = LocalizationManager::instance();

    std::vector<LocalizationManager::LocaleData> locales = {
        {"en_US", kEnUsCsv},
    };
    REQUIRE(mgr.loadTranslationsFromMemory(locales));

    // A combo box collects several labels before it draws any of them, so
    // the pointers from one lookup have to survive the next. A shared
    // buffer would leave all three aimed at the last key looked up.
    const char* items[] = {
        mgr.get("wizard.source.filter_any"),
        mgr.get("wizard.source.filter_creature"),
        mgr.get("wizard.source.filter_environment"),
    };

    CHECK(std::string(items[0]) == "wizard.source.filter_any");
    CHECK(std::string(items[1]) == "wizard.source.filter_creature");
    CHECK(std::string(items[2]) == "wizard.source.filter_environment");

    CHECK(items[0] != items[1]);
    CHECK(items[1] != items[2]);

    // Looking the same key up twice should hand back the same allocation
    CHECK(mgr.get("wizard.source.filter_any") == items[0]);

    // A key that exists but has no text anywhere falls back the same way,
    // and must not alias a neighbouring miss either
    const char* blank = mgr.get("menu.blank_everywhere");
    const char* other = mgr.get("menu.also_absent");
    CHECK(blank != other);
    CHECK(std::string(blank) == "menu.blank_everywhere");
}
