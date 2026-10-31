#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <imgui.h>

class ActorColorGenerator;
struct InterpolatedPosition;
struct InterpolatedResource;

// Configuration for enemy grid appearance
struct EnemyGridConfig {
    int columns = 5;              // Max columns
    int visibleRows = 8;          // Visible rows before scrolling
    float cellWidth = 90.0f;      // Width per enemy cell
    float cellHeight = 50.0f;     // Height per enemy cell
    float healthBarHeight = 12.0f;
    float powerBarHeight = 8.0f;
    float cellPadding = 2.0f;
};

// Enemy data for grid display
struct EnemyGridEntry {
    std::string guid;
    std::string name;
    float healthPercent = 1.0f;   // 0.0 to 1.0
    uint64_t currentHealth = 0;   // uint64 for large health values
    uint64_t maxHealth = 0;
    float powerPercent = 0.0f;
    uint32_t currentPower = 0;
    uint32_t maxPower = 0;
    int8_t powerType = 0;         // PowerType enum value
    float r = 0.8f, g = 0.2f, b = 0.2f;  // Default red for enemies
    bool isActive = false;        // Has position in current time window
    bool isDead = false;          // Health == 0
};

class UIEnemyGridPanel {
public:
    UIEnemyGridPanel() = default;

    // Render the enemy grid
    void render(const std::shared_ptr<ActorColorGenerator>& colorGen,
                const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
                const std::unordered_map<std::string_view, InterpolatedResource>& actorResources,
                const std::unordered_map<std::string_view, std::string_view>& guidToName);

    // Selection integration - returns GUID of clicked enemy (if any)
    std::optional<std::string> getClickedEnemy() const { return clickedEnemy_; }
    void clearClickedEnemy() { clickedEnemy_ = std::nullopt; }

    // Clear selection request (from right-click context menu)
    bool isClearSelectionRequested() const { return clearSelectionRequested_; }
    void clearClearSelectionRequest() { clearSelectionRequested_ = false; }

    // Configuration accessors
    EnemyGridConfig& getConfig() { return config_; }
    const EnemyGridConfig& getConfig() const { return config_; }

private:
    EnemyGridConfig config_;
    std::optional<std::string> clickedEnemy_;
    bool clearSelectionRequested_ = false;

    // Cached sorted enemy order (GUIDs only)
    std::vector<std::string> cachedEnemyOrder_;
    size_t lastGuidToNameSize_ = 0;

    // Reusable buffer for frame data
    std::vector<EnemyGridEntry> enemiesBuffer_;

    // Collect and filter hostile/neutral enemies from the current frame data
    std::vector<EnemyGridEntry> collectEnemies(
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, InterpolatedPosition>& actorPositions,
        const std::unordered_map<std::string_view, InterpolatedResource>& actorResources,
        const std::unordered_map<std::string_view, std::string_view>& guidToName);

    // Render a single enemy cell, returns true if clicked
    bool renderEnemyCell(const EnemyGridEntry& enemy, float cellWidth, float cellHeight,
                         float healthBarHeight, float powerBarHeight);
};
