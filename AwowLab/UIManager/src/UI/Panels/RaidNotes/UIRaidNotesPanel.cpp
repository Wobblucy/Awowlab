#include "UI/Panels/RaidNotes/UIRaidNotesPanel.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/SpellIconLoader.h"
#include "RaidNoteContextMenu.h"
#include "Core/LocalizationManager.h"
#include "Core/Utils/FileNameUtils.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

namespace {

// How long the "Copied" / "Saved" button states stay up
constexpr float FEEDBACK_SECONDS = 2.0f;
constexpr float AUTOSAVE_INTERVAL_SECONDS = 10.0f;

std::optional<std::string> readWholeFile(const fs::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

} // namespace

UIRaidNotesPanel::UIRaidNotesPanel() {
    // Default export options
    exportOptions_.includeGroupHeaders = true;
    exportOptions_.includeSpellNames = true;
    exportOptions_.sortByTimestamp = true;
}

UIRaidNotesPanel::~UIRaidNotesPanel() = default;

void UIRaidNotesPanel::setCurrentEncounter(uint32_t encounterId, const std::string& encounterName) {
    if (currentEncounterId_ != encounterId) {
        currentEncounterId_ = encounterId;
        currentEncounterName_ = encounterName;
        selectedPresetIndex_ = -1;
        presetsLoaded_ = false;  // Force refresh when encounter changes
    }
}

bool UIRaidNotesPanel::hasSelectedPreset() const {
    return selectedPresetIndex_ >= 0 && selectedPresetIndex_ < static_cast<int>(savedPresets_.size());
}

void UIRaidNotesPanel::render(SpellIconLoader* iconLoader) {
    // Autosave runs even while the panel is hidden - entries can be
    // added from the spell context menu without the window being open.
    updateAutosave();

    if (!visible_) {
        return;
    }

    // Load presets on first render
    if (!presetsLoaded_) {
        refreshPresetList();
        presetsLoaded_ = true;
    }

    if (copyFeedbackTimer_ > 0.0f) {
        copyFeedbackTimer_ -= ImGui::GetIO().DeltaTime;
    }
    if (saveFeedbackTimer_ > 0.0f) {
        saveFeedbackTimer_ -= ImGui::GetIO().DeltaTime;
    }

    if (!entryToDelete_.empty()) {
        document_.removeEntry(entryToDelete_);
        entryToDelete_.clear();
    }
    if (!delayToDelete_.empty()) {
        document_.removePhaseDelay(delayToDelete_);
        delayToDelete_.clear();
    }

    ImGui::SetNextWindowSize(ImVec2(500, 550), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(L("raidnotes.title"), &visible_)) {
        // Offer to restore notes left behind by a crash
        if (autosaveRecoveryAvailable_) {
            renderRecoveryBar();
            ImGui::Separator();
        }

        renderSaveLoadSection();

        ImGui::Separator();

        renderToolbar();

        ImGui::Separator();

        // Scrollable content area
        float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2 + 10;
        ImGui::BeginChild("RaidNotesContent", ImVec2(0, -footerHeight), false);

        // Render phase delays first (if any)
        renderPhaseDelays();

        for (auto& group : document_.getGroups()) {
            renderGroup(group, iconLoader);
        }

        renderAddManualNoteSection();

        ImGui::EndChild();

        ImGui::Separator();
        renderFooter();
    }
    ImGui::End();
    // renderModal() is called from UIWindowManager::renderModalPanels() so the
    // context menu works even when this panel is not visible
}

void UIRaidNotesPanel::renderSaveLoadSection() {
    ImGui::Text("%s", L("raidnotes.load_preset"));
    ImGui::SameLine();

    ImGui::SetNextItemWidth(200);

    const char* previewText = hasSelectedPreset()
        ? savedPresets_[selectedPresetIndex_].name.c_str()
        : L("raidnotes.select_preset");

    if (ImGui::BeginCombo("##PresetCombo", previewText)) {
        for (int i = 0; i < static_cast<int>(savedPresets_.size()); ++i) {
            const auto& preset = savedPresets_[i];
            bool isSelected = (selectedPresetIndex_ == i);

            if (ImGui::Selectable(preset.name.c_str(), isSelected)) {
                selectedPresetIndex_ = i;
                loadPreset(preset.filename);
            }

            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();

    // Load button (for explicit load after selection)
    if (awlui::Button(L("raidnotes.load"), awlui::ButtonVariant::Secondary) && hasSelectedPreset()) {
        loadPreset(savedPresets_[selectedPresetIndex_].filename);
    }

    ImGui::SameLine();

    bool canDelete = hasSelectedPreset();
    if (!canDelete) {
        ImGui::BeginDisabled();
    }
    if (awlui::Button(L("raidnotes.delete_preset"), awlui::ButtonVariant::Danger)) {
        ImGui::OpenPopup("ConfirmDeletePreset");
    }
    if (!canDelete) {
        ImGui::EndDisabled();
    }

    if (ImGui::BeginPopupModal("ConfirmDeletePreset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", L("raidnotes.delete_confirm"));
        ImGui::Spacing();

        if (awlui::Button(L("common.yes"), awlui::ButtonVariant::Danger,
                          awlui::ButtonSize::Md, ImVec2(80, 0))) {
            if (hasSelectedPreset()) {
                deletePreset(savedPresets_[selectedPresetIndex_].filename);
                selectedPresetIndex_ = -1;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (awlui::Button(L("common.no"), awlui::ButtonVariant::Ghost,
                          awlui::ButtonSize::Md, ImVec2(80, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Text("%s", L("raidnotes.save_as"));
    ImGui::SameLine();

    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##SaveName", L("raidnotes.preset_name_hint"),
                              saveNameBuffer_, sizeof(saveNameBuffer_));
    ImGui::SameLine();

    if (saveFeedbackTimer_ > 0.0f) {
        // Show a static "saved" confirmation in Success color for a moment.
        ImGui::PushStyleColor(ImGuiCol_Button, awlui::theme::Success);
        awlui::Button(L("raidnotes.saved"), awlui::ButtonVariant::Primary,
                      awlui::ButtonSize::Md, ImVec2(60, 0));
        ImGui::PopStyleColor();
    } else {
        bool canSave = saveNameBuffer_[0] != '\0' && !document_.isEmpty();
        if (!canSave) {
            ImGui::BeginDisabled();
        }
        if (awlui::Button(L("raidnotes.save"), awlui::ButtonVariant::Primary,
                          awlui::ButtonSize::Md, ImVec2(60, 0))) {
            saveFailed_ = !savePreset(saveNameBuffer_);
            if (!saveFailed_) {
                saveFeedbackTimer_ = FEEDBACK_SECONDS;
            }
        }
        if (!canSave) {
            ImGui::EndDisabled();
        }
        if (saveFailed_) {
            ImGui::SameLine();
            ImGui::TextColored(awlui::theme::Danger, "%s", L("raidnotes.save_failed"));
        }
    }
}

void UIRaidNotesPanel::renderToolbar() {
    // Phase Delay UI: "Phase Delay: after [1:00] by [5] sec [Add]"
    ImGui::Text("%s", L("raidnotes.phase_delay"));
    ImGui::SameLine();

    ImGui::Text("%s", L("raidnotes.after"));
    ImGui::SameLine();

    ImGui::SetNextItemWidth(55);
    ImGui::InputText("##DelayAfter", delayAfterBuffer_, sizeof(delayAfterBuffer_));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("raidnotes.phase_delay_after_tooltip"));
    }

    ImGui::SameLine();
    ImGui::Text("%s", L("raidnotes.by"));
    ImGui::SameLine();

    ImGui::SetNextItemWidth(40);
    ImGui::InputText("##DelayAmount", delayAmountBuffer_, sizeof(delayAmountBuffer_),
                     ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    ImGui::Text("%s", L("raidnotes.sec"));
    ImGui::SameLine();

    if (awlui::Button(L("raidnotes.add_delay"), awlui::ButtonVariant::Secondary)) {
        int32_t afterMs = parseTimestamp(delayAfterBuffer_);
        int delaySeconds = std::atoi(delayAmountBuffer_);

        if (afterMs >= 0 && delaySeconds != 0) {
            // Add phase delay (editable, applies during export)
            document_.addPhaseDelay(afterMs, delaySeconds * 1000);
            // Reset delay amount but keep the "after" time for convenience
            std::strcpy(delayAmountBuffer_, "0");
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("raidnotes.phase_delay_tooltip"));
    }

    // Export options
    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    if (ImGui::BeginMenu(L("raidnotes.options"))) {
        ImGui::MenuItem(L("raidnotes.opt_headers"), nullptr, &exportOptions_.includeGroupHeaders);
        ImGui::MenuItem(L("raidnotes.opt_spellnames"), nullptr, &exportOptions_.includeSpellNames);
        ImGui::MenuItem(L("raidnotes.opt_sort"), nullptr, &exportOptions_.sortByTimestamp);
        ImGui::EndMenu();
    }
}

void UIRaidNotesPanel::renderPhaseDelays() {
    auto& delays = document_.getPhaseDelays();
    if (delays.empty()) {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.4f, 0.5f, 1.0f));

    if (ImGui::CollapsingHeader(L("raidnotes.phase_delays"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        for (auto& delay : delays) {
            ImGui::PushID(delay.id.c_str());

            // Format: "After 1:00 +5s [X]"
            std::string afterStr = formatTimestamp(delay.after_ms);
            int32_t delaySec = delay.delay_ms / 1000;
            char sign = delaySec >= 0 ? '+' : '-';

            ImGui::Text("%s %s %c%ds", L("raidnotes.after"), afterStr.c_str(),
                       sign, std::abs(delaySec));

            ImGui::SameLine();

            if (awlui::Button("X", awlui::ButtonVariant::Danger, awlui::ButtonSize::Sm)) {
                delayToDelete_ = delay.id;
            }

            ImGui::PopID();
        }

        ImGui::Unindent();
    }

    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void UIRaidNotesPanel::renderGroup(RaidNotes::RaidNoteGroup& group, SpellIconLoader* iconLoader) {
    // Collapsible header for each group
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;

    size_t enabledCount = 0;
    for (const auto& entry : group.entries) {
        if (entry.enabled) enabledCount++;
    }

    std::string headerLabel = group.display_name;
    if (!group.entries.empty()) {
        headerLabel += " (" + std::to_string(enabledCount) + "/" +
                       std::to_string(group.entries.size()) + ")";
    }

    if (group.collapsed) {
        flags &= ~ImGuiTreeNodeFlags_DefaultOpen;
    }

    bool open = ImGui::CollapsingHeader(headerLabel.c_str(), flags);
    group.collapsed = !open;

    if (open) {
        ImGui::Indent();

        if (group.entries.empty()) {
            ImGui::TextDisabled("%s", L("raidnotes.no_entries"));
        } else {
            for (auto& entry : group.entries) {
                ImGui::PushID(entry.id.c_str());
                renderEntry(entry, iconLoader);
                ImGui::PopID();
            }
        }

        ImGui::Unindent();
    }
}

void UIRaidNotesPanel::renderEntry(RaidNotes::RaidNoteEntry& entry, SpellIconLoader* iconLoader) {
    float availWidth = ImGui::GetContentRegionAvail().x;

    // Checkbox for enabled state
    awlui::Checkbox("##Enabled", &entry.enabled);
    ImGui::SameLine();

    // Timestamp display/edit
    std::string tsStr = formatTimestamp(entry.timestamp_ms);
    char tsBuf[32];
    std::strncpy(tsBuf, tsStr.c_str(), sizeof(tsBuf) - 1);
    tsBuf[sizeof(tsBuf) - 1] = '\0';

    ImGui::SetNextItemWidth(60);
    if (ImGui::InputText("##Timestamp", tsBuf, sizeof(tsBuf),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        int32_t newTs = parseTimestamp(tsBuf);
        if (newTs >= 0) {
            entry.timestamp_ms = newTs;
        }
    }
    ImGui::SameLine();

    // Spell icon (if available)
    if (entry.spell_id > 0 && iconLoader) {
        ImTextureID icon = iconLoader->getSpellIcon(entry.spell_id);
        if (icon) {
            ImGui::Image(icon, ImVec2(20, 20));
            SpellIconLoader::checkWowheadLookup(entry.spell_id);
            ImGui::SameLine();
        }
    }

    // Spell name
    if (!entry.spell_name.empty()) {
        ImGui::Text("%s", entry.spell_name.c_str());
        ImGui::SameLine();
    }

    // Custom text field
    float deleteButtonWidth = 25.0f;
    float customTextWidth = std::max(80.0f, availWidth - ImGui::GetCursorPosX() - deleteButtonWidth - 10.0f);

    char customBuf[256];
    std::strncpy(customBuf, entry.custom_text.c_str(), sizeof(customBuf) - 1);
    customBuf[sizeof(customBuf) - 1] = '\0';

    ImGui::SetNextItemWidth(customTextWidth);
    if (ImGui::InputTextWithHint("##CustomText", L("raidnotes.custom_text_hint"),
                                  customBuf, sizeof(customBuf))) {
        entry.custom_text = customBuf;
    }
    ImGui::SameLine();

    if (awlui::Button("X", awlui::ButtonVariant::Danger,
                      awlui::ButtonSize::Sm, ImVec2(deleteButtonWidth, 0))) {
        entryToDelete_ = entry.id;
    }
}

void UIRaidNotesPanel::renderAddManualNoteSection() {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("%s", L("raidnotes.add_manual"));

    ImGui::SetNextItemWidth(-100);
    bool enterPressed = ImGui::InputTextWithHint("##ManualNote", L("raidnotes.manual_hint"),
                                                  manualNoteBuffer_, sizeof(manualNoteBuffer_),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();

    if (awlui::Button(L("raidnotes.add"), awlui::ButtonVariant::Primary) ||
        (enterPressed && manualNoteBuffer_[0] != '\0')) {
        if (manualNoteBuffer_[0] != '\0') {
            RaidNotes::RaidNoteEntry entry;
            entry.spell_id = 0;  // Manual entry
            entry.timestamp_ms = 0;  // User should edit
            entry.custom_text = manualNoteBuffer_;
            entry.enabled = true;

            document_.addEntry(RaidNotes::RaidNoteGroupType::Manual, std::move(entry));
            manualNoteBuffer_[0] = '\0';
        }
    }
}

void UIRaidNotesPanel::renderFooter() {
    float buttonWidth = 140.0f;
    float totalWidth = buttonWidth * 2 + ImGui::GetStyle().ItemSpacing.x;
    float startX = (ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);

    if (copyFeedbackTimer_ > 0.0f) {
        ImGui::PushStyleColor(ImGuiCol_Button, awlui::theme::Success);
        awlui::Button(L("raidnotes.copied"), awlui::ButtonVariant::Primary,
                      awlui::ButtonSize::Md, ImVec2(buttonWidth, 0));
        ImGui::PopStyleColor();
    } else {
        if (awlui::Button(L("raidnotes.copy_clipboard"), awlui::ButtonVariant::Primary,
                          awlui::ButtonSize::Md, ImVec2(buttonWidth, 0))) {
            std::string exported = RaidNotes::RaidNoteExporter::exportToERT(document_, exportOptions_);
            ImGui::SetClipboardText(exported.c_str());
            copyFeedbackTimer_ = FEEDBACK_SECONDS;
        }
    }

    ImGui::SameLine();

    if (awlui::Button(L("raidnotes.clear_all"), awlui::ButtonVariant::Danger,
                      awlui::ButtonSize::Md, ImVec2(buttonWidth, 0))) {
        ImGui::OpenPopup("ConfirmClear");
    }

    // Confirmation popup
    if (ImGui::BeginPopupModal("ConfirmClear", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", L("raidnotes.clear_confirm"));
        ImGui::Spacing();

        if (awlui::Button(L("common.yes"), awlui::ButtonVariant::Danger,
                          awlui::ButtonSize::Md, ImVec2(80, 0))) {
            document_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (awlui::Button(L("common.no"), awlui::ButtonVariant::Ghost,
                          awlui::ButtonSize::Md, ImVec2(80, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Entry count
    ImGui::Spacing();
    size_t count = document_.getEntryCount();
    ImGui::TextDisabled("%zu %s", count, count == 1 ? L("raidnotes.entry") : L("raidnotes.entries"));
}

std::string UIRaidNotesPanel::formatTimestamp(int32_t ms) const {
    bool negative = ms < 0;
    int32_t absMs = std::abs(ms);

    int32_t totalSeconds = absMs / 1000;
    int32_t minutes = totalSeconds / 60;
    int32_t seconds = totalSeconds % 60;

    std::ostringstream oss;
    if (negative) {
        oss << "-";
    }
    oss << minutes << ":";
    if (seconds < 10) {
        oss << "0";
    }
    oss << seconds;

    return oss.str();
}

int32_t UIRaidNotesPanel::parseTimestamp(const std::string& str) const {
    // Parse M:SS or -M:SS format
    bool negative = false;
    size_t start = 0;

    if (!str.empty() && str[0] == '-') {
        negative = true;
        start = 1;
    }

    size_t colonPos = str.find(':');
    if (colonPos == std::string::npos || colonPos <= start) {
        return -1;  // Invalid format
    }

    try {
        int minutes = std::stoi(str.substr(start, colonPos - start));
        int seconds = std::stoi(str.substr(colonPos + 1));

        if (seconds < 0 || seconds >= 60) {
            return -1;
        }

        int32_t ms = (minutes * 60 + seconds) * 1000;
        return negative ? -ms : ms;
    } catch (...) {
        return -1;
    }
}

std::string UIRaidNotesPanel::getPresetsDirectory() const {
    // Store presets in data/raid_notes/ next to executable
    return "data/raid_notes";
}

std::string UIRaidNotesPanel::getAutosavePath() const {
    return getPresetsDirectory() + "/.autosave.json";
}

void UIRaidNotesPanel::updateAutosave() {
    // First frame: look for a sidecar a previous session left behind.
    // Anything found now predates this session's own autosaves.
    if (!autosaveChecked_) {
        autosaveChecked_ = true;
        try {
            autosaveRecoveryAvailable_ =
                fs::exists(getAutosavePath()) && fs::file_size(getAutosavePath()) > 0;
        } catch (const std::exception&) {
            autosaveRecoveryAvailable_ = false;
        }
    }

    autosaveTimer_ += ImGui::GetIO().DeltaTime;
    if (autosaveTimer_ < AUTOSAVE_INTERVAL_SECONDS) {
        return;
    }
    autosaveTimer_ = 0.0f;

    // Don't overwrite a not-yet-answered recovery offer
    if (autosaveRecoveryAvailable_) {
        return;
    }

    std::string json = document_.isEmpty() ? std::string() : document_.toJson();
    if (json == lastAutosavedJson_) {
        return;
    }

    try {
        std::string dir = getPresetsDirectory();
        if (!fs::exists(dir)) {
            fs::create_directories(dir);
        }
        if (json.empty()) {
            std::error_code ec;
            fs::remove(getAutosavePath(), ec);
        } else {
            std::ofstream file(getAutosavePath());
            if (!file.is_open()) {
                return;
            }
            file << json;
        }
        lastAutosavedJson_ = std::move(json);
    } catch (const std::exception&) {
        // Autosave is best-effort - never disturb the editing session
    }
}

void UIRaidNotesPanel::renderRecoveryBar() {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", L("raidnotes.recovered_notes"));
    ImGui::SameLine();
    if (awlui::Button(L("raidnotes.restore"), awlui::ButtonVariant::Secondary, awlui::ButtonSize::Sm)) {
        try {
            if (auto content = readWholeFile(getAutosavePath())) {
                if (auto doc = RaidNotes::RaidNoteDocument::fromJson(*content)) {
                    document_ = std::move(*doc);
                    lastAutosavedJson_ = std::move(*content);
                }
            }
        } catch (const std::exception&) {
            // Unreadable sidecar - nothing to restore
        }
        autosaveRecoveryAvailable_ = false;
    }
    ImGui::SameLine();
    if (awlui::Button(L("raidnotes.discard"), awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
        std::error_code ec;
        fs::remove(getAutosavePath(), ec);
        autosaveRecoveryAvailable_ = false;
    }
}

std::string UIRaidNotesPanel::sanitizeFilename(const std::string& name) const {
    // Strips only what Windows forbids, so names in other scripts stay
    // distinct files instead of all landing on preset.json
    return awow::files::toFileStem(name, "preset") + ".json";
}

void UIRaidNotesPanel::refreshPresetList() {
    savedPresets_.clear();

    std::string dir = getPresetsDirectory();

    try {
        if (!fs::exists(dir)) {
            return;  // No presets directory yet
        }

        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                RaidNotePreset preset;
                // UTF-8, not .string(): that converts to the ANSI code page
                // and throws on names it cannot represent
                preset.filename = awow::files::utf8FromPath(entry.path().filename());

                // Display name is the file stem. Underscores read as spaces,
                // but only for files that parse as notes, so anything else
                // in the folder keeps its literal name.
                preset.name = awow::files::utf8FromPath(entry.path().stem());
                auto content = readWholeFile(entry.path());
                if (content && RaidNotes::RaidNoteDocument::fromJson(*content)) {
                    std::replace(preset.name.begin(), preset.name.end(), '_', ' ');
                }

                savedPresets_.push_back(std::move(preset));
            }
        }

        std::sort(savedPresets_.begin(), savedPresets_.end(),
            [](const RaidNotePreset& a, const RaidNotePreset& b) {
                return a.name < b.name;
            });

    } catch (const std::exception&) {
        // Filesystem errors - ignore
    }
}

bool UIRaidNotesPanel::savePreset(const std::string& name) {
    std::string dir = getPresetsDirectory();

    try {
        // Create directory if it doesn't exist
        if (!fs::exists(dir)) {
            fs::create_directories(dir);
        }

        std::string filename = sanitizeFilename(name);
        std::string filepath = dir + "/" + filename;

        // Serialize document to JSON
        std::string json = document_.toJson();

        std::ofstream file(awow::files::pathFromUtf8(filepath));
        if (file.is_open()) {
            file << json;
            file.close();
            if (file.fail()) {
                return false;
            }

            // Explicit save supersedes the crash-recovery sidecar
            lastAutosavedJson_ = json;
            std::error_code removeEc;
            fs::remove(getAutosavePath(), removeEc);

            std::strncpy(saveNameBuffer_, name.c_str(), sizeof(saveNameBuffer_) - 1);
            saveNameBuffer_[sizeof(saveNameBuffer_) - 1] = '\0';

            refreshPresetList();

            // Select the newly saved preset
            for (int i = 0; i < static_cast<int>(savedPresets_.size()); ++i) {
                if (savedPresets_[i].filename == filename) {
                    selectedPresetIndex_ = i;
                    break;
                }
            }
            return true;
        }
    } catch (const std::exception&) {
        // Reported to the user through the return value
    }
    return false;
}

void UIRaidNotesPanel::loadPreset(const std::string& filename) {
    const fs::path filepath = awow::files::pathFromUtf8(getPresetsDirectory() + "/" + filename);

    try {
        if (auto content = readWholeFile(filepath)) {
            if (auto doc = RaidNotes::RaidNoteDocument::fromJson(*content)) {
                document_ = std::move(*doc);
            }
        }
    } catch (const std::exception&) {
        // Unreadable preset - keep the current notes
    }
}

void UIRaidNotesPanel::deletePreset(const std::string& filename) {
    const fs::path filepath = awow::files::pathFromUtf8(getPresetsDirectory() + "/" + filename);

    try {
        if (fs::exists(filepath)) {
            fs::remove(filepath);
            refreshPresetList();
        }
    } catch (const std::exception&) {
        // Left in place; the list still shows it
    }
}
