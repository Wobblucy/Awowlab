#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Forward declarations
class SpellCastDatabase;
class AuraDatabase;
class SpellIconLoader;
struct SpellCastEvent;

namespace spell_assignment {
    class SpellAssignmentSettings;
    enum class SpellDisplayLocation : uint16_t;
}

class UICooldownPanel {
public:
    UICooldownPanel();

    // Render the cooldown panel
    // Returns true if window is open
    bool render(uint32_t currentTimeMs);

    // Set data sources
    void setSpellCastDatabase(const SpellCastDatabase* db) { spellCastDb_ = db; }
    void setAuraDatabase(const AuraDatabase* db) { auraDb_ = db; }
    void setSpellIconLoader(SpellIconLoader* loader) { iconLoader_ = loader; }
    void setSelectedPlayerGuid(const std::string& guid) { selectedPlayerGuid_ = guid; }
    void setSelectedPlayerSpecId(uint16_t specId) { selectedPlayerSpecId_ = specId; }

    // Window visibility
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    void toggleVisible() { visible_ = !visible_; }

    // Settings
    float getIconSize() const { return iconSize_; }
    void setIconSize(float size) { iconSize_ = size; }

private:
    const SpellCastDatabase* spellCastDb_ = nullptr;
    const AuraDatabase* auraDb_ = nullptr;
    SpellIconLoader* iconLoader_ = nullptr;
    std::string selectedPlayerGuid_;
    uint16_t selectedPlayerSpecId_ = 0;

    bool visible_ = false;
    float iconSize_ = 40.0f;
    float iconSpacing_ = 4.0f;

    // Get spells to display based on SpellAssignmentSettings
    std::vector<uint32_t> getSpellsToDisplay() const;

    // Render a single spell icon with cooldown state
    void renderSpellIcon(uint32_t spellId, uint32_t currentTimeMs, float size);
};
