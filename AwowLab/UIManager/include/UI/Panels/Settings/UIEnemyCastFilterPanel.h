#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

class SpellIconLoader;
class BossTimerStrip;
class UIUpcomingBossCasts;
struct BossTimerSettings;
struct HostileCreatureSpells;

// Grouping of spells by hostile creature (for filter panel display)
struct FilterableCreatureSpells {
    std::string creature_name;
    std::unordered_set<uint32_t> cast_ids;
    std::unordered_set<uint32_t> damage_ids;
    std::unordered_set<uint32_t> debuff_ids;
};

// Settings panel for Enemy Cast Filters
// Allows users to configure which enemy abilities appear on the boss timer
class UIEnemyCastFilterPanel {
public:
    UIEnemyCastFilterPanel();
    ~UIEnemyCastFilterPanel();

    // Set dependencies
    void setSpellIconLoader(SpellIconLoader* loader) { iconLoader_ = loader; }
    void setSettings(BossTimerSettings* settings) { settings_ = settings; }
    void setBossTimerStrip(BossTimerStrip* strip) { timerStrip_ = strip; }
    void setUpcomingBossCastsPanel(UIUpcomingBossCasts* panel) { upcomingPanel_ = panel; }

    // Load creature spell data from pre-built HostileCreatureSpells
    // (same data as Auto Annotations panel)
    void setHostileCreatureSpells(const std::vector<HostileCreatureSpells>& creatureSpells);

    // Render the panel
    void render();

    // Visibility control
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    void toggleVisible() { visible_ = !visible_; }

    // Navigate to a specific spell (called from context menu)
    // Finds the creature with this spell and selects it
    void navigateToSpell(uint32_t spellId);

private:
    bool visible_ = false;
    SpellIconLoader* iconLoader_ = nullptr;
    BossTimerSettings* settings_ = nullptr;
    BossTimerStrip* timerStrip_ = nullptr;
    UIUpcomingBossCasts* upcomingPanel_ = nullptr;

    // Creature spell data
    std::vector<FilterableCreatureSpells> creatureSpells_;

    // UI state
    char searchBuffer_[256] = {0};
    int selectedCreatureIndex_ = -1;

    // Render sections
    void renderCreatureList();
    void renderSpellList();
    void renderTimerSettings();

    // Render a single spell row with checkbox
    void renderSpellRow(uint32_t spellId, const char* category, bool isTypeEnabled);
};
