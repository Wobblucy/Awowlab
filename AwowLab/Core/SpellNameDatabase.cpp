#include "SpellNameDatabase.h"
#include "LocalizationManager.h"
#include "ErrorLogger.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <charconv>
#include <filesystem>
#include <zlib.h>

SpellNameDatabase& SpellNameDatabase::instance() {
    static SpellNameDatabase instance;
    return instance;
}

void SpellNameDatabase::setDataDirectory(const std::string& dirPath) {
    dataDir_ = dirPath;
}

void SpellNameDatabase::setSpellNameLocale(Locale locale) {
    AWOW_LOG_INFO("SPELLDB", std::string("setSpellNameLocale called with ") + getLocaleCode(locale)
                  + ", current=" + getLocaleCode(currentLocale_));

    if (currentLocale_ == locale) {
        AWOW_LOG_INFO("SPELLDB", "No locale change needed");
        return;
    }

    if (!dataDir_.empty() && locale != Locale::en_US) {
        if (!hasLocaleFile(locale)) {
            AWOW_LOG_WARNING("SPELLDB", std::string("Locale file not found for ")
                      + getLocaleCode(locale) + ", falling back to en_US");
            locale = Locale::en_US;
        }
    }

    currentLocale_ = locale;
    clearCache();

    AWOW_LOG_INFO("SPELLDB", std::string("Switched to locale ") + getLocaleCode(locale));
}

bool SpellNameDatabase::hasLocaleFile(Locale locale) const {
    if (dataDir_.empty()) {
        return false;
    }

    std::string path = dataDir_;
    if (locale == Locale::en_US) {
        path += "/spell_names.csv.gz";
    } else {
        path += "/spell_names_";
        path += getLocaleCode(locale);
        path += ".csv.gz";
    }

    return std::filesystem::exists(path);
}

void SpellNameDatabase::clearCache() {
    spellNames_.clear();
    fallbackCache_.clear();
}

std::string SpellNameDatabase::getEffectiveDataPath() const {
    if (dataDir_.empty()) {
        return "";
    }

    std::string path = dataDir_;
    if (currentLocale_ == Locale::en_US) {
        path += "/spell_names.csv.gz";
    } else {
        path += "/spell_names_";
        path += getLocaleCode(currentLocale_);
        path += ".csv.gz";
    }

    if (!std::filesystem::exists(path)) {
        AWOW_LOG_INFO("SPELLDB", std::string("getEffectiveDataPath() locale file not found: ") + path
                  + ", falling back to English");
        path = dataDir_ + "/spell_names.csv.gz";
    } else {
        AWOW_LOG_INFO("SPELLDB", std::string("getEffectiveDataPath() using: ") + path);
    }

    return path;
}

const char* SpellNameDatabase::getLocaleCode(Locale locale) {
    switch (locale) {
        case Locale::en_US: return "en_US";
        case Locale::es_MX: return "es_MX";
        case Locale::pt_BR: return "pt_BR";
        case Locale::de_DE: return "de_DE";
        case Locale::fr_FR: return "fr_FR";
        case Locale::ru_RU: return "ru_RU";
        case Locale::ko_KR: return "ko_KR";
        case Locale::zh_TW: return "zh_TW";
        case Locale::zh_CN: return "zh_CN";
        default: return "en_US";
    }
}

size_t SpellNameDatabase::loadSpellIds(const std::unordered_set<uint32_t>& spellIds) {
    std::string effectivePath = getEffectiveDataPath();
    if (effectivePath.empty()) {
        return 0;
    }

    std::unordered_set<uint32_t> neededIds;
    for (uint32_t id : spellIds) {
        if (spellNames_.find(id) == spellNames_.end()) {
            neededIds.insert(id);
        }
    }

    if (neededIds.empty()) {
        return 0;
    }

    std::string decompressed;
    if (!decompressGzip(effectivePath, decompressed)) {
        AWOW_LOG_WARNING("SPELLDB", std::string("Failed to decompress ") + effectivePath);
        return 0;
    }
    AWOW_LOG_INFO("SPELLDB", std::string("Decompressed ") + std::to_string(decompressed.size()) + " bytes from " + effectivePath);

    size_t newCount = parseCSVForIds(decompressed, neededIds);

    return newCount;
}

bool SpellNameDatabase::decompressGzip(const std::string& filepath, std::string& output) const {
    gzFile file = gzopen(filepath.c_str(), "rb");
    if (!file) {
        return false;
    }

    constexpr size_t CHUNK_SIZE = 65536;
    std::vector<char> buffer(CHUNK_SIZE);
    output.clear();
    output.reserve(15 * 1024 * 1024);  // Roughly the size of one decompressed file

    int bytesRead;
    while ((bytesRead = gzread(file, buffer.data(), static_cast<unsigned>(CHUNK_SIZE))) > 0) {
        output.append(buffer.data(), bytesRead);
    }

    int err;
    gzerror(file, &err);
    gzclose(file);

    if (err != Z_OK && err != Z_STREAM_END) {
        AWOW_LOG_WARNING("DATA", "Error decompressing spell name database - file may be corrupted");
        return false;
    }

    return true;
}

size_t SpellNameDatabase::parseCSVForIds(const std::string& content, const std::unordered_set<uint32_t>& targetIds) {
    size_t newCount = 0;

    std::istringstream stream(content);
    std::string line;
    bool isHeader = true;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        if (isHeader) {
            isHeader = false;
            continue;
        }

        // spell_id,name
        size_t commaPos = line.find(',');
        if (commaPos == std::string::npos) continue;

        uint32_t spellId = 0;
        auto [ptr, ec] = std::from_chars(line.data(), line.data() + commaPos, spellId);
        if (ec != std::errc()) continue;

        if (targetIds.find(spellId) == targetIds.end()) {
            continue;
        }

        std::string name = line.substr(commaPos + 1);

        // A name containing commas is quoted, with "" for a literal quote
        if (!name.empty() && name.front() == '"') {
            if (name.size() >= 2 && name.back() == '"') {
                name = name.substr(1, name.size() - 2);
            }
            size_t pos = 0;
            while ((pos = name.find("\"\"", pos)) != std::string::npos) {
                name.replace(pos, 2, "\"");
                pos += 1;
            }
        }

        // Trailing \r from CRLF files, and stray spaces
        while (!name.empty() && (name.back() == '\r' || name.back() == '\n' || name.back() == ' ')) {
            name.pop_back();
        }

        if (!name.empty()) {
            auto [it, inserted] = spellNames_.emplace(spellId, std::move(name));
            if (inserted) {
                ++newCount;
            }
        }
    }

    return newCount;
}

const char* SpellNameDatabase::getSpellName(uint32_t spellId) const {
    auto it = spellNames_.find(spellId);
    if (it != spellNames_.end()) {
        return it->second.c_str();
    }

    auto cacheIt = fallbackCache_.find(spellId);
    if (cacheIt != fallbackCache_.end()) {
        return cacheIt->second.c_str();
    }

    std::string fallback = "Spell " + std::to_string(spellId);
    auto [insertIt, _] = fallbackCache_.emplace(spellId, std::move(fallback));
    return insertIt->second.c_str();
}

bool SpellNameDatabase::hasSpellName(uint32_t spellId) const {
    return spellNames_.find(spellId) != spellNames_.end();
}
