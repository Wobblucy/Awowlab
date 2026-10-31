#include "UI/Panels/UISettingsExportPanel.h"
#include "UI/AwlUI/Widgets.h"
#include "Core/Settings/SettingsExportManager.h"
#include "Core/LocalizationManager.h"
#include "Core/Utils/FileNameUtils.h"
#include <imgui.h>
#include <filesystem>

void UISettingsExportPanel::setVisible(bool visible) {
    if (visible && !visible_) {
        currentTab_ = 0;
        selectedExportKeys_.clear();
        exportAllSelected_ = false;
        fileProbed_ = false;
        availableImportKeys_.clear();
        selectedImportKeys_.clear();
        clearStatus();
    }
    visible_ = visible;
}

void UISettingsExportPanel::render() {
    if (!visible_) return;

    if (statusTimer_ > 0.0f) {
        statusTimer_ -= ImGui::GetIO().DeltaTime;
        if (statusTimer_ <= 0.0f) {
            clearStatus();
        }
    }

    ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin(L("Import/Export Settings"), &visible_, ImGuiWindowFlags_NoCollapse)) {
        // Tab bar
        if (ImGui::BeginTabBar("SettingsExportTabs")) {
            if (ImGui::BeginTabItem(L("Export"))) {
                currentTab_ = 0;
                renderExportTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(L("Import"))) {
                currentTab_ = 1;
                renderImportTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        // Status message at bottom
        if (!statusMessage_.empty()) {
            ImGui::Separator();
            if (statusIsError_) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", statusMessage_.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", statusMessage_.c_str());
            }
        }
    }
    ImGui::End();
}

void UISettingsExportPanel::renderExportTab() {
    auto& manager = SettingsExportManager::instance();
    auto categories = manager.getAvailableCategories();

    ImGui::Text(L("Select settings to export:"));
    ImGui::Separator();

    // Scrollable checkbox area
    ImGui::BeginChild("ExportCategories", ImVec2(0, -100), true);
    renderCategoryCheckboxes(categories, selectedExportKeys_);
    ImGui::EndChild();

    // Select All / Select None buttons
    if (awlui::Button(L("Select All"), awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
        for (const auto& [key, name] : categories) {
            selectedExportKeys_.insert(key);
        }
    }
    ImGui::SameLine();
    if (awlui::Button(L("Select None"), awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
        selectedExportKeys_.clear();
    }

    ImGui::Separator();

    // Filename input
    ImGui::Text(L("Filename:"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##ExportFilename", exportFilename_, sizeof(exportFilename_));
    ImGui::SameLine();
    ImGui::TextDisabled(".json");

    // Export button
    ImGui::Spacing();
    bool canExport = !selectedExportKeys_.empty() && strlen(exportFilename_) > 0;
    if (!canExport) ImGui::BeginDisabled();
    if (awlui::Button(L("Export Selected"), awlui::ButtonVariant::Primary,
                      awlui::ButtonSize::Md, ImVec2(-1, 0))) {
        doExport();
    }
    if (!canExport) ImGui::EndDisabled();

    // Help text
    ImGui::TextDisabled(L("Files saved to: exported_settings/"));
}

void UISettingsExportPanel::renderImportTab() {
    auto& manager = SettingsExportManager::instance();
    auto categories = manager.getAvailableCategories();

    // File path input
    ImGui::Text(L("File path:"));
    ImGui::SetNextItemWidth(-80);
    if (ImGui::InputText("##ImportFilePath", importFilePath_, sizeof(importFilePath_))) {
        // Path changed, need to re-probe
        fileProbed_ = false;
    }
    ImGui::SameLine();
    if (awlui::Button(L("Probe"), awlui::ButtonVariant::Secondary)) {
        probeImportFile();
    }

    ImGui::Separator();

    if (!fileProbed_) {
        ImGui::TextDisabled(L("Enter a file path and click Probe to see available settings."));
    } else if (availableImportKeys_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), L("No valid settings found in file."));
    } else {
        ImGui::Text(L("Select settings to import:"));

        // Scrollable checkbox area
        ImGui::BeginChild("ImportCategories", ImVec2(0, -80), true);
        renderCategoryCheckboxes(categories, selectedImportKeys_, &availableImportKeys_);
        ImGui::EndChild();

        // Select All Available button
        if (awlui::Button(L("Select All Available"), awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
            selectedImportKeys_ = availableImportKeys_;
        }
    }

    ImGui::Separator();

    // Import button
    bool canImport = fileProbed_ && !selectedImportKeys_.empty();
    if (!canImport) ImGui::BeginDisabled();
    if (awlui::Button(L("Import Selected"), awlui::ButtonVariant::Primary,
                      awlui::ButtonSize::Md, ImVec2(-1, 0))) {
        doImport();
    }
    if (!canImport) ImGui::EndDisabled();
}

void UISettingsExportPanel::renderCategoryCheckboxes(
    const std::vector<std::pair<std::string, std::string>>& categories,
    std::set<std::string>& selectedKeys,
    const std::set<std::string>* availableKeys
) {
    for (const auto& [key, displayName] : categories) {
        bool isAvailable = (availableKeys == nullptr) || (availableKeys->count(key) > 0);
        bool isSelected = selectedKeys.count(key) > 0;

        if (!isAvailable) {
            ImGui::BeginDisabled();
        }

        if (awlui::Checkbox(displayName.c_str(), &isSelected)) {
            if (isSelected) {
                selectedKeys.insert(key);
            } else {
                selectedKeys.erase(key);
            }
        }

        if (!isAvailable) {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", L("settings_export.not_in_file"));
        }
    }
}

void UISettingsExportPanel::doExport() {
    auto& manager = SettingsExportManager::instance();

    // Only a bare file name: the export always lands in the export folder
    if (!awow::files::isPlainFileName(exportFilename_)) {
        setStatus(L("settings_export.invalid_filename"), true);
        return;
    }

    std::string dir = SettingsExportManager::getExportDirectory();
    std::string filename = std::string(exportFilename_) + ".json";
    std::string filepath = dir + "/" + filename;

    // Never replace an earlier export without the user picking that name again
    std::error_code ec;
    if (std::filesystem::exists(awow::files::pathFromUtf8(filepath), ec)) {
        setStatus(LF("settings_export.file_exists_fmt", filename.c_str()), true);
        return;
    }

    if (manager.exportToFile(filepath, selectedExportKeys_)) {
        setStatus(LF("settings_export.exported_fmt", selectedExportKeys_.size(), filename.c_str()), false);
    } else {
        setStatus(L("settings_export.export_failed"), true);
    }
}

void UISettingsExportPanel::doImport() {
    auto& manager = SettingsExportManager::instance();

    size_t count = manager.importFromFile(importFilePath_, selectedImportKeys_);
    if (count > 0) {
        setStatus(LF("settings_export.imported_fmt", count), false);
    } else {
        setStatus(L("settings_export.import_failed"), true);
    }
}

void UISettingsExportPanel::probeImportFile() {
    auto& manager = SettingsExportManager::instance();

    std::string filepath = importFilePath_;

    std::error_code ec;
    if (!std::filesystem::exists(awow::files::pathFromUtf8(filepath), ec)) {
        setStatus(L("settings_export.file_not_found"), true);
        fileProbed_ = false;
        return;
    }

    availableImportKeys_ = manager.probeFile(filepath);
    selectedImportKeys_.clear();

    // Auto-select all available
    selectedImportKeys_ = availableImportKeys_;

    fileProbed_ = true;
    lastProbedFile_ = filepath;

    if (availableImportKeys_.empty()) {
        setStatus(L("settings_export.no_valid_settings"), true);
    } else {
        setStatus(LF("settings_export.found_fmt", availableImportKeys_.size()), false);
    }
}

void UISettingsExportPanel::setStatus(const std::string& message, bool isError) {
    statusMessage_ = message;
    statusIsError_ = isError;
    statusTimer_ = 5.0f;  // Show for 5 seconds
}

void UISettingsExportPanel::clearStatus() {
    statusMessage_.clear();
    statusIsError_ = false;
    statusTimer_ = 0.0f;
}
