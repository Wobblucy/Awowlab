#pragma once

#include "UI/NameplateSettings.h"

// Modal popup window for nameplate text appearance customization
// Opened from Settings menu, provides per-category settings for Player and Other actors
class UINameplateSettingsPanel {
public:
    UINameplateSettingsPanel() = default;

    // Call to open the modal window
    void open();

    // Call to close the modal window
    void close();

    // Returns true if the modal is currently open
    bool isOpen() const { return isOpen_; }

    // Render the modal window (call every frame, handles open/close state internally)
    // Returns true if settings were changed
    bool render(NameplateSettings& settings);

private:
    bool isOpen_ = false;
    bool shouldOpenPopup_ = false;
    int currentTab_ = 0;

    // Render the settings UI for a category (reused for Player and Other tabs)
    void renderCategorySettings(const char* categoryLabel, NameplateCategorySettings& settings);
};
