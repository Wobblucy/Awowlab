#include "UI/Panels/Navigation/UICachedFightsPanel.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/ClassIconCache.h"
#include "Storage/ResponseCache.h"
#include "Models/Report.h"
#include "Models/Actor.h"
#include "Core/LocalizationManager.h"
#include "Core/UIUtils.h"
#include "Core/BrowserHelper.h"
#include "Adapter/UrlParser.h"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <map>
#include <sstream>

namespace {

/**
 * Build class composition data from report actors.
 * Returns first player name and vector of (class name, count) pairs.
 */
std::pair<std::string, std::vector<std::pair<std::string, int>>> buildClassComposition(
    const std::vector<warcraftlogs::Actor>& actors) {

    std::map<std::string, int> classCounts;
    std::string firstPlayerName;

    for (const auto& actor : actors) {
        if (actor.type == warcraftlogs::ActorType::Player) {
            if (firstPlayerName.empty()) {
                firstPlayerName = actor.name;
            }
            if (!actor.subType.empty()) {
                classCounts[actor.subType]++;
            }
        }
    }

    // Convert map to vector of pairs (sorted alphabetically by class name)
    std::vector<std::pair<std::string, int>> composition;
    composition.reserve(classCounts.size());
    for (const auto& [className, count] : classCounts) {
        composition.emplace_back(className, count);
    }

    return {firstPlayerName, composition};
}

// WarcraftLogs has several ids per difficulty (current and legacy). The
// filter combo offers one entry per difficulty, stored as the current id;
// this maps any id to that entry, or returns it unchanged if it has none.
int32_t difficultyFilterGroup(int32_t difficulty) {
    switch (difficulty) {
        case 1: case 7: case 17:  return 1;   // LFR
        case 3: case 14:          return 3;   // Normal
        case 2: case 4: case 15:  return 4;   // Heroic
        case 5: case 16: case 23: return 5;   // Mythic
        case 8: case 10:          return 10;  // M+
        default:                  return difficulty;
    }
}

// Unknown dates (0) sort after every known one
bool startsBefore(const CachedFightInfo& a, const CachedFightInfo& b, bool newestFirst) {
    if (a.startTimeMs == 0) return false;
    if (b.startTimeMs == 0) return true;
    return newestFirst ? a.startTimeMs > b.startTimeMs : a.startTimeMs < b.startTimeMs;
}

} // anonymous namespace

std::string CachedFightInfo::difficultyName() const {
    // For M+, show keystone level if available
    if (keystoneLevel > 0) {
        return "+" + std::to_string(keystoneLevel);
    }
    // WarcraftLogs API difficulty values
    switch (difficulty) {
        case 1:  return "LFR";
        case 3:  return "Normal";
        case 4:  return "Heroic";
        case 5:  return "Mythic";
        case 10: return "M+";
        // Legacy/other values
        case 2:  return "Heroic";
        case 6:  return "25 Heroic";
        case 7:  return "LFR";
        case 8:  return "M+ Keystone";
        case 9:  return "40 Player";
        case 14: return "Normal";
        case 15: return "Heroic";
        case 16: return "Mythic";
        case 17: return "LFR";
        case 23: return "Mythic";
        case 24: return "Timewalking";
        default: return "";
    }
}

UICachedFightsPanel::UICachedFightsPanel() = default;

void UICachedFightsPanel::setCache(warcraftlogs::ResponseCache* cache) {
    cache_ = cache;
    if (cache_) {
        buildGroupedList();
    }
}

void UICachedFightsPanel::refreshCacheList() {
    buildGroupedList();
}

void UICachedFightsPanel::open() {
    isOpen_ = true;
    popupOpened_ = false;  // Reset so OpenPopup is called on first render
    clearSelection();
    refreshCacheList();
}

void UICachedFightsPanel::reset() {
    isOpen_ = false;
    popupOpened_ = false;
    groups_.clear();
    clearSelection();
}

void UICachedFightsPanel::clearSelection() {
    selection_ = CachedFightSelection{};
    clearHighlightedRow();
    clearPendingDelete();
}

void UICachedFightsPanel::clearHighlightedRow() {
    selectedGroupIndex_ = -1;
    selectedSubgroupIndex_ = -1;
    selectedFightIndex_ = -1;
}

void UICachedFightsPanel::clearPendingDelete() {
    showDeleteConfirm_ = false;
    deletePendingFightId_ = -1;
    deletePendingReportCode_.clear();
    deleteTargetDescription_.clear();
}

void UICachedFightsPanel::chooseFight(const CachedFightInfo& fight) {
    selection_.selected = true;
    selection_.reportCode = fight.reportCode;
    selection_.fightId = fight.fightId;
    ImGui::CloseCurrentPopup();
}

size_t UICachedFightsPanel::getCachedFightCount() const {
    size_t count = 0;
    for (const auto& group : groups_) {
        for (const auto& subgroup : group.subgroups) {
            count += subgroup.fights.size();
        }
    }
    return count;
}

std::vector<CachedFightInfo> UICachedFightsPanel::getAllFights() const {
    std::vector<CachedFightInfo> fights;
    fights.reserve(getCachedFightCount());
    for (const auto& group : groups_) {
        for (const auto& subgroup : group.subgroups) {
            fights.insert(fights.end(), subgroup.fights.begin(),
                          subgroup.fights.end());
        }
    }
    return fights;
}

std::unordered_map<uint32_t, std::string>
UICachedFightsPanel::getEncounterNames() const {
    std::unordered_map<uint32_t, std::string> names;
    for (const auto& group : groups_) {
        for (const auto& subgroup : group.subgroups) {
            for (const auto& fight : subgroup.fights) {
                if (fight.encounterID <= 0) continue;
                // First name wins; every fight of one encounter shares it.
                names.try_emplace(static_cast<uint32_t>(fight.encounterID),
                                  subgroup.encounterName);
            }
        }
    }
    return names;
}

void UICachedFightsPanel::buildGroupedList() {
    groups_.clear();

    if (!cache_) {
        return;
    }

    std::vector<std::string> reportCodes = cache_->getCachedReportCodes();

    // Temporary storage for fights by group type, then by encounter name
    std::map<std::string, std::vector<CachedFightInfo>> mythicPlusEncounters;
    std::map<std::string, std::vector<CachedFightInfo>> raidEncounters;
    std::map<std::string, std::vector<CachedFightInfo>> otherEncounters;

    for (const auto& code : reportCodes) {
        auto reportOpt = cache_->loadReport(code);
        if (!reportOpt) {
            continue;
        }

        const auto& report = *reportOpt;

        // Build class composition once per report (same for all fights in report)
        auto [firstPlayer, classComp] = buildClassComposition(report.actors);

        for (const auto& fight : report.fights) {
            CachedFightInfo info;
            info.reportCode = code;
            info.fightId = fight.id;
            info.fightName = fight.name;
            info.encounterID = fight.encounterID;
            info.difficulty = fight.difficulty;
            info.keystoneLevel = fight.keystoneLevel;
            info.durationMs = fight.duration();
            info.fullyLoaded = cache_->hasCachedFightEvents(code, fight.id);
            info.firstPlayerName = firstPlayer;
            info.classComposition = classComp;

            // Compute absolute start time from report start + fight relative start
            // report.startTime is Unix epoch ms, fight.startTime is relative to report start
            if (report.startTime > 0) {
                info.startTimeMs = report.startTime + fight.startTime;
            } else {
                info.startTimeMs = 0;  // Unknown for legacy cached reports
            }

            // Filter based on toggle - skip metadata-only unless user enabled showing them
            if (!info.fullyLoaded && !showMetadataOnly_) {
                continue;
            }

            if (difficultyFilter_ != -1) {
                // A keystone level marks M+ even when the difficulty id doesn't
                bool isKeystoneRun = (difficultyFilter_ == 10 && info.keystoneLevel > 0);
                if (!isKeystoneRun && difficultyFilterGroup(info.difficulty) != difficultyFilter_) {
                    continue;
                }
            }

            // Categorize the fight by type and encounter name
            if (info.isMythicPlus()) {
                mythicPlusEncounters[info.fightName].push_back(std::move(info));
            } else if (info.isBossEncounter()) {
                raidEncounters[info.fightName].push_back(std::move(info));
            } else {
                otherEncounters[info.fightName].push_back(std::move(info));
            }
        }
    }

    // Helper lambda to convert map to subgroups vector
    auto mapToSubgroups = [this](std::map<std::string, std::vector<CachedFightInfo>>& encounterMap) {
        std::vector<EncounterSubgroup> subgroups;
        for (auto& [name, fights] : encounterMap) {
            sortFightsInGroup(fights);
            subgroups.push_back({name, std::move(fights)});
        }
        // Sort subgroups alphabetically by encounter name
        std::sort(subgroups.begin(), subgroups.end(),
            [](const EncounterSubgroup& a, const EncounterSubgroup& b) {
                return a.encounterName < b.encounterName;
            });
        return subgroups;
    };

    if (!mythicPlusEncounters.empty()) {
        groups_.push_back({
            FightGroupType::MythicPlus,
            L("cache.mythic_plus"),
            mapToSubgroups(mythicPlusEncounters)
        });
    }

    if (!raidEncounters.empty()) {
        groups_.push_back({
            FightGroupType::RaidEncounter,
            L("cache.raid_encounters"),
            mapToSubgroups(raidEncounters)
        });
    }

    if (!otherEncounters.empty()) {
        groups_.push_back({
            FightGroupType::Other,
            L("cache.other"),
            mapToSubgroups(otherEncounters)
        });
    }
}

void UICachedFightsPanel::sortFightsInGroup(std::vector<CachedFightInfo>& fights) const {
    switch (sortMode_) {
        case CachedFightSortMode::DateNewest:
            std::sort(fights.begin(), fights.end(), [](const CachedFightInfo& a, const CachedFightInfo& b) {
                return startsBefore(a, b, true);
            });
            break;

        case CachedFightSortMode::DateOldest:
            std::sort(fights.begin(), fights.end(), [](const CachedFightInfo& a, const CachedFightInfo& b) {
                return startsBefore(a, b, false);
            });
            break;

        case CachedFightSortMode::NameAZ:
            std::sort(fights.begin(), fights.end(), [](const CachedFightInfo& a, const CachedFightInfo& b) {
                return a.fightName < b.fightName;
            });
            break;

        case CachedFightSortMode::NameZA:
            std::sort(fights.begin(), fights.end(), [](const CachedFightInfo& a, const CachedFightInfo& b) {
                return a.fightName > b.fightName;
            });
            break;
    }
}

// Helper to format Unix epoch ms to a date string like "Dec 15" or "12/15/24"
static std::string formatDate(int64_t epochMs) {
    if (epochMs <= 0) {
        return "Unknown";
    }

    std::time_t epochSec = static_cast<std::time_t>(epochMs / 1000);
    std::tm* tm = std::localtime(&epochSec);
    if (!tm) {
        return "Unknown";
    }

    // Format as "Mon DD" (e.g., "Dec 15")
    static const char* months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%s %d", months[tm->tm_mon], tm->tm_mday);
    return std::string(buf);
}

bool UICachedFightsPanel::render(ClassIconCache* iconCache) {
    if (!isOpen_) {
        return false;
    }

    if (selection_.selected || selection_.cancelled) {
        isOpen_ = false;
        popupOpened_ = false;  // Reset for next open
        return true;
    }

    // Only call OpenPopup once when first opening, not every frame
    if (!popupOpened_) {
        ImGui::OpenPopup("Cached Fights");
        popupOpened_ = true;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(1400, 1000), ImGuiCond_Appearing);

    bool isOpen = true;
    if (ImGui::BeginPopupModal("Cached Fights", &isOpen, ImGuiWindowFlags_NoResize)) {
        // Header with title and count
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
        ImGui::TextColored(ImVec4(0.95f, 0.95f, 1.0f, 1.0f), "%s", L("cache.title"));
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), L("cache.cached_count"), getCachedFightCount());

        ImGui::Spacing();
        ImGui::Spacing();

        // Controls row - all on one line
        // Sort dropdown
        ImGui::Text("%s:", L("cache.sort_by"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        const char* sortOptions[] = {
            L("cache.sort_date_newest"),
            L("cache.sort_date_oldest"),
            L("cache.sort_name_az"),
            L("cache.sort_name_za")
        };
        int sortIndex = static_cast<int>(sortMode_);
        if (awlui::Combo("##SortMode", &sortIndex, sortOptions, 4)) {
            sortMode_ = static_cast<CachedFightSortMode>(sortIndex);
            refreshCacheList();
        }

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(30, 0));
        ImGui::SameLine();

        // Difficulty filter dropdown
        ImGui::Text("%s:", L("cache.filter_difficulty"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        const char* difficultyOptions[] = {
            L("cache.filter_all"),  // -1: All
            "LFR",                  // Various LFR values
            "Normal",               // Various Normal values
            "Heroic",               // Various Heroic values
            "Mythic",               // Various Mythic values
            "M+"                    // M+ dungeons
        };
        // Combo entries after "All", by the id difficultyFilterGroup() maps to
        constexpr int32_t filterIds[] = {1, 3, 4, 5, 10};
        int diffIndex = 0;
        for (int i = 0; i < 5; ++i) {
            if (difficultyFilter_ != -1 && difficultyFilterGroup(difficultyFilter_) == filterIds[i]) {
                diffIndex = i + 1;
            }
        }

        if (awlui::Combo("##DiffFilter", &diffIndex, difficultyOptions, 6)) {
            difficultyFilter_ = (diffIndex >= 1 && diffIndex <= 5) ? filterIds[diffIndex - 1] : -1;
            refreshCacheList();
        }

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(30, 0));
        ImGui::SameLine();

        if (awlui::Checkbox(L("cache.show_all"), &showMetadataOnly_)) {
            refreshCacheList();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("cache.show_all_tooltip"));
        }

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(30, 0));
        ImGui::SameLine();

        // Refresh button
        if (awlui::Button(L("btn.refresh"), awlui::ButtonVariant::Secondary)) {
            refreshCacheList();
        }

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (groups_.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", L("cache.no_fights"));
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "%s", L("cache.load_hint"));
        } else {
            // Scrollable content area
            if (ImGui::BeginChild("FightsList", ImVec2(0, -1), false)) {
                int groupIdx = 0;
                for (const auto& group : groups_) {
                    renderGroup(group, groupIdx, iconCache);
                    groupIdx++;
                }
            }
            ImGui::EndChild();
        }

        if (!isOpen) {
            selection_.cancelled = true;
            ImGui::CloseCurrentPopup();
        }

        if (showDeleteConfirm_) {
            ImGui::OpenPopup(L("cache.delete_confirm_title"));
        }

        ImVec2 confirmCenter = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(confirmCenter, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal(L("cache.delete_confirm_title"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", L("cache.delete_confirm_msg"));
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", deleteTargetDescription_.c_str());
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.5f, 0.5f, 1.0f), "%s", L("cache.delete_warning"));
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (awlui::Button(L("cache.delete"), awlui::ButtonVariant::Danger,
                              awlui::ButtonSize::Md, ImVec2(100, 0))) {
                if (cache_ && deletePendingFightId_ >= 0) {
                    cache_->clearFightCache(deletePendingReportCode_, deletePendingFightId_);
                }
                clearPendingDelete();
                // The row indices shift once the list is rebuilt
                clearHighlightedRow();
                ImGui::CloseCurrentPopup();
                refreshCacheList();
            }
            ImGui::SameLine();
            if (awlui::Button(L("btn.cancel"), awlui::ButtonVariant::Ghost,
                              awlui::ButtonSize::Md, ImVec2(100, 0))) {
                clearPendingDelete();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::EndPopup();
    }

    return selection_.selected || selection_.cancelled;
}

void UICachedFightsPanel::renderGroup(const CachedFightGroup& group, int groupIndex, ClassIconCache* iconCache) {
    // Group header with icon based on type
    const char* icon = "";
    ImVec4 headerColor;
    switch (group.type) {
        case FightGroupType::MythicPlus:
            icon = "[M+]";
            headerColor = ImVec4(0.6f, 0.4f, 0.9f, 1.0f);  // Purple for M+
            break;
        case FightGroupType::RaidEncounter:
            icon = "[Raid]";
            headerColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);  // Gold for raids
            break;
        default:
            icon = "[Other]";
            headerColor = ImVec4(0.6f, 0.6f, 0.7f, 1.0f);  // Gray for other
            break;
    }

    // Count total fights across all subgroups
    size_t totalFights = 0;
    for (const auto& subgroup : group.subgroups) {
        totalFights += subgroup.fights.size();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, headerColor);

    // Collapsible header - default open
    bool headerOpen = ImGui::TreeNodeEx(
        (std::string(icon) + " " + group.label + " (" + std::to_string(totalFights) + ")").c_str(),
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth
    );

    ImGui::PopStyleColor();

    if (headerOpen) {
        int subgroupIdx = 0;
        for (const auto& subgroup : group.subgroups) {
            renderSubgroup(subgroup, groupIndex, subgroupIdx, iconCache);
            subgroupIdx++;
        }

        ImGui::TreePop();
    }

    ImGui::Spacing();
}

void UICachedFightsPanel::renderSubgroup(const EncounterSubgroup& subgroup, int groupIndex, int subgroupIndex, ClassIconCache* iconCache) {
    // Subgroup header with encounter name and fight count
    std::string subgroupLabel = subgroup.encounterName + " (" + std::to_string(subgroup.fights.size()) + ")";

    // Color the encounter/dungeon name
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.8f, 0.6f, 1.0f));  // Gold/tan color
    bool subgroupOpen = ImGui::TreeNodeEx(
        ("##subgroup_" + std::to_string(groupIndex) + "_" + std::to_string(subgroupIndex)).c_str(),
        ImGuiTreeNodeFlags_SpanAvailWidth,  // Collapsed by default
        "%s", subgroupLabel.c_str()
    );
    ImGui::PopStyleColor();

    if (subgroupOpen) {
        // Table for fights in this subgroup
        std::string tableId = "FightTable_" + std::to_string(groupIndex) + "_" + std::to_string(subgroupIndex);
        if (ImGui::BeginTable(tableId.c_str(), 7,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {

            ImGui::TableSetupColumn(L("cache.col_date"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(L("encounter.col_difficulty"), ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn(L("encounter.col_duration"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(L("cache.col_report"), ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn(L("cache.col_composition"), ImGuiTableColumnFlags_WidthStretch);  // Composition stretches
            ImGui::TableSetupColumn(L("cache.col_cached"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn(L("cache.col_actions"), ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableHeadersRow();

            int fightIdx = 0;
            for (const auto& fight : subgroup.fights) {
                ImGui::TableNextRow();

                // Date column (selectable for row selection)
                ImGui::TableSetColumnIndex(0);

                bool isSelected = (selectedGroupIndex_ == groupIndex &&
                                   selectedSubgroupIndex_ == subgroupIndex &&
                                   selectedFightIndex_ == fightIdx);
                std::string rowSuffix = std::to_string(groupIndex) + "_" +
                                        std::to_string(subgroupIndex) + "_" + std::to_string(fightIdx);
                std::string selectableId = "##fight_" + rowSuffix;

                std::string dateStr = formatDate(fight.startTimeMs);

                if (ImGui::Selectable(
                    selectableId.c_str(),
                    isSelected,
                    ImGuiSelectableFlags_AllowDoubleClick,
                    ImVec2(0, 0)
                )) {
                    selectedGroupIndex_ = groupIndex;
                    selectedSubgroupIndex_ = subgroupIndex;
                    selectedFightIndex_ = fightIdx;

                    if (ImGui::IsMouseDoubleClicked(0)) {
                        chooseFight(fight);
                    }
                }

                ImGui::SameLine();
                if (fight.startTimeMs > 0) {
                    ImGui::Text("%s", dateStr.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", dateStr.c_str());
                }

                // Difficulty column
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", fight.difficultyName().c_str());

                // Duration column
                ImGui::TableSetColumnIndex(2);
                uint32_t durationSec = static_cast<uint32_t>(fight.durationMs / 1000);
                uint32_t minutes = durationSec / 60;
                uint32_t seconds = durationSec % 60;
                ImGui::Text("%u:%02u", minutes, seconds);

                // Report code column (truncated to 5 chars)
                ImGui::TableSetColumnIndex(3);
                std::string shortCode = fight.reportCode.substr(0, 5);
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "%s", shortCode.c_str());

                // Tooltip with full report code
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(L("cache.report_tooltip"), fight.reportCode.c_str(), fight.fightId);
                }

                // Composition column (class icons)
                ImGui::TableSetColumnIndex(4);
                if (!fight.classComposition.empty()) {
                    // Show first player name (truncated to 10 chars, UTF-8 safe)
                    if (!fight.firstPlayerName.empty()) {
                        std::string shortName = ui::utf8Truncate(fight.firstPlayerName, 10);
                        ImGui::TextColored(ImVec4(0.8f, 0.85f, 0.95f, 1.0f), "%s", shortName.c_str());
                        ImGui::SameLine(0, 8);
                    }

                    constexpr float ICON_SIZE = 25.0f;
                    for (const auto& [className, count] : fight.classComposition) {
                        if (iconCache) {
                            ImTextureID classIcon = iconCache->getClassIcon(className);
                            if (classIcon) {
                                ImGui::Image(classIcon, ImVec2(ICON_SIZE, ICON_SIZE));
                                if (count > 1) {
                                    ImGui::SameLine(0, 2);
                                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "%d", count);
                                }
                                ImGui::SameLine(0, 8);
                            }
                        } else {
                            // Fallback to text if no icon cache
                            ImGui::TextColored(ImVec4(0.7f, 0.8f, 0.9f, 1.0f), "%s", className.c_str());
                            if (count > 1) {
                                ImGui::SameLine(0, 0);
                                ImGui::Text("%d", count);
                            }
                            ImGui::SameLine(0, 8);
                        }
                    }
                    ImGui::NewLine();  // End the SameLine sequence
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
                }

                // Cached status column
                ImGui::TableSetColumnIndex(5);
                if (fight.fullyLoaded) {
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", L("cache.yes"));
                } else {
                    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "%s", L("cache.meta"));
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", L("cache.meta_tooltip"));
                    }
                }

                // Actions column (all buttons together)
                ImGui::TableSetColumnIndex(6);

                std::string openBtnId = std::string(L("cache.open")) + "##open_" + rowSuffix;
                if (awlui::Button(openBtnId.c_str(), awlui::ButtonVariant::Primary, awlui::ButtonSize::Sm)) {
                    chooseFight(fight);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", L("cache.open_tooltip"));
                }

                ImGui::SameLine(0, 4);

                // WarcraftLogs link button
                std::string linkBtnId = "WCL##wcl_" + rowSuffix;
                if (awlui::Button(linkBtnId.c_str(), awlui::ButtonVariant::Secondary, awlui::ButtonSize::Sm)) {
                    std::string url = warcraftlogs::UrlParser::buildUrl(fight.reportCode, fight.fightId);
                    BrowserHelper::openUrl(url);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", L("cache.open_wcl_tooltip"));
                }

                ImGui::SameLine(0, 4);

                std::string deleteBtnId = "X##del_" + rowSuffix;
                if (awlui::Button(deleteBtnId.c_str(), awlui::ButtonVariant::Danger, awlui::ButtonSize::Sm)) {
                    requestDeleteFight(fight);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", L("cache.delete"));
                }

                fightIdx++;
            }

            ImGui::EndTable();
        }

        ImGui::TreePop();
    }
}

void UICachedFightsPanel::requestDeleteFight(const CachedFightInfo& fight) {
    deleteTargetDescription_ = fight.fightName + " (" + formatDate(fight.startTimeMs) + ")";
    deletePendingReportCode_ = fight.reportCode;
    deletePendingFightId_ = fight.fightId;
    showDeleteConfirm_ = true;
}
