#include "UI/UISpellAssignmentPanel.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/SpellIconLoader.h"
#include "UI/Panels/Frames/UIRaidFramePanel.h"
#include "UI/NameplateSettings.h"
#include "Core/LocalizationManager.h"
#include "Core/SpellNameDatabase.h"
#include "Color/ActorColorGenerator.h"
#include "Parser/SpecializationColors.h"
#include "../../Database/SpellCastDatabase.h"
#include "../../Database/AuraDatabase.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstring>

using namespace spell_assignment;

namespace {

// Whether a location column means anything for this kind of row. Columns that
// don't apply draw a dash instead of a checkbox.
bool locationApplies(SpellDisplayLocation location, bool isAura, bool isBuff) {
    switch (location) {
        case SpellDisplayLocation::GCDTracker:
        case SpellDisplayLocation::CDGrouping1:
        case SpellDisplayLocation::PlayerHUDBigCD:
            return !isAura;
        case SpellDisplayLocation::Nameplate:
            return isAura;
        case SpellDisplayLocation::PlayerBuffs:
            return isAura && isBuff;
        case SpellDisplayLocation::PlayerDebuffs:
            return isAura && !isBuff;
        default:
            return true;
    }
}

// "Class - Spec", as shown in the spec selector
std::string specDisplayName(uint16_t specId) {
    std::string name = parser::getWowClassName(parser::getClassFromSpecId(specId));
    name += " - ";
    name += parser::getSpecName(specId);
    return name;
}

}  // namespace

UISpellAssignmentPanel::UISpellAssignmentPanel() = default;

void UISpellAssignmentPanel::clearCache() {
    specCache_.clear();
    spellToSpecs_.clear();
    auraToSpecs_.clear();
    aurasWithNonPlayerSource_.clear();
    cachedRows_.clear();
    rowCacheValid_ = false;
    lastSpellCastDb_ = nullptr;
    lastAuraDb_ = nullptr;
    cachedUniqueSpellCount_ = 0;
    cachedUniqueBuffCount_ = 0;
    cachedUniqueDebuffCount_ = 0;
}

void UISpellAssignmentPanel::render(
    const SpellCastDatabase* spellCastDb,
    const AuraDatabase* auraDb,
    const ActorColorGenerator* colorGenerator,
    const std::unordered_map<uint32_t, std::string>* spellNameMap,
    SpellIconLoader* iconLoader,
    RaidFrameConfig* raidFrameConfig,
    NameplateSettings* nameplateSettings
) {
    if (!visible_) return;

    // Rebuild cache if databases changed
    if (spellCastDb != lastSpellCastDb_ || auraDb != lastAuraDb_) {
        rebuildCache(spellCastDb, auraDb, colorGenerator, spellNameMap);
        lastSpellCastDb_ = spellCastDb;
        lastAuraDb_ = auraDb;
    }

    // Check if row cache needs rebuilding due to filter/spec/search changes
    bool searchChanged = (std::string(searchBuffer_) != lastSearchBuffer_);
    if (filterMode_ != lastFilterMode_ ||
        selectedSpecId_ != lastSelectedSpecId_ ||
        showAllSpecs_ != lastShowAllSpecs_ ||
        searchChanged) {
        rowCacheValid_ = false;
        lastFilterMode_ = filterMode_;
        lastSelectedSpecId_ = selectedSpecId_;
        lastShowAllSpecs_ = showAllSpecs_;
        if (searchChanged) {
            lastSearchBuffer_ = searchBuffer_;
            // Pre-compute lowercase search
            cachedLowerSearch_ = searchBuffer_;
            std::transform(cachedLowerSearch_.begin(), cachedLowerSearch_.end(),
                           cachedLowerSearch_.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        }
    }

    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(L("assignment.panel_title"), &visible_)) {
        ImGui::End();
        return;
    }

    // Spec selector at top
    renderSpecSelector();

    if (!showAllSpecs_ && selectedSpecId_ == 0) {
        ImGui::TextDisabled("%s", L("assignment.select_spec"));
        ImGui::End();
        return;
    }

    ImGui::Separator();

    // Icon size sliders (if configs available)
    if (raidFrameConfig || nameplateSettings) {
        ImGui::TextDisabled("%s", L("assignment.icon_sizes"));

        if (raidFrameConfig) {
            ImGui::Text("%s", L("assignment.raid_frames"));
            ImGui::SameLine(120.0f);
            ImGui::SetNextItemWidth(150.0f);
            awlui::SliderFloat("##raid_icon_size", &raidFrameConfig->spellIconSize, 12.0f, 32.0f, "%.0f px");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", L("tooltip.raid_frame_icon_size"));
            }
        }

        if (nameplateSettings) {
            ImGui::Text("%s", L("assignment.nameplates"));
            ImGui::SameLine(120.0f);
            ImGui::SetNextItemWidth(150.0f);
            awlui::SliderFloat("##nameplate_icon_size", &nameplateSettings->auraIconSize, 16.0f, 48.0f, "%.0f px");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", L("tooltip.nameplate_icon_size"));
            }
        }

        ImGui::Separator();
    }

    // Filter tabs (All / Cooldowns / Buffs / Debuffs)
    renderFilterTabs();

    // Search box
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##search", L("assignment.search_hint"), searchBuffer_, sizeof(searchBuffer_));

    ImGui::Separator();

    // Spell/aura table
    renderSpellTable(iconLoader);

    ImGui::End();
}

void UISpellAssignmentPanel::rebuildCache(
    const SpellCastDatabase* spellCastDb,
    const AuraDatabase* auraDb,
    const ActorColorGenerator* colorGenerator,
    const std::unordered_map<uint32_t, std::string>* spellNameMap
) {
    specCache_.clear();

    if (!colorGenerator) return;

    if (spellCastDb) {
        std::unordered_map<uint16_t, std::unordered_set<uint32_t>> spellsBySpec;

        for (const auto& [guid, casts] : spellCastDb->getAllActorCasts()) {
            uint16_t specId = colorGenerator->getSpecId(guid);
            if (specId == 0) continue;  // Not a player or unknown spec

            for (const auto& cast : casts) {
                spellsBySpec[specId].insert(cast.spell_id);
            }
        }

        for (const auto& [specId, spellIds] : spellsBySpec) {
            auto& cache = specCache_[specId];
            for (uint32_t spellId : spellIds) {
                std::string name = getSpellName(spellId, spellNameMap);
                cache.spells.push_back({spellId, name});
            }
            std::sort(cache.spells.begin(), cache.spells.end(),
                [](const SpellEntry& a, const SpellEntry& b) {
                    return a.spellName < b.spellName;
                });
        }
    }

    if (auraDb) {
        // Track unique auras per spec (by target)
        // Key: (specId, spellId), Value: (spellName, isBuff)
        std::unordered_map<uint16_t, std::unordered_map<uint32_t, std::pair<std::string, bool>>> aurasBySpec;

        aurasWithNonPlayerSource_.clear();

        for (const auto& [targetGuid, instances] : auraDb->getAllTargetAuras()) {
            uint16_t specId = colorGenerator->getSpecId(targetGuid);
            if (specId == 0) continue;  // Not a player or unknown spec

            for (const auto& instance : instances) {
                // Track if this aura comes from a non-player source (encounter-specific)
                uint16_t sourceSpecId = colorGenerator->getSpecId(instance.source_guid);
                if (sourceSpecId == 0) {
                    aurasWithNonPlayerSource_.insert(instance.spell_id);
                }

                auto& auraMap = aurasBySpec[specId];
                if (auraMap.find(instance.spell_id) == auraMap.end()) {
                    std::string name = getSpellName(instance.spell_id, spellNameMap);
                    if (name.empty() || name == std::to_string(instance.spell_id)) {
                        name = instance.spell_name;  // Fallback to instance name
                    }
                    bool isBuff = (instance.aura_type == AuraType::BUFF);
                    auraMap[instance.spell_id] = {name, isBuff};
                }
            }
        }

        for (const auto& [specId, auraMap] : aurasBySpec) {
            auto& cache = specCache_[specId];
            for (const auto& [spellId, info] : auraMap) {
                cache.auras.push_back({spellId, info.first, info.second});
            }
            std::sort(cache.auras.begin(), cache.auras.end(),
                [](const AuraEntry& a, const AuraEntry& b) {
                    return a.spellName < b.spellName;
                });
        }
    }

    // Build reverse lookups: spellId -> list of specs
    spellToSpecs_.clear();
    auraToSpecs_.clear();
    for (const auto& [specId, cache] : specCache_) {
        for (const auto& spell : cache.spells) {
            spellToSpecs_[spell.spellId].push_back(specId);
        }
        for (const auto& aura : cache.auras) {
            auraToSpecs_[aura.spellId].push_back(specId);
        }
    }

    // Cache unique counts for "All Specs" mode display
    std::unordered_set<uint32_t> uniqueSpells, uniqueBuffs, uniqueDebuffs;
    for (const auto& [specId, cache] : specCache_) {
        for (const auto& spell : cache.spells) {
            uniqueSpells.insert(spell.spellId);
        }
        for (const auto& aura : cache.auras) {
            if (aura.isBuff) {
                uniqueBuffs.insert(aura.spellId);
            } else {
                uniqueDebuffs.insert(aura.spellId);
            }
        }
    }
    cachedUniqueSpellCount_ = uniqueSpells.size();
    cachedUniqueBuffCount_ = uniqueBuffs.size();
    cachedUniqueDebuffCount_ = uniqueDebuffs.size();

    // Force row cache rebuild
    rowCacheValid_ = false;
}

void UISpellAssignmentPanel::renderSpecSelector() {
    ImGui::Text("%s", L("assignment.spec_label"));
    ImGui::SameLine();

    // Get specs that have data from cache, build display names
    struct SpecInfo {
        uint16_t specId;
        std::string displayName;  // "Class - Spec"
    };
    std::vector<SpecInfo> availableSpecs;
    for (const auto& [specId, cache] : specCache_) {
        if (!cache.spells.empty() || !cache.auras.empty()) {
            availableSpecs.push_back({specId, specDisplayName(specId)});
        }
    }
    std::sort(availableSpecs.begin(), availableSpecs.end(),
        [](const SpecInfo& a, const SpecInfo& b) {
            return a.displayName < b.displayName;
        });

    // Combo box for spec selection
    std::string currentSpecName;
    if (showAllSpecs_) {
        currentSpecName = L("assignment.all_specs");
    } else if (selectedSpecId_ != 0) {
        currentSpecName = specDisplayName(selectedSpecId_);
    } else {

        currentSpecName = L("assignment.select_spec");
    }

    ImGui::SetNextItemWidth(250);
    if (ImGui::BeginCombo("##specSelect", currentSpecName.c_str())) {
        // "All Specs" option at the top
        bool isAllSelected = showAllSpecs_;
        if (ImGui::Selectable(L("assignment.all_specs"), isAllSelected)) {
            showAllSpecs_ = true;
            selectedSpecId_ = 0;
        }
        if (isAllSelected) {
            ImGui::SetItemDefaultFocus();
        }

        ImGui::Separator();

        // Individual spec options
        for (const auto& spec : availableSpecs) {
            ImGui::PushID(static_cast<int>(spec.specId));
            bool isSelected = (!showAllSpecs_ && spec.specId == selectedSpecId_);
            if (ImGui::Selectable(spec.displayName.c_str(), isSelected)) {
                selectedSpecId_ = spec.specId;
                showAllSpecs_ = false;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    // Show counts (using cached values for "All Specs" mode)
    if (showAllSpecs_) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu %s, %zu %s, %zu %s)",
            cachedUniqueSpellCount_, L("assignment.cooldowns"),
            cachedUniqueBuffCount_, L("assignment.buffs"),
            cachedUniqueDebuffCount_, L("assignment.debuffs"));
    } else if (selectedSpecId_ != 0) {
        auto it = specCache_.find(selectedSpecId_);
        if (it != specCache_.end()) {
            ImGui::SameLine();
            const auto& cache = it->second;

            size_t buffCount = 0, debuffCount = 0;
            for (const auto& aura : cache.auras) {
                if (aura.isBuff) buffCount++;
                else debuffCount++;
            }

            ImGui::TextDisabled("(%zu %s, %zu %s, %zu %s)",
                cache.spells.size(), L("assignment.cooldowns"),
                buffCount, L("assignment.buffs"),
                debuffCount, L("assignment.debuffs"));
        }
    }
}

void UISpellAssignmentPanel::renderFilterTabs() {
    if (ImGui::RadioButton(L("assignment.all"), filterMode_ == FilterMode::All)) {
        filterMode_ = FilterMode::All;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(L("assignment.cooldowns"), filterMode_ == FilterMode::Cooldowns)) {
        filterMode_ = FilterMode::Cooldowns;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(L("assignment.buffs"), filterMode_ == FilterMode::Buffs)) {
        filterMode_ = FilterMode::Buffs;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(L("assignment.debuffs"), filterMode_ == FilterMode::Debuffs)) {
        filterMode_ = FilterMode::Debuffs;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(L("assignment.encounter"), filterMode_ == FilterMode::Encounter)) {
        filterMode_ = FilterMode::Encounter;
    }
}

void UISpellAssignmentPanel::renderSpellTable(SpellIconLoader* iconLoader) {
    if (!showAllSpecs_ && selectedSpecId_ == 0) return;

    // Rebuild row cache if needed
    if (!rowCacheValid_) {
        rebuildRowCache();
        rowCacheValid_ = true;
    }

    if (cachedRows_.empty()) {
        ImGui::TextDisabled("%s", L("assignment.no_spells_for_filter"));
        return;
    }

    auto& settings = SpellAssignmentSettings::instance();

    // Table with scrolling
    const float iconSize = 20.0f;

    ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
                                  ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;

    // Columns: Icon | Type | Spell Name | ID | [Locations...]
    constexpr int kFirstLocationColumn = 4;
    int numCols = kFirstLocationColumn + static_cast<int>(kNumLocations);

    if (ImGui::BeginTable("SpellAssignmentTable", numCols, tableFlags)) {
        // Setup columns with auto-sizing (WidthFixed with no size = auto-fit to content)
        ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, iconSize + 4); // Icon
        ImGui::TableSetupColumn(L("assignment.col_type"), ImGuiTableColumnFlags_WidthFixed); // Auto-fit
        ImGui::TableSetupColumn(L("assignment.col_spell"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(L("assignment.col_id"), ImGuiTableColumnFlags_WidthFixed); // Auto-fit

        // Location columns - auto-fit to header text
        for (size_t i = 0; i < kNumLocations; ++i) {
            ImGui::TableSetupColumn(getLocationShortName(kLocations[i]),
                ImGuiTableColumnFlags_WidthFixed);  // Auto-fit
        }

        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(cachedRows_.size()));

        while (clipper.Step()) {
            for (int rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd; ++rowIdx) {
                auto& row = cachedRows_[rowIdx];
                ImGui::TableNextRow();
                // Kind and id as separate scopes: spell ids run past any offset
                // that could be added to tell an aura row from a cooldown row
                ImGui::PushID(row.isAura ? 1 : 0);
                ImGui::PushID(static_cast<int>(row.spellId));

                // Icon column
                ImGui::TableSetColumnIndex(0);
                if (iconLoader) {
                    if (!iconLoader->renderSpellIcon(row.spellId, ImVec2(iconSize, iconSize))) {
                        ImGui::Dummy(ImVec2(iconSize, iconSize));
                    }
                }

                // Type column (CD / Buff / Debuff)
                ImGui::TableSetColumnIndex(1);
                if (!row.isAura) {
                    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "CD");
                } else if (row.isBuff) {
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%s", L("aura.type_buff"));
                } else {
                    ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "%s", L("aura.type_debuff"));
                }

                // Spell name column
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(row.name.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s (ID: %u)", row.name.c_str(), row.spellId);
                }

                // Spell ID column
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%u", row.spellId);

                // Location checkboxes
                bool maskChanged = false;
                bool settingsChanged = false;
                uint16_t newLocations = row.locations;
                bool nameplateToggled = false;
                bool newNameplateState = false;
                const uint16_t querySpecId = showAllSpecs_ ? row.primarySpecId : selectedSpecId_;

                for (size_t i = 0; i < kNumLocations; ++i) {
                    const SpellDisplayLocation location = kLocations[i];
                    ImGui::TableSetColumnIndex(kFirstLocationColumn + static_cast<int>(i));

                    if (!locationApplies(location, row.isAura, row.isBuff)) {
                        ImGui::TextDisabled("-");
                        continue;
                    }

                    // GCD, Player Buffs and Player Debuffs are on by default and
                    // store exclusions. Nameplates read the flat cache that is
                    // shared across specs. Everything else is a bit in the mask.
                    bool checked;
                    switch (location) {
                        case SpellDisplayLocation::GCDTracker:
                            checked = !settings.isSpellGCDExcluded(querySpecId, row.spellId);
                            break;
                        case SpellDisplayLocation::Nameplate:
                            checked = settings.isAuraEnabledForNameplate(row.spellId);
                            break;
                        case SpellDisplayLocation::PlayerBuffs:
                            checked = !settings.isAuraExcludedFromPlayerBuffs(querySpecId, row.spellId);
                            break;
                        case SpellDisplayLocation::PlayerDebuffs:
                            checked = !settings.isAuraExcludedFromPlayerDebuffs(querySpecId, row.spellId);
                            break;
                        default:
                            checked = hasFlag(row.locations, location);
                            break;
                    }

                    ImGui::PushID(static_cast<int>(i));
                    if (awlui::Checkbox("##loc", &checked)) {
                        switch (location) {
                            case SpellDisplayLocation::GCDTracker:
                                for (uint16_t specId : specsToEdit(row)) {
                                    settings.setSpellGCDExcluded(specId, row.spellId, !checked);
                                }
                                settingsChanged = true;
                                break;
                            case SpellDisplayLocation::Nameplate:
                                // Applied after the loop, to every spec with the aura
                                nameplateToggled = true;
                                newNameplateState = checked;
                                break;
                            case SpellDisplayLocation::PlayerBuffs:
                                for (uint16_t specId : specsToEdit(row)) {
                                    settings.setAuraExcludedFromPlayerBuffs(specId, row.spellId, !checked);
                                }
                                settingsChanged = true;
                                break;
                            case SpellDisplayLocation::PlayerDebuffs:
                                for (uint16_t specId : specsToEdit(row)) {
                                    settings.setAuraExcludedFromPlayerDebuffs(specId, row.spellId, !checked);
                                }
                                settingsChanged = true;
                                break;
                            default:
                                if (checked) {
                                    newLocations |= static_cast<uint16_t>(location);
                                } else {
                                    newLocations &= ~static_cast<uint16_t>(location);
                                }
                                maskChanged = true;
                                break;
                        }
                    }

                    // Tooltip on hover
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", getLocationName(location));
                    }
                    ImGui::PopID();
                }

                if (nameplateToggled) {
                    // Nameplate changes always propagate to all specs that have this aura
                    for (uint16_t specId : getSpecsWithAura(row.spellId)) {
                        uint16_t currentLocs = settings.getLocationsForAura(specId, row.spellId);
                        if (newNameplateState) {
                            currentLocs |= static_cast<uint16_t>(SpellDisplayLocation::Nameplate);
                        } else {
                            currentLocs &= ~static_cast<uint16_t>(SpellDisplayLocation::Nameplate);
                        }
                        settings.setAuraLocations(specId, row.spellId, currentLocs, row.isBuff);
                    }
                    settingsChanged = true;
                }

                if (maskChanged) {
                    for (uint16_t specId : specsToEdit(row)) {
                        if (row.isAura) {
                            settings.setAuraLocations(specId, row.spellId, newLocations, row.isBuff);
                            continue;
                        }
                        // The GCD bit is tracked as an exclusion, so it is put
                        // back from that rather than taken from the row's mask
                        uint16_t finalLocations = newLocations;
                        if (!settings.isSpellGCDExcluded(specId, row.spellId)) {
                            finalLocations |= static_cast<uint16_t>(SpellDisplayLocation::GCDTracker);
                        }
                        settings.setSpellLocations(specId, row.spellId, finalLocations);
                    }
                    settingsChanged = true;
                }

                // Saved once per row; the row cache is rebuilt next frame to
                // pick up the new state
                if (settingsChanged) {
                    settings.saveToSettings();
                    rowCacheValid_ = false;
                }

                ImGui::PopID();
                ImGui::PopID();
            }
        }
        clipper.End();

        ImGui::EndTable();
    }
}

bool UISpellAssignmentPanel::auraPassesFilter(bool isBuff, bool hasNonPlayerSource) const {
    switch (filterMode_) {
        case FilterMode::Cooldowns: return false;
        case FilterMode::Buffs:     return isBuff;
        case FilterMode::Debuffs:   return !isBuff;
        // Encounter mode only shows auras that an NPC or boss applies
        case FilterMode::Encounter: return hasNonPlayerSource;
        case FilterMode::All:       return true;
    }
    return true;
}

std::vector<uint16_t> UISpellAssignmentPanel::specsToEdit(const SpellRow& row) const {
    if (!showAllSpecs_) return {selectedSpecId_};
    return row.isAura ? getSpecsWithAura(row.spellId) : getSpecsWithSpell(row.spellId);
}

void UISpellAssignmentPanel::rebuildRowCache() {
    cachedRows_.clear();

    auto& settings = SpellAssignmentSettings::instance();

    // Cooldowns are player casts, so only the All and Cooldowns tabs list them
    const bool showCooldowns = filterMode_ == FilterMode::All ||
                               filterMode_ == FilterMode::Cooldowns;
    const auto hasNonPlayerSource = [this](uint32_t spellId) {
        return aurasWithNonPlayerSource_.count(spellId) > 0;
    };

    if (showAllSpecs_) {
        // Aggregate unique spells across all specs
        std::unordered_map<uint32_t, SpellRow> uniqueSpells;
        std::unordered_map<uint32_t, SpellRow> uniqueAuras;

        for (const auto& [specId, cache] : specCache_) {
            if (showCooldowns) {
                for (const auto& spell : cache.spells) {
                    if (!matchesSearch(spell.spellName, spell.spellId)) continue;

                    if (uniqueSpells.find(spell.spellId) == uniqueSpells.end()) {
                        // Use first spec's settings as representative
                        uint16_t locs = settings.getLocationsForSpell(specId, spell.spellId);
                        uniqueSpells[spell.spellId] = {spell.spellId, spell.spellName, false, false, locs, specId, false};
                    }
                }
            }

            for (const auto& aura : cache.auras) {
                const bool nonPlayer = hasNonPlayerSource(aura.spellId);
                if (!auraPassesFilter(aura.isBuff, nonPlayer)) continue;
                if (!matchesSearch(aura.spellName, aura.spellId)) continue;

                if (uniqueAuras.find(aura.spellId) == uniqueAuras.end()) {
                    uint16_t locs = settings.getLocationsForAura(specId, aura.spellId);
                    uniqueAuras[aura.spellId] = {aura.spellId, aura.spellName, true, aura.isBuff, locs, specId, nonPlayer};
                }
            }
        }

        cachedRows_.reserve(uniqueSpells.size() + uniqueAuras.size());
        for (const auto& [id, row] : uniqueSpells) {
            cachedRows_.push_back(row);
        }
        for (const auto& [id, row] : uniqueAuras) {
            cachedRows_.push_back(row);
        }
    } else {
        // Single spec mode
        auto cacheIt = specCache_.find(selectedSpecId_);
        if (cacheIt == specCache_.end()) return;

        const auto& cache = cacheIt->second;

        if (showCooldowns) {
            for (const auto& spell : cache.spells) {
                if (!matchesSearch(spell.spellName, spell.spellId)) continue;

                uint16_t locs = settings.getLocationsForSpell(selectedSpecId_, spell.spellId);
                cachedRows_.push_back({spell.spellId, spell.spellName, false, false, locs, selectedSpecId_, false});
            }
        }

        for (const auto& aura : cache.auras) {
            const bool nonPlayer = hasNonPlayerSource(aura.spellId);
            if (!auraPassesFilter(aura.isBuff, nonPlayer)) continue;
            if (!matchesSearch(aura.spellName, aura.spellId)) continue;

            uint16_t locs = settings.getLocationsForAura(selectedSpecId_, aura.spellId);
            cachedRows_.push_back({aura.spellId, aura.spellName, true, aura.isBuff, locs, selectedSpecId_, nonPlayer});
        }
    }

    // Sort: in Non-Player mode, sort by type (debuffs first) then name; otherwise just by name
    if (filterMode_ == FilterMode::Encounter) {
        std::sort(cachedRows_.begin(), cachedRows_.end(), [](const SpellRow& a, const SpellRow& b) {
            // Debuffs first (isBuff=false), then buffs (isBuff=true)
            if (a.isBuff != b.isBuff) {
                return !a.isBuff;  // debuffs (false) come before buffs (true)
            }
            return a.name < b.name;
        });
    } else {
        std::sort(cachedRows_.begin(), cachedRows_.end(), [](const SpellRow& a, const SpellRow& b) {
            return a.name < b.name;
        });
    }
}

std::string UISpellAssignmentPanel::getSpellName(
    uint32_t spellId,
    const std::unordered_map<uint32_t, std::string>* spellNameMap
) const {
    // SpellNameDatabase first, since it covers the most spells
    const char* dbName = SPELL_NAME(spellId);
    if (dbName && dbName[0] != '\0') {
        // SPELL_NAME returns the ID as string if not found, check if it's a real name
        std::string nameStr(dbName);
        if (nameStr != std::to_string(spellId)) {
            return nameStr;
        }
    }

    // Try spellNameMap (encounter-specific names from combat log)
    if (spellNameMap) {
        auto it = spellNameMap->find(spellId);
        if (it != spellNameMap->end() && !it->second.empty()) {
            return it->second;
        }
    }

    // Fallback to spell ID
    return std::to_string(spellId);
}

bool UISpellAssignmentPanel::matchesSearch(const std::string& spellName, uint32_t spellId) const {
    if (cachedLowerSearch_.empty()) return true;

    // Case-insensitive search in name using cached lowercase search string
    std::string lowerName;
    lowerName.reserve(spellName.size());
    for (char c : spellName) {
        lowerName += static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    }

    if (lowerName.find(cachedLowerSearch_) != std::string::npos) {
        return true;
    }

    // Also match spell ID
    if (std::to_string(spellId).find(searchBuffer_) != std::string::npos) {
        return true;
    }

    return false;
}

std::vector<uint16_t> UISpellAssignmentPanel::getSpecsWithAura(uint32_t spellId) const {
    auto it = auraToSpecs_.find(spellId);
    if (it != auraToSpecs_.end()) {
        return it->second;
    }
    return {};
}

std::vector<uint16_t> UISpellAssignmentPanel::getSpecsWithSpell(uint32_t spellId) const {
    auto it = spellToSpecs_.find(spellId);
    if (it != spellToSpecs_.end()) {
        return it->second;
    }
    return {};
}

void UISpellAssignmentPanel::setFilterSpellId(uint32_t spellId) {
    // Make panel visible
    visible_ = true;

    filterMode_ = FilterMode::All;

    snprintf(searchBuffer_, sizeof(searchBuffer_), "%u", spellId);

    // Invalidate row cache to force rebuild with new search
    rowCacheValid_ = false;
}

void UISpellAssignmentPanel::setSelectedSpec(uint16_t specId) {
    // Make panel visible
    visible_ = true;

    selectedSpecId_ = specId;
    showAllSpecs_ = false;

    // Invalidate row cache to force rebuild with new spec
    rowCacheValid_ = false;
}
