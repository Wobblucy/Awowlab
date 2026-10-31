// UIActorAppearancePanel_Individual.cpp
// Individual actor appearance override tab

#include "UI/UIActorAppearancePanel.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/ActorAppearanceSettings.h"
#include "Color/ActorColorGenerator.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <map>

// Helper to convert string to lowercase
static std::string toLowerStr(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

static bool matchesRoleName(const std::string& searchLower, ActorRole role) {
    if (searchLower.empty()) return false;

    // Map search terms to roles (case-insensitive partial matching)
    static const std::vector<std::pair<std::string, ActorRole>> roleTerms = {
        {"tank", ActorRole::Tank},
        {"healer", ActorRole::Healer},
        {"heal", ActorRole::Healer},
        {"melee", ActorRole::MeleeDPS},
        {"ranged", ActorRole::RangedDPS},
        {"dps", ActorRole::MeleeDPS},  // Will also match RangedDPS below
    };

    for (const auto& [term, termRole] : roleTerms) {
        if (term.find(searchLower) != std::string::npos || searchLower.find(term) != std::string::npos) {
            if (termRole == role) return true;
            // Special case: "dps" matches both melee and ranged
            if (term == "dps" && (role == ActorRole::MeleeDPS || role == ActorRole::RangedDPS)) {
                return true;
            }
        }
    }
    return false;
}

static bool matchesClassName(const std::string& searchLower, const std::string& className) {
    if (searchLower.empty() || className.empty()) return false;

    std::string classLower = toLowerStr(className);

    // Direct substring match
    if (classLower.find(searchLower) != std::string::npos) return true;

    // Common abbreviations and alternate names
    static const std::vector<std::pair<std::string, std::string>> classAliases = {
        {"dk", "deathknight"},
        {"death knight", "deathknight"},
        {"dh", "demonhunter"},
        {"demon hunter", "demonhunter"},
        {"lock", "warlock"},
        {"pally", "paladin"},
        {"pala", "paladin"},
        {"warr", "warrior"},
        {"sham", "shaman"},
        {"shammy", "shaman"},
        {"monk", "monk"},
        {"rogue", "rogue"},
        {"mage", "mage"},
        {"priest", "priest"},
        {"hunter", "hunter"},
        {"druid", "druid"},
        {"evoker", "evoker"},
    };

    for (const auto& [alias, fullClass] : classAliases) {
        if (searchLower.find(alias) != std::string::npos || alias.find(searchLower) != std::string::npos) {
            if (classLower == fullClass) return true;
        }
    }
    return false;
}

// Strip WCL instance suffix " (N)" from actor names for grouping
// "Bear (1)" -> "Bear", "Fractured Image (10)" -> "Fractured Image"
static std::string stripInstanceSuffix(std::string_view name) {
    if (name.size() < 4) return std::string(name);
    size_t parenClose = name.rfind(')');
    if (parenClose != name.size() - 1) return std::string(name);
    size_t parenOpen = name.rfind('(');
    if (parenOpen == std::string_view::npos || parenOpen < 2) return std::string(name);
    if (name[parenOpen - 1] != ' ') return std::string(name);
    std::string_view between = name.substr(parenOpen + 1, parenClose - parenOpen - 1);
    if (between.empty()) return std::string(name);
    for (char c : between) {
        if (c < '0' || c > '9') return std::string(name);
    }
    return std::string(name.substr(0, parenOpen - 1));
}

void UIActorAppearancePanel::renderIndividualTab(ActorAppearanceSettings& settings,
                                                  const std::shared_ptr<ActorColorGenerator>& colorGen,
                                                  const std::unordered_map<std::string_view, std::string_view>& guidToName,
                                                  const std::string& selectedPlayerGuid) {
    ImGui::TextWrapped("%s", L("appearance.individual_desc"));
    ImGui::Separator();

    // Search filter with expanded hint
    ImGui::InputTextWithHint("##search", L("appearance.search_hint_extended"), searchBuffer_, sizeof(searchBuffer_));
    ImGui::SameLine();
    awlui::Checkbox(L("appearance.only_overridden"), &showOnlyOverridden_);
    ImGui::SameLine();
    awlui::Checkbox(L("appearance.only_favorites"), &showOnlyFavorites_);

    ImGui::Spacing();

    // Show selected player first if there is one
    if (!selectedPlayerGuid.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", L("appearance.selected_actor"));

        auto nameIt = guidToName.find(selectedPlayerGuid);
        std::string displayName = (nameIt != guidToName.end()) ? std::string(nameIt->second) : selectedPlayerGuid;

        ImGui::PushID("selected");

        // Star button for favorite toggle
        bool isFav = settings.isFavorite(selectedPlayerGuid);
        if (isFav) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.0f, 1.0f));  // Gold star
        }
        if (awlui::Button(isFav ? "*" : "o", awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
            settings.toggleFavorite(selectedPlayerGuid);
            settings.saveToFile();  // Persist changes
        }
        if (isFav) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("appearance.favorite_tooltip"));
        }
        ImGui::SameLine();

        uint16_t selectedSpecId = colorGen ? colorGen->getSpecId(selectedPlayerGuid) : 0;

        bool selectedNodeOpen = ImGui::TreeNodeEx(displayName.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        // Show override badges on the same line as tree node
        ImGui::SameLine();
        renderOverrideBadges(settings, selectedPlayerGuid, displayName, selectedSpecId);

        if (selectedNodeOpen) {
            renderOverrideTrace(settings, selectedPlayerGuid, displayName, selectedSpecId);

            ImGui::Spacing();

            AppearanceOverride override;
            const AppearanceOverride* existing = settings.getGuidOverride(selectedPlayerGuid);
            if (existing) {
                override = *existing;
            }

            bool changed = false;
            changed |= renderOptionalShapeComboWithCustom("Shape##guid", override.shape, override.customShapeName);
            changed |= renderColorEditWithPresets("Color##guid", override.r, override.g, override.b, settings);
            changed |= renderScaleSlider("Scale##guid", override.scale);

            if (changed) {
                settings.setGuidOverride(selectedPlayerGuid, override);
                settings.saveToFile();  // Persist changes
            }

            if (settings.hasGuidOverride(selectedPlayerGuid) &&
                awlui::Button(L("appearance.clear_override"),
                              awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
                settings.clearGuidOverride(selectedPlayerGuid);
                settings.saveToFile();  // Persist changes
            }

            ImGui::TreePop();
        }

        // Right-click context menu for selected actor
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            contextMenuGuid_ = selectedPlayerGuid;
            contextMenuName_ = displayName;
            contextMenuSpecId_ = colorGen ? colorGen->getSpecId(selectedPlayerGuid) : 0;
            ImGui::OpenPopup("SelectedActorContextMenu");
        }

        if (ImGui::BeginPopup("SelectedActorContextMenu")) {
            if (ImGui::MenuItem(L("appearance.context_copy"))) {
                const AppearanceOverride* existing = settings.getGuidOverride(contextMenuGuid_);
                if (existing) {
                    clipboard_ = *existing;
                } else {
                    clipboard_ = AppearanceOverride{};
                }
                hasClipboard_ = true;
                lastSelectedGuid_ = contextMenuGuid_;
            }

            // Paste Appearance
            if (ImGui::MenuItem(L("appearance.context_paste"), nullptr, false, hasClipboard_)) {
                settings.setGuidOverride(contextMenuGuid_, clipboard_);
                settings.saveToFile();  // Persist changes
            }

            ImGui::Separator();

            if (ImGui::MenuItem(L("appearance.context_clear"), nullptr, false, settings.hasGuidOverride(contextMenuGuid_))) {
                settings.clearGuidOverride(contextMenuGuid_);
                settings.saveToFile();  // Persist changes
            }

            ImGui::Separator();

            // Apply to All [Class] / Apply to All [Role]
            if (contextMenuSpecId_ > 0 && colorGen) {
                std::string className = ActorAppearanceSettings::getClassFromSpecId(contextMenuSpecId_);
                ActorRole role = ActorAppearanceSettings::getRoleFromSpecId(contextMenuSpecId_);

                AppearanceOverride toApply;
                const AppearanceOverride* currentOverride = settings.getGuidOverride(contextMenuGuid_);
                if (currentOverride) {
                    toApply = *currentOverride;
                }

                char classLabel[128];
                snprintf(classLabel, sizeof(classLabel), L("appearance.context_apply_class"), className.c_str());
                if (ImGui::MenuItem(classLabel)) {
                    for (const auto& [g, n] : guidToName) {
                        std::string gStr(g);
                        uint16_t gSpecId = colorGen->getSpecId(gStr);
                        if (gSpecId > 0 && ActorAppearanceSettings::getClassFromSpecId(gSpecId) == className) {
                            settings.setGuidOverride(gStr, toApply);
                        }
                    }
                    settings.saveToFile();  // Persist changes
                }

                const char* roleName = ActorAppearanceSettings::getRoleName(role);
                char roleLabel[128];
                snprintf(roleLabel, sizeof(roleLabel), L("appearance.context_apply_role"), roleName);
                if (ImGui::MenuItem(roleLabel)) {
                    for (const auto& [g, n] : guidToName) {
                        std::string gStr(g);
                        uint16_t gSpecId = colorGen->getSpecId(gStr);
                        if (gSpecId > 0 && ActorAppearanceSettings::getRoleFromSpecId(gSpecId) == role) {
                            settings.setGuidOverride(gStr, toApply);
                        }
                    }
                    settings.saveToFile();  // Persist changes
                }
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();

        ImGui::Separator();
    }

    // Build grouped list:
    // - All players grouped under "Players"
    // - Non-players grouped by base name (stripping instance suffix)
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> nameGroups;
    std::string searchLower = toLowerStr(searchBuffer_);
    size_t totalActors = guidToName.size();
    if (!selectedPlayerGuid.empty()) totalActors--;  // Don't count selected player in total

    for (const auto& [guid, name] : guidToName) {
        std::string guidStr(guid);
        std::string nameStr(name);

        // Skip selected player (already shown above)
        if (guidStr == selectedPlayerGuid) continue;

        // Determine group name:
        // - Players (have specId) go under "Players"
        // - Non-players grouped by base name (stripped suffix)
        std::string groupName;
        bool isPlayer = false;
        if (colorGen) {
            uint16_t specId = colorGen->getSpecId(guidStr);
            isPlayer = (specId > 0);
        }

        if (isPlayer) {
            groupName = "Players";
        } else {
            groupName = stripInstanceSuffix(nameStr);
        }

        // Filter by search (match against group name OR full name OR role/class)
        if (!searchLower.empty()) {
            bool matches = false;

            std::string groupNameLower = toLowerStr(groupName);
            std::string nameLower = toLowerStr(nameStr);
            if (groupNameLower.find(searchLower) != std::string::npos ||
                nameLower.find(searchLower) != std::string::npos) {
                matches = true;
            }

            // Check role/class match (only for players)
            if (!matches && isPlayer && colorGen) {
                uint16_t specId = colorGen->getSpecId(guidStr);
                if (specId > 0) {
                    ActorRole role = ActorAppearanceSettings::getRoleFromSpecId(specId);
                    if (matchesRoleName(searchLower, role)) {
                        matches = true;
                    }
                    if (!matches) {
                        std::string className = ActorAppearanceSettings::getClassFromSpecId(specId);
                        if (matchesClassName(searchLower, className)) {
                            matches = true;
                        }
                    }
                }
            }

            if (!matches) continue;
        }

        nameGroups[groupName].push_back({guidStr, nameStr});
    }

    // Filter groups based on "only overridden" and "only favorites" filters
    // and build final sorted list
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> filteredGroups;

    for (auto& [baseName, actors] : nameGroups) {
        // Check if group passes "only overridden" filter
        if (showOnlyOverridden_) {
            bool hasAnyOverride = false;
            for (const auto& [guid, _] : actors) {
                if (settings.hasGuidOverride(guid)) {
                    hasAnyOverride = true;
                    break;
                }
            }
            if (!hasAnyOverride) continue;
        }

        // Check if group passes "only favorites" filter
        if (showOnlyFavorites_) {
            bool hasAnyFavorite = false;
            for (const auto& [guid, _] : actors) {
                if (settings.isFavorite(guid)) {
                    hasAnyFavorite = true;
                    break;
                }
            }
            if (!hasAnyFavorite) continue;
        }

        filteredGroups.push_back({baseName, std::move(actors)});
    }

    // Sort groups: favorites first, then alphabetically by base name
    std::sort(filteredGroups.begin(), filteredGroups.end(),
              [&settings](const auto& a, const auto& b) {
                  bool aHasFav = false, bHasFav = false;
                  for (const auto& [guid, _] : a.second) {
                      if (settings.isFavorite(guid)) { aHasFav = true; break; }
                  }
                  for (const auto& [guid, _] : b.second) {
                      if (settings.isFavorite(guid)) { bHasFav = true; break; }
                  }
                  if (aHasFav != bHasFav) return aHasFav > bHasFav;  // Favorites first
                  return a.first < b.first;  // Then alphabetical by base name
              });

    // Sort actors within each group: favorites first, then alphabetically by full name
    for (auto& [baseName, actors] : filteredGroups) {
        std::sort(actors.begin(), actors.end(),
                  [&settings](const auto& a, const auto& b) {
                      bool aFav = settings.isFavorite(a.first);
                      bool bFav = settings.isFavorite(b.first);
                      if (aFav != bFav) return aFav > bFav;
                      return a.second < b.second;  // Alphabetical by full name
                  });
    }

    // Count total actors in filtered groups
    size_t filteredActorCount = 0;
    for (const auto& [_, actors] : filteredGroups) {
        filteredActorCount += actors.size();
    }

    if (!searchLower.empty()) {
        ImGui::Text(L("appearance.search_results"), filteredActorCount, totalActors);
    } else {
        ImGui::Text("%s", L("appearance.all_actors"));
    }

    // Scrollable list with grouped display
    if (ImGui::BeginChild("ActorList", ImVec2(0, 250), true)) {
        for (const auto& [baseName, actors] : filteredGroups) {
            // Lambda to render a single actor row (reused for both single and grouped actors)
            auto renderActorRow = [&](const std::string& guid, const std::string& name, bool indented) {
                ImGui::PushID(guid.c_str());

                if (indented) {
                    ImGui::Indent(30.0f);
                }

                // Star button for favorite toggle
                bool isFav = settings.isFavorite(guid);
                if (isFav) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.0f, 1.0f));  // Gold star
                }
                if (awlui::Button(isFav ? "*" : "o", awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
                    settings.toggleFavorite(guid);
                    settings.saveToFile();  // Persist changes
                }
                if (isFav) {
                    ImGui::PopStyleColor();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", L("appearance.favorite_tooltip"));
                }
                ImGui::SameLine();

                bool hasOverride = settings.hasGuidOverride(guid);
                if (hasOverride) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                }

                uint16_t specId = colorGen ? colorGen->getSpecId(guid) : 0;

                bool nodeOpen = ImGui::TreeNode(name.c_str());

                // Show override badges on the same line as tree node
                ImGui::SameLine();
                renderOverrideBadges(settings, guid, name, specId);

                if (nodeOpen) {
                    if (hasOverride) {
                        ImGui::PopStyleColor();
                    }

                    renderOverrideTrace(settings, guid, name, specId);

                    ImGui::Spacing();

                    AppearanceOverride override;
                    const AppearanceOverride* existing = settings.getGuidOverride(guid);
                    if (existing) {
                        override = *existing;
                    }

                    bool changed = false;
                    changed |= renderOptionalShapeComboWithCustom("Shape##guid", override.shape, override.customShapeName);
                    changed |= renderColorEditWithPresets("Color##guid", override.r, override.g, override.b, settings);
                    changed |= renderScaleSlider("Scale##guid", override.scale);

                    if (changed) {
                        settings.setGuidOverride(guid, override);
                        settings.saveToFile();  // Persist changes
                    }

                    if (hasOverride && awlui::Button(L("appearance.clear_override"),
                                                     awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
                        settings.clearGuidOverride(guid);
                        settings.saveToFile();  // Persist changes
                    }

                    ImGui::TreePop();
                } else {
                    if (hasOverride) {
                        ImGui::PopStyleColor();
                    }
                }

                // Right-click context menu on tree node item
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    contextMenuGuid_ = guid;
                    contextMenuName_ = name;
                    contextMenuSpecId_ = colorGen ? colorGen->getSpecId(guid) : 0;
                    ImGui::OpenPopup("ActorContextMenu");
                }

                if (ImGui::BeginPopup("ActorContextMenu")) {
                    if (contextMenuGuid_ == guid) {  // Only render for the right-clicked item
                        if (ImGui::MenuItem(L("appearance.context_copy"))) {
                            const AppearanceOverride* existing = settings.getGuidOverride(contextMenuGuid_);
                            if (existing) {
                                clipboard_ = *existing;
                            } else {
                                clipboard_ = AppearanceOverride{};
                            }
                            hasClipboard_ = true;
                            lastSelectedGuid_ = contextMenuGuid_;
                        }

                        // Paste Appearance
                        if (ImGui::MenuItem(L("appearance.context_paste"), nullptr, false, hasClipboard_)) {
                            settings.setGuidOverride(contextMenuGuid_, clipboard_);
                            settings.saveToFile();  // Persist changes
                        }

                        ImGui::Separator();

                        if (ImGui::MenuItem(L("appearance.context_clear"), nullptr, false, settings.hasGuidOverride(contextMenuGuid_))) {
                            settings.clearGuidOverride(contextMenuGuid_);
                            settings.saveToFile();  // Persist changes
                        }

                        ImGui::Separator();

                        // Apply to All [Class] / Apply to All [Role]
                        if (contextMenuSpecId_ > 0 && colorGen) {
                            std::string className = ActorAppearanceSettings::getClassFromSpecId(contextMenuSpecId_);
                            ActorRole role = ActorAppearanceSettings::getRoleFromSpecId(contextMenuSpecId_);

                            AppearanceOverride toApply;
                            const AppearanceOverride* currentOverride = settings.getGuidOverride(contextMenuGuid_);
                            if (currentOverride) {
                                toApply = *currentOverride;
                            }

                            char classLabel[128];
                            snprintf(classLabel, sizeof(classLabel), L("appearance.context_apply_class"), className.c_str());
                            if (ImGui::MenuItem(classLabel)) {
                                for (const auto& [g, n] : guidToName) {
                                    std::string gStr(g);
                                    uint16_t gSpecId = colorGen->getSpecId(gStr);
                                    if (gSpecId > 0 && ActorAppearanceSettings::getClassFromSpecId(gSpecId) == className) {
                                        settings.setGuidOverride(gStr, toApply);
                                    }
                                }
                                settings.saveToFile();  // Persist changes
                            }

                            const char* roleName = ActorAppearanceSettings::getRoleName(role);
                            char roleLabel[128];
                            snprintf(roleLabel, sizeof(roleLabel), L("appearance.context_apply_role"), roleName);
                            if (ImGui::MenuItem(roleLabel)) {
                                for (const auto& [g, n] : guidToName) {
                                    std::string gStr(g);
                                    uint16_t gSpecId = colorGen->getSpecId(gStr);
                                    if (gSpecId > 0 && ActorAppearanceSettings::getRoleFromSpecId(gSpecId) == role) {
                                        settings.setGuidOverride(gStr, toApply);
                                    }
                                }
                                settings.saveToFile();  // Persist changes
                            }
                        }
                    }
                    ImGui::EndPopup();
                }

                if (indented) {
                    ImGui::Unindent(30.0f);
                }

                ImGui::PopID();
            };

            // Single actor - render directly without grouping
            if (actors.size() == 1) {
                const auto& [guid, name] = actors[0];
                renderActorRow(guid, name, false);
                ImGui::Dummy(ImVec2(0, 0));  // Force newline after single actor
                continue;
            }

            // Multiple actors - render as expandable group
            ImGui::PushID(baseName.c_str());

            // Check if any actor in group has override or is favorite
            bool groupHasOverride = false;
            bool groupHasFavorite = false;
            for (const auto& [guid, _] : actors) {
                if (settings.hasGuidOverride(guid)) groupHasOverride = true;
                if (settings.isFavorite(guid)) groupHasFavorite = true;
            }

            bool isExpanded = expandedNameGroups_.count(baseName) > 0;

            // Expand/collapse arrow button
            if (awlui::Button(isExpanded ? "v" : ">", awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
                if (isExpanded) {
                    expandedNameGroups_.erase(baseName);
                } else {
                    expandedNameGroups_.insert(baseName);
                }
            }
            ImGui::SameLine();

            // Group favorite indicator (gold star if any actor is favorite)
            if (groupHasFavorite) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.0f, 1.0f));
                ImGui::Text("*");
                ImGui::PopStyleColor();
                ImGui::SameLine();
            }

            // Group name with count - green if any actor has override
            if (groupHasOverride) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s (%zu)",
                                   baseName.c_str(), actors.size());
            } else {
                ImGui::Text("%s (%zu)", baseName.c_str(), actors.size());
            }

            // Force newline after group header (prevents horizontal wrapping)
            ImGui::Dummy(ImVec2(0, 0));

            // When expanded, show individual actors with indent
            if (isExpanded) {
                for (const auto& [guid, fullName] : actors) {
                    renderActorRow(guid, fullName, true);
                    ImGui::Dummy(ImVec2(0, 0));  // Force newline after each expanded actor
                }
            }

            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    // Summary
    ImGui::Text(L("appearance.showing_actors"),
                filteredActorCount,
                settings.getAllGuidOverrides().size());
}
