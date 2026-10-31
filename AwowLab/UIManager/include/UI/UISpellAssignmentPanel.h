#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include "SpellAssignmentSettings.h"
#include "Parser/WowClass.h"

class SpellCastDatabase;
class AuraDatabase;
class ActorColorGenerator;
class SpellIconLoader;
struct RaidFrameConfig;
struct NameplateSettings;

// Panel for configuring which spells/auras appear in which UI locations
// Shows a table with spell rows and location checkboxes as columns
// Queries databases directly at render time and caches results
class UISpellAssignmentPanel {
public:
    UISpellAssignmentPanel();
    ~UISpellAssignmentPanel() = default;

    // Render the panel
    // Queries spellCastDb for cooldowns, auraDb for buffs/debuffs
    // Uses colorGenerator to determine spec from actor GUIDs
    // raidFrameConfig/nameplateSettings are optional - if provided, shows icon size sliders
    void render(
        const SpellCastDatabase* spellCastDb,
        const AuraDatabase* auraDb,
        const ActorColorGenerator* colorGenerator,
        const std::unordered_map<uint32_t, std::string>* spellNameMap,
        SpellIconLoader* iconLoader = nullptr,
        RaidFrameConfig* raidFrameConfig = nullptr,
        NameplateSettings* nameplateSettings = nullptr
    );

    // Visibility control
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    bool* getVisiblePtr() { return &visible_; }

    // Clear cache (call when encounter changes or databases are unloaded)
    void clearCache();

    // Set filter to highlight a specific spell ID (called from context menu navigation)
    void setFilterSpellId(uint32_t spellId);

    // Set the selected spec (called from context menu navigation)
    void setSelectedSpec(uint16_t specId);

private:
    bool visible_ = false;

    // Filter state
    enum class FilterMode { All, Cooldowns, Buffs, Debuffs, Encounter };
    FilterMode filterMode_ = FilterMode::All;

    // Selected spec for editing (spec ID, 0 = none selected)
    uint16_t selectedSpecId_ = 0;
    bool showAllSpecs_ = false;  // When true, shows aggregated spells from all specs

    // Search filter
    char searchBuffer_[128] = "";

    // Display locations to show as columns (in order)
    static constexpr spell_assignment::SpellDisplayLocation kLocations[] = {
        spell_assignment::SpellDisplayLocation::Nameplate,
        spell_assignment::SpellDisplayLocation::CDGrouping1,      // Raid CD (Major)
        spell_assignment::SpellDisplayLocation::PlayerHUDBigCD,   // HUD (Major)
        spell_assignment::SpellDisplayLocation::RaidFrameAura,    // Raid Frames
        spell_assignment::SpellDisplayLocation::GCDTracker,
        spell_assignment::SpellDisplayLocation::PlayerBuffs,      // Player Buffs panel
        spell_assignment::SpellDisplayLocation::PlayerDebuffs     // Player Debuffs panel
    };
    static constexpr size_t kNumLocations = sizeof(kLocations) / sizeof(kLocations[0]);

    // Cached data per spec (rebuilt when databases change)
    struct SpellEntry {
        uint32_t spellId;
        std::string spellName;  // Cached for display
    };
    struct AuraEntry {
        uint32_t spellId;
        std::string spellName;
        bool isBuff;
    };
    struct SpecCache {
        std::vector<SpellEntry> spells;
        std::vector<AuraEntry> auras;
    };
    std::unordered_map<uint16_t, SpecCache> specCache_;

    // Track database pointers to detect changes
    const SpellCastDatabase* lastSpellCastDb_ = nullptr;
    const AuraDatabase* lastAuraDb_ = nullptr;

    // Cached row data (rebuilt when filters/spec/search change)
    struct SpellRow {
        uint32_t spellId;
        std::string name;
        bool isAura;       // false = cooldown, true = buff/debuff
        bool isBuff;       // only valid if isAura
        uint16_t locations; // current location mask
        uint16_t primarySpecId; // First spec that has this spell
        bool hasNonPlayerSource = false;  // true if any NPC/boss applies this aura
    };
    std::vector<SpellRow> cachedRows_;

    // Row cache invalidation tracking
    bool rowCacheValid_ = false;
    FilterMode lastFilterMode_ = FilterMode::All;
    uint16_t lastSelectedSpecId_ = 0;
    bool lastShowAllSpecs_ = false;
    std::string lastSearchBuffer_;

    // Cached search buffer (lowercase, computed once per search change)
    std::string cachedLowerSearch_;

    // Cached counts for "All Specs" mode
    size_t cachedUniqueSpellCount_ = 0;
    size_t cachedUniqueBuffCount_ = 0;
    size_t cachedUniqueDebuffCount_ = 0;

    // Reverse lookup: spellId -> list of specs (built during rebuildCache)
    std::unordered_map<uint32_t, std::vector<uint16_t>> spellToSpecs_;
    std::unordered_map<uint32_t, std::vector<uint16_t>> auraToSpecs_;

    // Track which auras have non-player sources (encounter-specific auras)
    std::unordered_set<uint32_t> aurasWithNonPlayerSource_;

    // Helper methods
    void rebuildCache(
        const SpellCastDatabase* spellCastDb,
        const AuraDatabase* auraDb,
        const ActorColorGenerator* colorGenerator,
        const std::unordered_map<uint32_t, std::string>* spellNameMap
    );
    void renderSpecSelector();
    void renderFilterTabs();
    void renderSpellTable(SpellIconLoader* iconLoader);

    // Rebuild the cached row data (called when filters/spec/search change)
    void rebuildRowCache();

    // Whether an aura row survives the current filter tab
    bool auraPassesFilter(bool isBuff, bool hasNonPlayerSource) const;

    // Specs a checkbox on this row writes to: every spec that has the spell
    // in All Specs mode, otherwise just the selected one
    std::vector<uint16_t> specsToEdit(const SpellRow& row) const;

    // Get spell name (SpellNameDatabase, then spellNameMap, then the ID)
    std::string getSpellName(
        uint32_t spellId,
        const std::unordered_map<uint32_t, std::string>* spellNameMap
    ) const;

    // Check if spell matches search filter
    bool matchesSearch(const std::string& spellName, uint32_t spellId) const;

    // Get all specs that have a specific aura in their cache
    std::vector<uint16_t> getSpecsWithAura(uint32_t spellId) const;

    // Get all specs that have a specific spell (cooldown) in their cache
    std::vector<uint16_t> getSpecsWithSpell(uint32_t spellId) const;
};
