#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include "../../Database/EventTypes.h"

class DeathDatabase;
class PlaybackController;
class UIActorSelector;
class CombatDatabase;
class ResourceDatabase;
class SpellIconLoader;
struct TargetCombatEvent;
struct ResourceEvent;

// Panel showing player deaths with click-to-snap to timeline
// Clicking a death: jumps to 4s before, focuses the player, plays at 50% speed
// Also provides a popup detail view showing health timeline and damage/healing events
class UIDeathsPanel {
public:
    UIDeathsPanel();
    ~UIDeathsPanel() = default;

    void render(
        const DeathDatabase* deathDb,
        std::shared_ptr<PlaybackController> playback,
        UIActorSelector* actorSelector = nullptr,
        const CombatDatabase* combatDb = nullptr,
        const ResourceDatabase* resourceDb = nullptr,
        const std::unordered_map<uint32_t, std::string>* spellNameMap = nullptr,
        SpellIconLoader* iconLoader = nullptr
    );

    // Visibility control
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    bool* getVisiblePtr() { return &visible_; }

private:
    bool visible_ = true;

    // Death breakout popup state
    bool showBreakoutPopup_ = false;
    // A copy, not a pointer: the death list is rebuilt when another
    // encounter loads, and the popup can outlive that.
    std::optional<DeathEvent> selectedDeath_;
    std::vector<TargetCombatEvent> cachedEvents_;
    std::vector<ResourceEvent> cachedHealthHistory_;
    uint64_t cachedMaxHealth_ = 0;  // Max health of dead player for filtering
    float eventFilterThreshold_ = 7.0f;  // 0-20%, filter out events below this % of max health (default 7%)

    // Dependencies cached for popup rendering
    const CombatDatabase* combatDb_ = nullptr;
    const ResourceDatabase* resourceDb_ = nullptr;
    const std::unordered_map<uint32_t, std::string>* spellNameMap_ = nullptr;
    SpellIconLoader* iconLoader_ = nullptr;

    // Helper methods for popup rendering
    void openDeathBreakout(const DeathEvent* death);
    void closeDeathBreakout();
    void renderDeathBreakoutPopup();
    void renderHealthChart(float width, float height);
    void renderEventList();
};
