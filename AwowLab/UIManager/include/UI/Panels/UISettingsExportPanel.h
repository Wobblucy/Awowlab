#pragma once

#include <string>
#include <set>
#include <vector>

// Modal panel for exporting and importing settings
class UISettingsExportPanel {
public:
    UISettingsExportPanel() = default;
    ~UISettingsExportPanel() = default;

    // Render the panel (call every frame when visible)
    void render();

    // Visibility control
    bool isVisible() const { return visible_; }
    void setVisible(bool visible);
    void show() { setVisible(true); }
    void hide() { setVisible(false); }

private:
    bool visible_ = false;

    // Current tab (0 = Export, 1 = Import)
    int currentTab_ = 0;

    // Export state
    std::set<std::string> selectedExportKeys_;
    char exportFilename_[256] = "settings";
    bool exportAllSelected_ = false;

    // Import state
    char importFilePath_[512] = "";
    std::set<std::string> availableImportKeys_;  // Keys found in file
    std::set<std::string> selectedImportKeys_;
    bool fileProbed_ = false;
    std::string lastProbedFile_;

    // Status messages
    std::string statusMessage_;
    bool statusIsError_ = false;
    float statusTimer_ = 0.0f;

    // Render helpers
    void renderExportTab();
    void renderImportTab();
    void renderCategoryCheckboxes(
        const std::vector<std::pair<std::string, std::string>>& categories,
        std::set<std::string>& selectedKeys,
        const std::set<std::string>* availableKeys = nullptr  // nullptr = all available
    );

    void setStatus(const std::string& message, bool isError = false);
    void clearStatus();

    // Actions
    void doExport();
    void doImport();
    void probeImportFile();
};
