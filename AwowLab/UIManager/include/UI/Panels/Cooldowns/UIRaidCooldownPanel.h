#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

// Forward declarations
class SpellCastDatabase;
class AuraDatabase;
class SpellIconLoader;
class ActorColorGenerator;
struct SpellCastEvent;

namespace spell_assignment {
    class SpellAssignmentSettings;
    enum class SpellDisplayLocation : uint16_t;
}

// Panel showing raid-wide cooldowns for all players
// Displays spells assigned to CDGrouping1/2/3 for tracking major raid CDs
// Each row shows: Player Name | Spell Icons with cooldown timers
class UIRaidCooldownPanel {
public:
    UIRaidCooldownPanel();

    // Render the panel
    // Returns true if window is open
    bool render(uint32_t currentTimeMs);

    // Set data sources
    void setSpellCastDatabase(const SpellCastDatabase* db) { spellCastDb_ = db; }
    void setAuraDatabase(const AuraDatabase* db) { auraDb_ = db; }
    void setSpellIconLoader(SpellIconLoader* loader) { iconLoader_ = loader; }
    void setColorGenerator(const ActorColorGenerator* colorGen) { colorGenerator_ = colorGen; }
    void setGuidToNameMap(const std::unordered_map<std::string_view, std::string_view>* map) { guidToName_ = map; }

    // Window visibility
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    void toggleVisible() { visible_ = !visible_; }
    bool* getVisiblePtr() { return &visible_; }

    // Settings
    float getIconSize() const { return iconSize_; }
    void setIconSize(float size) { iconSize_ = size; }

    // Which CD grouping to display (1, 2, or 3)
    int getGrouping() const { return grouping_; }
    void setGrouping(int grouping) { grouping_ = grouping; }

private:
    const SpellCastDatabase* spellCastDb_ = nullptr;
    const AuraDatabase* auraDb_ = nullptr;
    SpellIconLoader* iconLoader_ = nullptr;
    const ActorColorGenerator* colorGenerator_ = nullptr;
    const std::unordered_map<std::string_view, std::string_view>* guidToName_ = nullptr;

    bool visible_ = false;
    float iconSize_ = 32.0f;
    float iconSpacing_ = 2.0f;
    int grouping_ = 1;  // Which CDGrouping to display (1, 2, or 3)

    // Cached player data for rendering
    struct PlayerCooldownInfo {
        std::string guid;
        std::string name;
        uint16_t specId;
        std::vector<uint32_t> spellIds;  // Spells assigned to this grouping for this spec
    };
    std::vector<PlayerCooldownInfo> playerCooldowns_;

    // Cache invalidation
    mutable bool cacheValid_ = false;
    uint32_t lastSeenSettingsVersion_ = 0;  // Track SpellAssignmentSettings version

    // Rebuild the player cooldown cache
    void rebuildCache();

    // Render a single spell icon with cooldown state
    void renderSpellIcon(const std::string& playerGuid, uint32_t spellId,
                         uint32_t currentTimeMs, float size);

    // Get the SpellDisplayLocation for current grouping
    spell_assignment::SpellDisplayLocation getGroupingLocation() const;
};
