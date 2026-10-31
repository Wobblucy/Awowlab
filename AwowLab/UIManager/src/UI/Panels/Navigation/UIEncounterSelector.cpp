#include "UI/Panels/Navigation/UIEncounterSelector.h"
#include "UI/AwlUI/Widgets.h"
#include "Encounter/EncounterSegment.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>

UIEncounterSelector::UIEncounterSelector()
    : selectedIndex_(0)
    , encounterSelected_(false)
    , refreshRequested_(false)
    , returnToHomeRequested_(false)
    , totalLines_(0)
    , parsedBytes_(0)
    , totalBytes_(0)
{
}

void UIEncounterSelector::setEncounters(const std::vector<EncounterSegment>& encounters) {
    encounters_ = encounters;
    selectedIndex_ = 0;
    encounterSelected_ = false;
    refreshRequested_ = false;
    returnToHomeRequested_ = false;
}

void UIEncounterSelector::setFileInfo(size_t totalLines, size_t parsedBytes, size_t totalBytes) {
    totalLines_ = totalLines;
    parsedBytes_ = parsedBytes;
    totalBytes_ = totalBytes;
}

bool UIEncounterSelector::render() {
    if (encounterSelected_) {
        return false;  // Already selected, don't show window
    }

    ImGui::OpenPopup("Select Encounter");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(850, 500), ImGuiCond_Appearing);

    bool isOpen = true;
    if (ImGui::BeginPopupModal("Select Encounter", &isOpen, ImGuiWindowFlags_NoResize)) {
        ImGui::Text(L("format.encounters_found"), encounters_.size());

        // File info row
        if (totalLines_ > 0 || totalBytes_ > 0) {
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(20, 0));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f),
                L("encounter.parsed_info"),
                totalLines_,
                parsedBytes_ / (1024.0 * 1024.0));
        }

        ImGui::Spacing();

        // All buttons at the top
        if (awlui::Button(L("btn.load_encounter"), awlui::ButtonVariant::Primary,
                          awlui::ButtonSize::Md, ImVec2(200, 0))) {
            encounterSelected_ = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (awlui::Button(L("btn.refresh_log"), awlui::ButtonVariant::Secondary,
                          awlui::ButtonSize::Md, ImVec2(120, 0))) {
            refreshRequested_ = true;
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("tooltip.refresh_encounters"));
        }

        ImGui::SameLine();

        if (awlui::Button(L("btn.return_home"), awlui::ButtonVariant::Ghost,
                          awlui::ButtonSize::Md, ImVec2(130, 0))) {
            returnToHomeRequested_ = true;
            ImGui::CloseCurrentPopup();
        }

        // F5 keyboard shortcut for refresh
        if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
            refreshRequested_ = true;
        }

        ImGui::Separator();

        // Table with encounter information
        if (ImGui::BeginTable("EncounterTable", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {

            // Headers
            ImGui::TableSetupColumn(L("encounter.col_number"), ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn(L("encounter.col_name"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(L("encounter.col_difficulty"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn(L("encounter.col_size"), ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn(L("encounter.col_duration"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(L("encounter.col_result"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            // Encounter rows
            for (size_t i = 0; i < encounters_.size(); ++i) {
                const auto& encounter = encounters_[i];

                ImGui::TableNextRow();

                // Column 0: Index
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(
                    ("##enc" + std::to_string(i)).c_str(),
                    selectedIndex_ == i,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick,
                    ImVec2(0, 0)
                )) {
                    selectedIndex_ = i;
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        encounterSelected_ = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                ImGui::Text("%zu", i + 1);

                // Column 1: Encounter Name (with M+ indicator)
                ImGui::TableSetColumnIndex(1);
                if (encounter.isMythicPlus()) {
                    // M+ dungeon - show with special formatting
                    ImGui::TextColored(ImVec4(0.6f, 0.4f, 1.0f, 1.0f), "%s", encounter.encounterName.c_str());
                } else {
                    ImGui::Text("%s", encounter.encounterName.c_str());
                }

                // Column 2: Difficulty (show keystone level for M+)
                ImGui::TableSetColumnIndex(2);
                if (encounter.isMythicPlus()) {
                    ImGui::TextColored(ImVec4(0.6f, 0.4f, 1.0f, 1.0f), "M+ %u", encounter.keystoneLevel);
                } else {
                    ImGui::Text("%s", getDifficultyName(encounter.difficultyId));
                }

                // Column 3: Group Size
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%u", encounter.groupSize);

                // Column 4: Duration (with underflow protection)
                ImGui::TableSetColumnIndex(4);
                uint32_t durationMs = 0;
                if (encounter.endTimestamp_ms >= encounter.startTimestamp_ms) {
                    durationMs = encounter.endTimestamp_ms - encounter.startTimestamp_ms;
                }
                uint32_t durationSec = durationMs / 1000;

                // Sanity check: cap duration at 3 hours (covers longest M+ runs)
                constexpr uint32_t MAX_DURATION_SEC = 3 * 60 * 60;  // 10800 seconds
                if (durationSec > MAX_DURATION_SEC) {
                    durationSec = 0;  // Invalid duration, display as 0:00
                }

                uint32_t minutes = durationSec / 60;
                uint32_t seconds = durationSec % 60;
                ImGui::Text("%u:%02u", minutes, seconds);

                // Column 5: Result
                ImGui::TableSetColumnIndex(5);
                if (encounter.isMythicPlus()) {
                    // M+ uses "Timed" / "Depleted" instead of Kill/Wipe
                    if (encounter.isSuccess) {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", L("encounter.timed"));
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "%s", L("encounter.depleted"));
                    }
                } else {
                    if (encounter.isSuccess) {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", L("encounter.kill"));
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", L("encounter.wipe"));
                    }
                }
            }

            ImGui::EndTable();
        }

        ImGui::EndPopup();
    }

    return encounterSelected_;
}

const char* UIEncounterSelector::getDifficultyName(uint32_t difficultyId) {
    // WoW difficulty IDs
    // Source: https://wowpedia.fandom.com/wiki/DifficultyID
    switch (difficultyId) {
        case 1:  return L("difficulty.normal");
        case 2:  return L("difficulty.heroic");
        case 3:  return L("difficulty.10_player");
        case 4:  return L("difficulty.25_player");
        case 5:  return L("difficulty.10_heroic");
        case 6:  return L("difficulty.25_heroic");
        case 7:  return L("difficulty.lfr");
        case 8:  return L("difficulty.mythic_keystone");
        case 9:  return L("difficulty.40_player");
        case 11: return L("difficulty.heroic_scenario");
        case 12: return L("difficulty.normal_scenario");
        case 14: return L("difficulty.normal");
        case 15: return L("difficulty.heroic");
        case 16: return L("difficulty.mythic");
        case 17: return L("difficulty.lfr");
        case 23: return L("difficulty.mythic");
        case 24: return L("difficulty.timewalking");
        default: return L("difficulty.unknown");
    }
}
