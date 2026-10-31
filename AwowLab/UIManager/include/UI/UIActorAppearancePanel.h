#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "UI/ActorAppearanceSettings.h"

class ActorColorGenerator;
class CustomShapeRegistry;

// Modal popup window for actor appearance customization
// Opened from Settings menu, not a persistent panel
class UIActorAppearancePanel {
public:
    UIActorAppearancePanel() = default;

    // Call to open the modal window
    void open();

    // Open to the Individual tab with a specific actor selected
    void openForActor(const std::string& actorGuid);

    // Call to close the modal window
    void close();

    // Returns true if the modal is currently open
    bool isOpen() const { return isOpen_; }

    // Set the custom shape registry (for loading OBJ files)
    void setCustomShapeRegistry(CustomShapeRegistry* registry) { customRegistry_ = registry; }

    // Set the path to the custom_models directory
    void setCustomModelsPath(const std::string& path) { customModelsPath_ = path; }

    // Render the modal window (call every frame, handles open/close state internally)
    // Returns true if settings were changed
    bool render(ActorAppearanceSettings& settings,
                const std::shared_ptr<ActorColorGenerator>& colorGen,
                const std::unordered_map<std::string_view, std::string_view>& guidToName,
                const std::string& selectedPlayerGuid);

private:
    bool isOpen_ = false;
    bool shouldOpenPopup_ = false;
    int currentTab_ = 0;
    bool shouldSelectIndividualTab_ = false;  // Force Individual tab selection on next frame

    // Search/filter state for Individual tab
    char searchBuffer_[256] = "";
    bool showOnlyOverridden_ = false;
    bool showOnlyFavorites_ = false;

    // Currently editing GUID (for individual override editing)
    std::string editingGuid_;

    // State for Enemies tab
    char enemiesSearchBuffer_[256] = "";
    std::unordered_set<std::string> expandedNameGroups_;  // Names currently expanded

    // Clipboard for copy/paste
    AppearanceOverride clipboard_;
    bool hasClipboard_ = false;
    std::string lastSelectedGuid_;  // Track which GUID is selected for apply-to-similar

    // Context menu state
    std::string contextMenuGuid_;   // GUID that was right-clicked
    std::string contextMenuName_;   // Display name for context menu
    uint16_t contextMenuSpecId_ = 0; // Spec ID for class/role operations

    // Custom shape registry
    CustomShapeRegistry* customRegistry_ = nullptr;
    std::string customModelsPath_;
    std::vector<std::string> cachedCustomShapeNames_;  // Cached for combo rendering
    bool customShapesNeedRefresh_ = true;  // Refresh on first combo open

    // Tab rendering
    void renderRoleDefaultsTab(ActorAppearanceSettings& settings);
    void renderOverridesTab(ActorAppearanceSettings& settings);
    void renderIndividualTab(ActorAppearanceSettings& settings,
                             const std::shared_ptr<ActorColorGenerator>& colorGen,
                             const std::unordered_map<std::string_view, std::string_view>& guidToName,
                             const std::string& selectedPlayerGuid);
    void renderEnemiesTab(ActorAppearanceSettings& settings,
                          const std::shared_ptr<ActorColorGenerator>& colorGen,
                          const std::unordered_map<std::string_view, std::string_view>& guidToName);
    void renderCustomModelsTab();

    // Helper widgets
    bool renderShapeCombo(const char* label, ActorShape& shape);
    bool renderOptionalShapeCombo(const char* label, std::optional<ActorShape>& shape);
    bool renderOptionalShapeComboWithCustom(const char* label,
                                            std::optional<ActorShape>& shape,
                                            std::optional<std::string>& customShapeName);
    bool renderColorEdit(const char* label, std::optional<float>& r,
                        std::optional<float>& g, std::optional<float>& b);
    bool renderColorEditWithPresets(const char* label, std::optional<float>& r,
                                    std::optional<float>& g, std::optional<float>& b,
                                    ActorAppearanceSettings& settings);
    bool renderScaleSlider(const char* label, std::optional<float>& scale);
    bool renderScaleSlider(const char* label, float& scale, float min = 0.5f, float max = 20.0f);

    // Override resolution trace (shows which overrides are active and their sources)
    void renderOverrideTrace(const ActorAppearanceSettings& settings,
                             const std::string& guid,
                             const std::string& actorName,
                             uint16_t specId);

    // Color presets bar (class colors, role colors, custom saved colors)
    // Returns true if a color was selected, and fills outR/outG/outB with the selected color
    bool renderColorPresetsBar(ActorAppearanceSettings& settings,
                               float& outR, float& outG, float& outB,
                               float currentR, float currentG, float currentB);

    // Refresh custom shapes from directory
    void refreshCustomShapes();

    // Render override level badges inline (e.g., [G] [C] [R])
    // Shows which override levels are active for the given actor
    void renderOverrideBadges(const ActorAppearanceSettings& settings,
                              const std::string& guid,
                              const std::string& actorName,
                              uint16_t specId);
};
