#include "UI/Panels/Debug/UIAuraDebugPanel.h"
#include "../../Database/AuraDatabase.h"
#include "Playback/PlaybackController.h"
#include "Core/NumberFormatter.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

UIAuraDebugPanel::UIAuraDebugPanel() = default;

void UIAuraDebugPanel::render(
    const AuraDatabase* auraDb,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime,
    std::shared_ptr<PlaybackController> playback
) {
    if (!visible_) return;

    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(L("panel.aura_debug"), &visible_)) {
        ImGui::End();
        return;
    }

    if (!auraDb || auraDb->empty()) {
        ImGui::TextDisabled("%s", L("aura.no_data"));
        ImGui::End();
        return;
    }

    // Filter controls
    ImGui::Text("%s", L("aura.filter_label"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##filter", filterText_, sizeof(filterText_));

    ImGui::SameLine();
    ImGui::Checkbox(L("aura.buffs"), &showBuffs_);
    ImGui::SameLine();
    ImGui::Checkbox(L("aura.debuffs"), &showDebuffs_);
    ImGui::SameLine();
    ImGui::Checkbox(L("aura.active_only"), &showActiveOnly_);

    ImGui::Separator();

    std::string filterStr(filterText_);
    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::vector<std::pair<std::string, std::string>> matchingTargets;  // GUID, Name

    for (const auto& [guid, name] : guidToName) {
        std::string guidStr(guid);
        std::string nameStr(name);

        if (!filterStr.empty()) {
            std::string lowerGuid = guidStr;
            std::string lowerName = nameStr;
            std::transform(lowerGuid.begin(), lowerGuid.end(), lowerGuid.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (lowerGuid.find(filterStr) == std::string::npos &&
                lowerName.find(filterStr) == std::string::npos) {
                continue;
            }
        }

        auto auras = auraDb->getAllAurasOnTarget(guidStr);
        if (!auras.empty()) {
            matchingTargets.emplace_back(guidStr, nameStr);
        }
    }

    std::sort(matchingTargets.begin(), matchingTargets.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });

    ImGui::Text(L("aura.targets_with_auras"), matchingTargets.size());
    ImGui::Text(L("aura.current_time"), ui::format::formatTimestamp(currentTime).c_str());
    ImGui::Separator();

    for (const auto& [targetGuid, targetName] : matchingTargets) {
        std::vector<const AuraInstance*> auras;

        if (showActiveOnly_) {
            auras = auraDb->getActiveAurasOnTarget(targetGuid, currentTime);
        } else {
            auras = auraDb->getAllAurasOnTarget(targetGuid);
        }

        // Filter by buff/debuff
        if (!showBuffs_ || !showDebuffs_) {
            auras.erase(std::remove_if(auras.begin(), auras.end(),
                [this](const AuraInstance* a) {
                    if (a->aura_type == AuraType::BUFF && !showBuffs_) return true;
                    if (a->aura_type == AuraType::DEBUFF && !showDebuffs_) return true;
                    return false;
                }), auras.end());
        }

        if (auras.empty()) continue;

        // Collapsible header for each target
        std::string headerLabel = targetName + " (" + std::to_string(auras.size()) + " auras)";
        if (ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            // Table for auras
            if (ImGui::BeginTable(("AuraTable_" + targetGuid).c_str(), 6,
                ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                ImVec2(0, 150))) {

                ImGui::TableSetupColumn(L("aura.col_type"), ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn(L("aura.col_spell"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(L("aura.col_source"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(L("aura.col_stacks"), ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn(L("aura.col_duration"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn(L("aura.col_applied"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableHeadersRow();

                for (const auto* aura : auras) {
                    ImGui::TableNextRow();

                    // Highlight active auras
                    bool isActive = aura->isActiveAt(currentTime);
                    if (isActive) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                            ImGui::GetColorU32(ImVec4(0.2f, 0.4f, 0.2f, 0.4f)));
                    }

                    // Type column (colored)
                    ImGui::TableSetColumnIndex(0);
                    if (aura->aura_type == AuraType::BUFF) {
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%s", L("aura.type_buff"));
                    } else {
                        ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "%s", L("aura.type_debuff"));
                    }

                    // Spell name and ID
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s (%u)", aura->spell_name.c_str(), aura->spell_id);

                    // Source (lookup name)
                    ImGui::TableSetColumnIndex(2);
                    auto sourceIt = guidToName.find(aura->source_guid);
                    if (sourceIt != guidToName.end()) {
                        ImGui::TextUnformatted(std::string(sourceIt->second).c_str());
                    } else {
                        ImGui::TextDisabled("%s", L("deaths.unknown"));
                    }

                    // Stacks
                    ImGui::TableSetColumnIndex(3);
                    if (aura->max_stacks > 1) {
                        ImGui::Text("%u/%u", aura->current_stacks, aura->max_stacks);
                    } else {
                        ImGui::Text("%u", aura->current_stacks);
                    }

                    // Duration
                    ImGui::TableSetColumnIndex(4);
                    if (aura->removed_at_ms == UINT32_MAX) {
                        ImGui::TextDisabled("%s", L("aura.status_active"));
                    } else {
                        ImGui::Text("%s", formatDuration(aura->getDurationMs()).c_str());
                    }

                    // Applied timestamp (clickable to jump)
                    ImGui::TableSetColumnIndex(5);
                    ImGui::PushID(aura);
                    if (ImGui::Selectable(ui::format::formatTimestamp(aura->applied_at_ms).c_str(), false,
                        ImGuiSelectableFlags_SpanAllColumns)) {
                        if (playback) {
                            playback->setCurrentTime(aura->applied_at_ms);
                            playback->pause();
                        }
                    }
                    ImGui::PopID();
                }

                ImGui::EndTable();
            }
        }
    }

    ImGui::End();
}

std::string UIAuraDebugPanel::formatDuration(uint32_t duration_ms) {
    float seconds = duration_ms / 1000.0f;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << seconds << "s";
    return oss.str();
}
