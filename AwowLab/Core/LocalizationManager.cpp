#include "LocalizationManager.h"
#include "ErrorLogger.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

// Static locale data

namespace {

struct LocaleInfo {
    const char* code;           // "en_US"
    const char* displayName;    // Native language name
    const char* englishName;    // English name
};

constexpr LocaleInfo LOCALE_INFO[] = {
    {"en_US", "English (US)", "English (US)"},
    {"es_MX", "Español (México)", "Spanish (Mexico)"},
    {"pt_BR", "Português (Brasil)", "Portuguese (Brazil)"},
    {"de_DE", "Deutsch", "German"},
    {"fr_FR", "Français", "French"},
    {"ru_RU", "Русский", "Russian"},
    {"ko_KR", "한국어", "Korean"},
    {"zh_TW", "繁體中文", "Chinese (Traditional)"},
    {"zh_CN", "简体中文", "Chinese (Simplified)"},
};

static_assert(std::size(LOCALE_INFO) == static_cast<size_t>(Locale::COUNT),
              "LOCALE_INFO must have entries for all locales");

} // anonymous namespace

// Singleton instance

LocalizationManager& LocalizationManager::instance() {
    static LocalizationManager instance;
    return instance;
}

// Translation loading

bool LocalizationManager::loadTranslations(const std::string& langDir) {
    langDir_ = langDir;
    translations_.clear();
    loaded_ = false;

    // Must load en_US as the fallback locale
    std::string enUsPath = langDir + "/en_US.csv";
    if (!loadLocaleFile(enUsPath, Locale::en_US)) {
        // Don't log - Application.cpp logs a summary warning about localization
        return false;
    }

    // Load other locales (optional - missing translations will fall back to en_US)
    for (size_t i = 1; i < static_cast<size_t>(Locale::COUNT); ++i) {
        Locale loc = static_cast<Locale>(i);
        std::string path = langDir + "/" + getLocaleCode(loc) + ".csv";

        if (std::filesystem::exists(path)) {
            if (!loadLocaleFile(path, loc)) {
                // Parse failure for non-primary locale - just use fallback silently
            }
        }
    }

    loaded_ = true;
    return true;
}

bool LocalizationManager::reloadCurrentLocale() {
    if (langDir_.empty()) {
        return false;
    }

    // Reload en_US first (fallback)
    std::string enUsPath = langDir_ + "/en_US.csv";
    if (!loadLocaleFile(enUsPath, Locale::en_US)) {
        return false;
    }

    // Reload current locale if different from en_US
    if (currentLocale_ != Locale::en_US) {
        std::string path = langDir_ + "/" + getLocaleCode(currentLocale_) + ".csv";
        if (std::filesystem::exists(path)) {
            loadLocaleFile(path, currentLocale_);
        }
    }

    return true;
}

bool LocalizationManager::loadTranslationsFromMemory(std::span<const LocaleData> locales) {
    // Memory loading replaces the disk directory entirely; clearing
    // langDir_ keeps setLocale()/reloadCurrentLocale() off the filesystem.
    langDir_.clear();
    translations_.clear();
    loaded_ = false;

    // en_US goes in first so every key carries its fallback text
    const LocaleData* fallback = nullptr;
    for (const auto& entry : locales) {
        if (entry.code == "en_US") {
            fallback = &entry;
            break;
        }
    }
    if (!fallback || !loadLocaleFromMemory(fallback->csv, Locale::en_US)) {
        return false;
    }

    for (const auto& entry : locales) {
        auto locale = parseLocale(entry.code);
        if (!locale || *locale == Locale::en_US) {
            continue;
        }
        loadLocaleFromMemory(entry.csv, *locale);
    }

    loaded_ = true;
    return true;
}

bool LocalizationManager::loadLocaleFile(const std::string& filepath, Locale locale) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    return loadLocaleFromStream(file, locale);
}

bool LocalizationManager::loadLocaleFromMemory(std::string_view csvText, Locale locale) {
    std::istringstream in{std::string(csvText)};
    return loadLocaleFromStream(in, locale);
}

bool LocalizationManager::loadLocaleFromStream(std::istream& file, Locale locale) {
    std::string line;
    int lineNum = 0;
    size_t localeIdx = static_cast<size_t>(locale);
    size_t rejected = 0;

    while (std::getline(file, line)) {
        ++lineNum;

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Skip BOM if present (first line only)
        if (lineNum == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line = line.substr(3);
        }

        // Remove carriage return if present (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Skip header row
        if (lineNum == 1 && line.find("key,") == 0) {
            continue;
        }

        // Parse key,value pair
        auto [key, value] = parseCSVLine(line);
        if (key.empty()) {
            continue;
        }

        // A translation whose printf conversions differ from the English
        // text would read the wrong arguments when formatted. Leave its slot
        // empty so lookups fall back to English.
        if (locale != Locale::en_US) {
            auto existing = translations_.find(key);
            if (existing != translations_.end() && !existing->second[0].empty() &&
                formatSignature(value) != formatSignature(existing->second[0])) {
                existing->second[localeIdx].clear();
                ++rejected;
                continue;
            }
        }

        // Store translation
        translations_[key][localeIdx] = std::move(value);
    }

    return true;
}

std::string LocalizationManager::formatSignature(std::string_view format) {
    std::string out;
    auto addToken = [&out](std::string_view token) {
        if (!out.empty()) out += ' ';
        out += token;
    };

    for (size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%') continue;
        size_t j = i + 1;
        if (j < format.size() && format[j] == '%') {
            i = j;  // Literal percent, no argument
            continue;
        }

        // Read the specifier the way printf does: flags, width, precision,
        // length, conversion. Text after a '%' is never prose to printf.
        while (j < format.size() &&
               (format[j] == '-' || format[j] == '+' || format[j] == ' ' ||
                format[j] == '#' || format[j] == '0')) {
            ++j;
        }
        if (j < format.size() && format[j] == '*') {
            addToken("i");
            ++j;
        } else {
            while (j < format.size() && format[j] >= '0' && format[j] <= '9') ++j;
        }
        if (j < format.size() && format[j] == '.') {
            ++j;
            if (j < format.size() && format[j] == '*') {
                addToken("i");
                ++j;
            } else {
                while (j < format.size() && format[j] >= '0' && format[j] <= '9') ++j;
            }
        }

        std::string length;
        auto takeLength = [&](std::string_view mod) {
            if (format.substr(j, mod.size()) == mod) {
                length = mod;
                j += mod.size();
                return true;
            }
            return false;
        };
        takeLength("hh") || takeLength("h") || takeLength("ll") || takeLength("l") ||
            takeLength("I64") || takeLength("I32") || takeLength("I") ||
            takeLength("j") || takeLength("z") || takeLength("t") ||
            takeLength("L") || takeLength("w");

        if (j >= format.size()) {
            addToken("!end");
            break;
        }

        const char conversion = format[j];
        char kind = 0;
        switch (conversion) {
            case 'd': case 'i': case 'o': case 'u': case 'x': case 'X': case 'c': case 'C':
                kind = 'i';
                break;
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
                kind = 'f';
                break;
            case 's': case 'S': case 'Z':
                kind = 's';
                break;
            case 'p':
                kind = 'p';
                break;
            case 'n':
                kind = 'n';
                break;
            default:
                break;
        }

        if (kind != 0) {
            addToken(length + kind);
        } else {
            addToken(std::string("!") + std::string(format.substr(i + 1, j - i)));
        }
        i = j;
    }
    return out;
}

std::pair<std::string, std::string> LocalizationManager::parseCSVLine(const std::string& line) const {
    std::string key;
    std::string value;

    // Find the first comma (key is never quoted)
    size_t commaPos = line.find(',');
    if (commaPos == std::string::npos) {
        return {key, value};
    }

    key = line.substr(0, commaPos);

    // Parse value (may be quoted)
    size_t valueStart = commaPos + 1;
    if (valueStart >= line.size()) {
        return {key, value};
    }

    if (line[valueStart] == '"') {
        // Quoted value - find closing quote
        // Handle escaped quotes ("") within the value
        std::string result;
        size_t pos = valueStart + 1;

        while (pos < line.size()) {
            if (line[pos] == '"') {
                // Check for escaped quote
                if (pos + 1 < line.size() && line[pos + 1] == '"') {
                    result += '"';
                    pos += 2;
                } else {
                    // End of quoted string
                    break;
                }
            } else {
                result += line[pos];
                ++pos;
            }
        }

        value = std::move(result);
    } else {
        // Unquoted value - take rest of line
        value = line.substr(valueStart);
    }

    return {key, value};
}

// Locale switching

void LocalizationManager::setLocale(Locale locale) {
    if (locale == currentLocale_) {
        return;
    }

    currentLocale_ = locale;

    // Reload current locale file to ensure we have latest translations
    if (!langDir_.empty() && locale != Locale::en_US) {
        std::string path = langDir_ + "/" + getLocaleCode(locale) + ".csv";
        if (std::filesystem::exists(path)) {
            loadLocaleFile(path, locale);
        }
    }

}

// Translation lookup

const char* LocalizationManager::get(std::string_view key) const {
    // Thread-safe: translations_ is only written during loadTranslations()
    // which happens before UI rendering starts

    auto it = translations_.find(std::string(key));
    if (it == translations_.end()) {
        // Key not found - show the key itself so the gap is visible on screen
        return stableUntranslated(key);
    }

    const auto& localeStrings = it->second;
    size_t localeIdx = static_cast<size_t>(currentLocale_);

    // Try current locale
    if (!localeStrings[localeIdx].empty()) {
        return localeStrings[localeIdx].c_str();
    }

    // Fall back to en_US
    if (!localeStrings[0].empty()) {
        return localeStrings[0].c_str();
    }

    // Key exists but every locale column is blank - show the key
    return stableUntranslated(key);
}

const char* LocalizationManager::stableUntranslated(std::string_view key) const {
    std::lock_guard<std::mutex> lock(untranslatedMutex_);
    // try_emplace leaves an existing entry alone, so repeat lookups of the
    // same key keep handing back the same pointer.
    auto [it, _] = untranslated_.try_emplace(std::string(key), key);
    return it->second.c_str();
}

bool LocalizationManager::hasKey(std::string_view key) const {
    return translations_.find(std::string(key)) != translations_.end();
}

// Static locale utilities

const char* LocalizationManager::getLocaleCode(Locale locale) {
    size_t idx = static_cast<size_t>(locale);
    if (idx < std::size(LOCALE_INFO)) {
        return LOCALE_INFO[idx].code;
    }
    return "en_US";
}

const char* LocalizationManager::getLocaleDisplayName(Locale locale) {
    size_t idx = static_cast<size_t>(locale);
    if (idx < std::size(LOCALE_INFO)) {
        return LOCALE_INFO[idx].displayName;
    }
    return "English (US)";
}

const char* LocalizationManager::getLocaleEnglishName(Locale locale) {
    size_t idx = static_cast<size_t>(locale);
    if (idx < std::size(LOCALE_INFO)) {
        return LOCALE_INFO[idx].englishName;
    }
    return "English (US)";
}

std::optional<Locale> LocalizationManager::parseLocale(std::string_view code) {
    for (size_t i = 0; i < std::size(LOCALE_INFO); ++i) {
        if (code == LOCALE_INFO[i].code) {
            return static_cast<Locale>(i);
        }
    }
    return std::nullopt;
}

