// UIActorAppearancePanel_Enemies.cpp
// Enemies tab for hostile/neutral creature appearance settings

#include "UI/UIActorAppearancePanel.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/ActorAppearanceSettings.h"
#include "Color/ActorColorGenerator.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <optional>

void UIActorAppearancePanel::renderEnemiesTab(ActorAppearanceSettings& settings,
                                               const std::shared_ptr<ActorColorGenerator>& colorGen,
                                               [[maybe_unused]] const std::unordered_map<std::string_view, std::string_view>& guidToName) {
    ImGui::TextWrapped("%s", L("appearance.enemies_desc"));
    ImGui::Separator();

    if (!colorGen) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", L("status.no_actor_data"));
        return;
    }

    const auto& nameGroups = colorGen->getNonFriendlyNameGroups();
    if (nameGroups.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", L("appearance.no_enemies"));
        return;
    }

    // Search filter
    ImGui::InputTextWithHint("##enemySearch", L("appearance.search_hint"), enemiesSearchBuffer_, sizeof(enemiesSearchBuffer_));

    ImGui::Spacing();

    std::vector<std::pair<std::string, size_t>> filteredNames;  // name, count
    std::string searchLower = enemiesSearchBuffer_;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    size_t totalEnemyTypes = nameGroups.size();

    for (const auto& [name, guids] : nameGroups) {
        std::string nameStr(name);
        if (!searchLower.empty()) {
            std::string nameLower = nameStr;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (nameLower.find(searchLower) == std::string::npos) {
                continue;
            }
        }
        filteredNames.push_back({nameStr, guids.size()});
    }

    std::sort(filteredNames.begin(), filteredNames.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    if (!searchLower.empty()) {
        ImGui::Text(L("appearance.enemy_search_results"), filteredNames.size(), totalEnemyTypes);
        ImGui::Spacing();
    }

    // Scrollable list
    if (ImGui::BeginChild("EnemyList", ImVec2(0, 280), true)) {
        for (const auto& [name, count] : filteredNames) {
            ImGui::PushID(name.c_str());

            // Get current name group override (if any)
            bool hasNameOverride = settings.hasNameGroupOverride(name);

            // Get base color for this name (either override or generated)
            ActorColor displayColor;
            // Copied: "Reset to default" and "Clear" below erase the map entry
            // this would otherwise point into, in the same frame it is read
            std::optional<AppearanceOverride> nameOverride;
            if (const AppearanceOverride* stored = settings.getNameGroupOverride(name)) {
                nameOverride = *stored;
            }
            if (nameOverride && nameOverride->r.has_value()) {
                displayColor = ActorColor{nameOverride->r.value(), nameOverride->g.value(), nameOverride->b.value()};
            } else {
                displayColor = colorGen->getColorForName(name);
            }

            bool isExpanded = expandedNameGroups_.count(name) > 0;

            // Row: color indicator + name (count) + expand button
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(displayColor.r, displayColor.g, displayColor.b, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(displayColor.r * 1.1f, displayColor.g * 1.1f, displayColor.b * 1.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(displayColor.r * 0.9f, displayColor.g * 0.9f, displayColor.b * 0.9f, 1.0f));

            // Color button (opens color picker on left-click)
            if (ImGui::Button("##color", ImVec2(20, 20))) {
                ImGui::OpenPopup("ColorPicker");
            }
            ImGui::PopStyleColor(3);

            // Color picker popup (opens on left-click or right-click)
            if (ImGui::BeginPopup("ColorPicker")) {
                float color[3] = { displayColor.r, displayColor.g, displayColor.b };
                if (ImGui::ColorPicker3("##picker", color)) {
                    AppearanceOverride override;
                    if (nameOverride) {
                        override = *nameOverride;
                    }
                    override.r = color[0];
                    override.g = color[1];
                    override.b = color[2];
                    settings.setNameGroupOverride(name, override);
                    settings.saveToFile();  // Persist changes
                }
                if (hasNameOverride && awlui::Button(L("appearance.reset_default"),
                                                     awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
                    settings.clearNameGroupOverride(name);
                    settings.saveToFile();  // Persist changes
                }
                ImGui::EndPopup();
            }

            ImGui::SameLine();

            // Expand/collapse arrow
            const char* arrowLabel = isExpanded ? "v" : ">";
            if (awlui::Button(arrowLabel, awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
                if (isExpanded) {
                    expandedNameGroups_.erase(name);
                } else {
                    expandedNameGroups_.insert(name);
                }
            }

            ImGui::SameLine();

            // Name with count and override indicator
            if (hasNameOverride) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s (%zu)", name.c_str(), count);
            } else {
                ImGui::Text("%s (%zu)", name.c_str(), count);
            }

            // Show [N] badge if name group has override
            if (hasNameOverride) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), "%s", L("appearance.badge_name"));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", L("appearance.badge_name_tooltip"));
                }
            }

            // Right-click on name opens context menu
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                contextMenuName_ = name;
                contextMenuGuid_.clear();  // Mark as name group context menu (not individual GUID)
                ImGui::OpenPopup("EnemyNameContextMenu");
            }

            // Context menu for name group
            if (ImGui::BeginPopup("EnemyNameContextMenu")) {
                if (contextMenuName_ == name && contextMenuGuid_.empty()) {
                    if (ImGui::MenuItem(L("appearance.context_copy"))) {
                        if (nameOverride) {
                            clipboard_ = *nameOverride;
                        } else {
                            clipboard_ = AppearanceOverride{};
                        }
                        hasClipboard_ = true;
                    }

                    // Paste Appearance
                    if (ImGui::MenuItem(L("appearance.context_paste"), nullptr, false, hasClipboard_)) {
                        settings.setNameGroupOverride(name, clipboard_);
                        settings.saveToFile();  // Persist changes
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem(L("appearance.context_clear"), nullptr, false, hasNameOverride)) {
                        settings.clearNameGroupOverride(name);
                        settings.saveToFile();  // Persist changes
                    }
                }
                ImGui::EndPopup();
            }

            // Inline shape and scale controls
            ImGui::SameLine();

            // Build override struct for modifications. Fetched again because
            // the popups above may have changed or cleared it this frame.
            AppearanceOverride currentOverride;
            if (const AppearanceOverride* stored = settings.getNameGroupOverride(name)) {
                currentOverride = *stored;
            }

            // Compact shape dropdown
            {
                const char* shapeNamesWithDefault[] = {
                    L("shape.default"), L("shape.sphere"), L("shape.cube"), L("shape.shield"),
                    L("shape.medic_cross"), L("shape.sword"), L("shape.bow"), L("shape.cone"),
                    L("shape.cylinder"), L("shape.diamond"), L("shape.pyramid"), L("shape.star"),
                    L("shape.classicon")
                };
                ImGui::SetNextItemWidth(90.0f);
                int currentShape = currentOverride.shape.has_value() ? (static_cast<int>(currentOverride.shape.value()) + 1) : 0;
                if (awlui::Combo("##shape", &currentShape, shapeNamesWithDefault, IM_ARRAYSIZE(shapeNamesWithDefault))) {
                    if (currentShape == 0) {
                        currentOverride.shape.reset();
                    } else {
                        currentOverride.shape = static_cast<ActorShape>(currentShape - 1);
                    }
                    settings.setNameGroupOverride(name, currentOverride);
                    settings.saveToFile();  // Persist changes
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", L("appearance.shape_tooltip"));
                }
            }

            ImGui::SameLine();

            // Compact scale slider
            {
                ImGui::SetNextItemWidth(80.0f);
                float scaleVal = currentOverride.scale.value_or(1.0f);
                if (awlui::SliderFloat("##scale", &scaleVal, 0.5f, 5.0f, "%.1fx")) {
                    currentOverride.scale = scaleVal;
                    settings.setNameGroupOverride(name, currentOverride);
                    settings.saveToFile();  // Persist changes
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", L("appearance.scale_tooltip"));
                }
            }

            ImGui::SameLine();

            // Clear button (only if override exists)
            if (hasNameOverride) {
                if (awlui::Button("X##clear", awlui::ButtonVariant::Danger, awlui::ButtonSize::Sm)) {
                    settings.clearNameGroupOverride(name);
                    settings.saveToFile();  // Persist changes
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", L("appearance.clear_override"));
                }
            }

            // Force newline after inline controls (prevents horizontal wrapping)
            ImGui::Dummy(ImVec2(0, 0));

            // Expanded: show individual GUIDs
            if (isExpanded) {
                ImGui::Indent(30.0f);

                const std::vector<std::string_view>* guids = colorGen->getGuidsForName(name);
                if (guids) {
                    for (const auto& guid : *guids) {
                        std::string guidStr(guid);
                        ImGui::PushID(guidStr.c_str());

                        bool hasGuidOverride = settings.hasGuidOverride(guidStr);

                        // Get GUID-specific color
                        // Default to name group color (displayColor) since that's what renders
                        // Only show different color if there's a GUID-specific override
                        ActorColor guidColor;
                        const AppearanceOverride* guidOverride = settings.getGuidOverride(guidStr);
                        if (guidOverride && guidOverride->r.has_value()) {
                            guidColor = ActorColor{guidOverride->r.value(), guidOverride->g.value(), guidOverride->b.value()};
                        } else {
                            guidColor = displayColor;  // Use name group color (matches rendering)
                        }

                        // Small color button for GUID
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(guidColor.r, guidColor.g, guidColor.b, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(guidColor.r * 1.1f, guidColor.g * 1.1f, guidColor.b * 1.1f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(guidColor.r * 0.9f, guidColor.g * 0.9f, guidColor.b * 0.9f, 1.0f));

                        if (ImGui::Button("##guidcolor", ImVec2(16, 16))) {
                            ImGui::OpenPopup("GuidColorPicker");
                        }
                        ImGui::PopStyleColor(3);

                        // GUID color picker popup (opens on left-click)
                        if (ImGui::BeginPopup("GuidColorPicker")) {
                            float color[3] = { guidColor.r, guidColor.g, guidColor.b };
                            if (ImGui::ColorPicker3("##guidpicker", color)) {
                                AppearanceOverride override;
                                if (guidOverride) {
                                    override = *guidOverride;
                                }
                                override.r = color[0];
                                override.g = color[1];
                                override.b = color[2];
                                settings.setGuidOverride(guidStr, override);
                                settings.saveToFile();  // Persist changes
                            }
                            if (hasGuidOverride && awlui::Button(L("appearance.reset_default"),
                                                                 awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
                                settings.clearGuidOverride(guidStr);
                                settings.saveToFile();  // Persist changes
                            }
                            ImGui::EndPopup();
                        }

                        ImGui::SameLine();

                        // Extract short GUID identifier (last part after last dash)
                        std::string shortGuid = guidStr;
                        size_t lastDash = guidStr.rfind('-');
                        if (lastDash != std::string::npos && lastDash + 1 < guidStr.length()) {
                            shortGuid = guidStr.substr(lastDash + 1);
                        }

                        if (hasGuidOverride) {
                            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", shortGuid.c_str());
                        } else {
                            ImGui::TextDisabled("%s", shortGuid.c_str());
                        }

                        // Tooltip with full GUID
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", guidStr.c_str());
                        }

                        // Show [G] badge if GUID has override
                        if (hasGuidOverride) {
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "%s", L("appearance.badge_guid"));
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", L("appearance.badge_guid_tooltip"));
                            }
                        }

                        // Right-click opens context menu
                        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                            contextMenuGuid_ = guidStr;
                            contextMenuName_ = name;
                            ImGui::OpenPopup("EnemyGuidContextMenu");
                        }

                        // Context menu for individual enemy GUID
                        if (ImGui::BeginPopup("EnemyGuidContextMenu")) {
                            if (contextMenuGuid_ == guidStr) {
                                if (ImGui::MenuItem(L("appearance.context_copy"))) {
                                    if (guidOverride) {
                                        clipboard_ = *guidOverride;
                                    } else {
                                        clipboard_ = AppearanceOverride{};
                                    }
                                    hasClipboard_ = true;
                                }

                                // Paste Appearance
                                if (ImGui::MenuItem(L("appearance.context_paste"), nullptr, false, hasClipboard_)) {
                                    settings.setGuidOverride(contextMenuGuid_, clipboard_);
                                    settings.saveToFile();  // Persist changes
                                }

                                ImGui::Separator();

                                if (ImGui::MenuItem(L("appearance.context_clear"), nullptr, false, hasGuidOverride)) {
                                    settings.clearGuidOverride(contextMenuGuid_);
                                    settings.saveToFile();  // Persist changes
                                }
                            }
                            ImGui::EndPopup();
                        }

                        ImGui::PopID();
                    }
                }

                ImGui::Unindent(30.0f);
            }

            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    // Summary
    size_t totalOverrides = settings.getAllNameGroupOverrides().size();
    ImGui::Text(L("appearance.showing_enemies"),
                filteredNames.size(), totalOverrides);
}
