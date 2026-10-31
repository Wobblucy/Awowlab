#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdint>
#include <imgui.h>
#include "Core/SpellStateComputer.h"

class ActorColorGenerator;
struct InterpolatedPosition;
struct InterpolatedResource;
class SpellCastDatabase;
class AuraDatabase;
class SpellIconLoader;

// Configuration for raid frame appearance
struct RaidFrameConfig {
    int columns = 5;              // Max columns (5 for raid groups)
    int visibleRows = 8;          // Visible rows before scrolling
    float cellWidth = 90.0f;      // Width per player cell
    float cellHeight = 50.0f;     // Height per player cell (name + health + power)
    float healthBarHeight = 12.0f;
    float powerBarHeight = 8.0f;
    float cellPadding = 2.0f;

    // Spell icon tracking options (replaces power bar when enabled)
    bool showSpellIcons = true;      // Master toggle
    float spellIconSize = 16.0f;     // Icon size in pixels
    float spellIconSpacing = 2.0f;   // Space between icons
    int maxSpellIcons = 4;           // Max icons per player

    // Serialization
    std::string toJson() const;
    void fromJson(const std::string& json);
};

// Player data for raid frame display
struct RaidFramePlayer {
    std::string guid;
    std::string name;
    float healthPercent = 1.0f;   // 0.0 to 1.0
    uint64_t currentHealth = 0;   // uint64 for large health values
    uint64_t maxHealth = 0;
    float powerPercent = 0.0f;
    uint32_t currentPower = 0;
    uint32_t maxPower = 0;
    int8_t powerType = 0;         // PowerType enum value
    float r = 0.5f, g = 0.5f, b = 0.5f;  // Class color (RGB 0-1)
    bool isActive = false;        // Has position in current time window
    bool isDead = false;          // Health == 0
    uint16_t specId = 0;          // For spell assignment lookup
};

class UIRaidFramePanel {
public:
    UIRaidFramePanel() = default;

    // Spell state cache tolerance - 250ms for performance (cooldowns update ~4x/sec)
    static constexpr uint32_t SPELL_STATE_CACHE_TOLERANCE_MS = 250;

    // Render the raid frame grid
    void render(const std::shared_ptr<ActorColorGenerator>& colorGen,
                const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
                const std::unordered_map<std::string_view, InterpolatedResource>& actorResources,
                const std::unordered_map<std::string_view, std::string_view>& guidToName,
                const SpellCastDatabase* spellCastDb,
                const AuraDatabase* auraDb,
                SpellIconLoader* iconLoader,
                uint32_t currentTimeMs);

    // Selection integration - returns GUID of clicked player (if any)
    std::optional<std::string> getClickedPlayer() const { return clickedPlayer_; }
    void clearClickedPlayer() { clickedPlayer_ = std::nullopt; }

    // Clear selection request (from right-click context menu)
    bool isClearSelectionRequested() const { return clearSelectionRequested_; }
    void clearClearSelectionRequest() { clearSelectionRequested_ = false; }

    // Configuration accessors
    RaidFrameConfig& getConfig() { return config_; }
    const RaidFrameConfig& getConfig() const { return config_; }

private:
    RaidFrameConfig config_;
    std::optional<std::string> clickedPlayer_;
    bool clearSelectionRequested_ = false;

    // Cached sorted player order (GUIDs only)
    std::vector<std::string> cachedPlayerOrder_;
    size_t lastGuidToNameSize_ = 0;

    // Reusable buffer for frame data
    std::vector<RaidFramePlayer> playersBuffer_;

    // Spell state cache - avoids recomputing every frame
    std::unordered_map<std::string, std::vector<SpellStateResult>> cachedSpellStates_;
    uint32_t spellStateCacheTime_ = 0;

    // Collect and filter friendly players from the current frame data
    std::vector<RaidFramePlayer> collectRaidPlayers(
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
        const std::unordered_map<std::string_view, InterpolatedResource>& actorResources,
        const std::unordered_map<std::string_view, std::string_view>& guidToName);

    // Render a single player cell, returns true if clicked
    bool renderPlayerCell(const RaidFramePlayer& player, float cellWidth, float cellHeight,
                          float healthBarHeight, float powerBarHeight,
                          const SpellCastDatabase* spellCastDb,
                          const AuraDatabase* auraDb,
                          SpellIconLoader* iconLoader,
                          uint32_t currentTimeMs);

    // Class-coloured health bar, with an X over it for dead players
    void renderHealthBar(float healthPercent, float r, float g, float b,
                         float width, float height, bool isDead);

    // Render spell icons in place of power bar
    void renderSpellIcons(const RaidFramePlayer& player, float barWidth,
                          const SpellCastDatabase* spellCastDb,
                          const AuraDatabase* auraDb,
                          SpellIconLoader* iconLoader,
                          uint32_t currentTimeMs);

    // Draw a single spell icon with its state (uses shared SpellStateResult)
    void drawSpellIcon(ImDrawList* drawList, ImVec2 iconMin, ImVec2 iconMax,
                       const SpellStateResult& spellState, SpellIconLoader* iconLoader);

    // Get cached spell states or compute if cache is stale
    const std::vector<SpellStateResult>& getOrComputeSpellStates(
        const std::string& playerGuid,
        const std::vector<uint32_t>& spellIds,
        const SpellCastDatabase* spellCastDb,
        const AuraDatabase* auraDb,
        uint32_t currentTimeMs);
};
