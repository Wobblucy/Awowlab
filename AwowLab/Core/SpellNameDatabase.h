#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

// Forward declare Locale enum to avoid circular dependency
enum class Locale : uint8_t;

// Localized spell names, read from data/spellnames on demand. Only the ids a
// loaded log needs are parsed out of the compressed CSV, and they stay cached
// until the locale changes.
//
//   auto& db = SpellNameDatabase::instance();
//   db.setDataDirectory("data/spellnames");
//   db.setSpellNameLocale(Locale::ko_KR);   // optional, en_US by default
//   db.loadSpellIds(idsSeenInTheLog);
//   const char* name = SPELL_NAME(12345);
class SpellNameDatabase {
public:
    static SpellNameDatabase& instance();

    SpellNameDatabase(const SpellNameDatabase&) = delete;
    SpellNameDatabase& operator=(const SpellNameDatabase&) = delete;
    SpellNameDatabase(SpellNameDatabase&&) = delete;
    SpellNameDatabase& operator=(SpellNameDatabase&&) = delete;

    // Holds spell_names.csv.gz (en_US) and spell_names_{locale}.csv.gz
    void setDataDirectory(const std::string& dirPath);

    // Clears the cache. A locale without a file falls back to en_US.
    void setSpellNameLocale(Locale locale);

    Locale getSpellNameLocale() const { return currentLocale_; }

    bool hasLocaleFile(Locale locale) const;

    // Parses the names of any ids not already cached. Returns how many were
    // added.
    size_t loadSpellIds(const std::unordered_set<uint32_t>& spellIds);

    // "Spell {id}" when the name is unknown
    const char* getSpellName(uint32_t spellId) const;

    bool hasSpellName(uint32_t spellId) const;

    size_t size() const { return spellNames_.size(); }

    void clearCache();

private:
    SpellNameDatabase() = default;

    // The current locale's file, or the en_US one if it is missing
    std::string getEffectiveDataPath() const;

    static const char* getLocaleCode(Locale locale);

    bool decompressGzip(const std::string& filepath, std::string& output) const;

    // Returns the number of names added
    size_t parseCSVForIds(const std::string& content, const std::unordered_set<uint32_t>& targetIds);

    std::string dataDir_;
    Locale currentLocale_{};
    std::unordered_map<uint32_t, std::string> spellNames_;

    // Backing storage for the "Spell 12345" strings getSpellName hands out
    mutable std::unordered_map<uint32_t, std::string> fallbackCache_;
};

// Usage: ImGui::Text("%s", SPELL_NAME(spell.spell_id));
#define SPELL_NAME(id) SpellNameDatabase::instance().getSpellName(id)
