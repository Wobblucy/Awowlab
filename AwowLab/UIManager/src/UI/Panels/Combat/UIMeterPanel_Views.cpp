// UIMeterPanel_Views.cpp
// Per-view render functions for the meter panel: damage, healing,
// damage taken, dispels, interrupts, overhealing, healing taken,
// friendly fire, CC breaks, deaths, avoidance, absorbs, and the
// enemy damage/healing views.

#include "UI/Panels/Combat/UIMeterPanel.h"
#include "UI/Panels/Combat/MissTypeLabel.h"
#include "UI/AwlUI/Widgets.h"
#include "Color/ActorColorGenerator.h"
#include "Core/NumberFormatter.h"
#include "Core/UIUtils.h"
#include "Core/LocalizationManager.h"
#include "DeathDatabase.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>

namespace {

// Playback moving further than this between frames counts as a scrub
// and forces the cached stats to refresh straight away
constexpr uint32_t SCRUB_JUMP_MS = 500;

// Space kept below the bar list for the summary line
constexpr float SUMMARY_LINE_HEIGHT = 30.0f;
constexpr float MIN_CHART_HEIGHT = 100.0f;

constexpr float MIN_METER_BAR_WIDTH = 5.0f;
constexpr float BAR_TEXT_INSET = 5.0f;

const char* const EMPTY_STATE_GLYPH = "\xc2\xb7\xc2\xb7\xc2\xb7";

void showNoCombatData() {
    awlui::EmptyState("meter_empty", EMPTY_STATE_GLYPH,
                      L("status.no_combat_data"), L("empty.select_encounter_hint"));
}

void showNothingInWindow(const char* messageKey) {
    awlui::EmptyState("meter_empty", EMPTY_STATE_GLYPH,
                      L(messageKey), L("empty.scrub_hint"));
}

// Name from position data first, then from combat events (pets and
// guardians often only appear there), then the head of the GUID
std::string lookupActorName(
    const std::string& guid,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    const std::unordered_map<std::string, std::string>* combatGuidToName,
    size_t guidFallbackLength)
{
    std::string name;
    auto nameIt = guidToName.find(std::string_view(guid));
    if (nameIt != guidToName.end()) {
        name = std::string(nameIt->second);
    } else if (combatGuidToName) {
        auto combatNameIt = combatGuidToName->find(guid);
        if (combatNameIt != combatGuidToName->end()) {
            name = combatNameIt->second;
        }
    }
    if (name.empty()) {
        name = guid.substr(0, guidFallbackLength);
    }
    return name;
}

// Environmental damage (falling, lava, ...) logs as "nil" / null GUID;
// show the localized label instead
std::string withEnvironmentLabel(const std::string& guid, std::string name) {
    return ui::isEnvironmentSource(guid, name) ? ui::environmentName() : name;
}

// Cut to fit a bar, counting characters rather than bytes
std::string shortenName(std::string name, size_t maxChars) {
    if (ui::utf8CharCount(name) > maxChars) {
        name = ui::utf8Truncate(name, maxChars - 2) + "..";
    }
    return name;
}

ImVec4 actorBarColor(const std::shared_ptr<ActorColorGenerator>& colorGen,
                     const std::string& guid, const ImVec4& fallback) {
    if (!colorGen) return fallback;
    ActorColor c = colorGen->getCachedColor(guid);
    return ImVec4(c.r, c.g, c.b, 1.0f);
}

int64_t sumEffectiveAmount(const std::vector<ActorCombatStats>& stats) {
    int64_t total = 0;
    for (const auto& s : stats) {
        total += s.effective_amount;
    }
    return total;
}

float amountPerSecond(int64_t amount, uint32_t duration_ms) {
    return static_cast<float>(amount) / (static_cast<float>(duration_ms) / 1000.0f);
}

} // namespace

bool UIMeterPanel::statsCacheStale(bool cacheEmpty, MeterViewType view, uint32_t currentTime_ms) const {
    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms_ + SCRUB_JUMP_MS) ||
                        (lastPlaybackTime_ms_ > currentTime_ms + SCRUB_JUMP_MS);
    bool filterChanged = (filterStartTime_ms_ != cachedFilterStartTime_ms_)
                      || (filterEndTime_ms_ != cachedFilterEndTime_ms_);
    return cacheEmpty
        || cachedViewType_ != view
        || timeScrubbed
        || config_.time_mode != cachedTimeMode_
        || filterChanged
        || (now_ms - lastRefreshTime_ms_ >= REFRESH_INTERVAL_MS);
}

ui::TimeWindow UIMeterPanel::queryWindow(uint32_t currentTime_ms, int32_t dbMin_ms, int32_t dbMax_ms) const {
    // The segment/phase filter overrides the database's own bounds when set
    int32_t start = (filterStartTime_ms_ > 0) ? filterStartTime_ms_ : dbMin_ms;
    int32_t end = (filterEndTime_ms_ > 0) ? filterEndTime_ms_ : dbMax_ms;
    bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
    return ui::calculateTimeWindow(currentTime_ms, static_cast<uint32_t>(start),
                                   static_cast<uint32_t>(end), useFullEncounter);
}

ui::TimeWindow UIMeterPanel::beginStatsRefresh(uint32_t currentTime_ms, int32_t dbMin_ms, int32_t dbMax_ms) {
    cachedFilterStartTime_ms_ = filterStartTime_ms_;
    cachedFilterEndTime_ms_ = filterEndTime_ms_;
    return queryWindow(currentTime_ms, dbMin_ms, dbMax_ms);
}

void UIMeterPanel::finishStatsRefresh(MeterViewType view, const ui::TimeWindow& window, uint32_t currentTime_ms) {
    cachedDuration_ms_ = window.duration_ms;
    cachedWindowStart_ms_ = window.startTime;
    cachedWindowEnd_ms_ = window.endTime;
    cachedViewType_ = view;
    cachedTimeMode_ = config_.time_mode;
    cachedExpandedActorGuid_.clear();  // window shifted, drill-down needs to refetch

    lastRefreshTime_ms_ = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    lastPlaybackTime_ms_ = currentTime_ms;
}

void UIMeterPanel::beginChart(const char* label) {
    float chartHeight = std::max(ImGui::GetContentRegionAvail().y - SUMMARY_LINE_HEIGHT, MIN_CHART_HEIGHT);
    std::string chartId = std::string(label) + "##" + std::to_string(instanceId_);
    ImGui::BeginChild(chartId.c_str(), ImVec2(0, chartHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
}

bool UIMeterPanel::renderSimpleBarRow(int rowId, const char* buttonId, const std::string& name,
                                      const ImVec4& color, float ratio, float rightReserve,
                                      const char* valueText) {
    float availableWidth = ImGui::GetContentRegionAvail().x;
    float barWidth = std::max((availableWidth - rightReserve) * ratio, MIN_METER_BAR_WIDTH);
    float textY = (config_.bar_height - ImGui::GetTextLineHeight()) / 2.0f;

    ImGui::PushID(rowId);

    ImVec2 cursorPos = ImGui::GetCursorPos();
    ImVec2 screenPos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        screenPos,
        ImVec2(screenPos.x + barWidth, screenPos.y + config_.bar_height),
        ImGui::ColorConvertFloat4ToU32(color)
    );

    ImGui::SetCursorPos(ImVec2(cursorPos.x + BAR_TEXT_INSET, cursorPos.y + textY));
    ImGui::Text("%s", name.c_str());

    // The value column sits just inside the space reserved on the right
    ImGui::SetCursorPos(ImVec2(cursorPos.x + availableWidth - rightReserve + 10.0f, cursorPos.y + textY));
    ImGui::Text("%s", valueText);

    ImGui::SetCursorPos(cursorPos);
    bool clicked = ImGui::InvisibleButton(buttonId, ImVec2(availableWidth, config_.bar_height));

    ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + config_.bar_height + config_.bar_spacing));
    ImGui::PopID();
    return clicked;
}

void UIMeterPanel::renderCachedCombatChart(
    const char* chartLabel,
    const ImVec4& barColor,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName,
    const CombatDatabase* lazyBreakdownDb,
    CombatMetricType lazyMetric
) {
    beginChart(chartLabel);
    renderCombatBarChart(cachedCombatStats_, colorGen, guidToName, cachedGrandTotal_, cachedDuration_ms_,
                         iconLoader, combatGuidToName, barColor,
                         lazyBreakdownDb, lazyMetric,
                         cachedWindowStart_ms_, cachedWindowEnd_ms_);
    ImGui::EndChild();

    renderTotalSummary(cachedGrandTotal_, amountPerSecond(cachedGrandTotal_, cachedDuration_ms_));
}

void UIMeterPanel::renderDamageView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!combatDb || combatDb->empty()) {
        showNoCombatData();
        return;
    }

    if (statsCacheStale(cachedCombatStats_.empty(), MeterViewType::DamageDealt, currentTime_ms)) {
        ui::TimeWindow window = beginStatsRefresh(currentTime_ms,
            combatDb->getMinTimestamp(), combatDb->getMaxTimestamp());

        spellGroupSettings_.loadFromSettings();
        const auto& blacklist = spellGroupSettings_.getBlacklistedSpells();
        const std::unordered_set<uint32_t>* blacklistPtr = blacklist.empty() ? nullptr : &blacklist;

        cachedCombatStats_ = combatDb->getRankedByActorWithPets(
            CombatMetricType::DamageDealt, window.startTime, window.endTime, 100, false, blacklistPtr);
        cachedCombatStats_ = ui::filterFriendlyActors(std::move(cachedCombatStats_), colorGen, 40);
        cachedGrandTotal_ = sumEffectiveAmount(cachedCombatStats_);

        finishStatsRefresh(MeterViewType::DamageDealt, window, currentTime_ms);
    }

    if (cachedCombatStats_.empty()) {
        showNothingInWindow("status.no_damage_data");
        return;
    }

    renderCachedCombatChart("DamageChart", ImVec4(0.4f, 0.4f, 0.8f, 1.0f),
                            colorGen, guidToName, iconLoader, combatGuidToName,
                            combatDb, CombatMetricType::DamageDealt);
}

void UIMeterPanel::renderHealingView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!combatDb || combatDb->empty()) {
        showNoCombatData();
        return;
    }

    if (statsCacheStale(cachedCombatStats_.empty(), MeterViewType::HealingDone, currentTime_ms)) {
        ui::TimeWindow window = beginStatsRefresh(currentTime_ms,
            combatDb->getMinTimestamp(), combatDb->getMaxTimestamp());

        spellGroupSettings_.loadFromSettings();
        const auto& blacklist = spellGroupSettings_.getBlacklistedSpells();
        const std::unordered_set<uint32_t>* blacklistPtr = blacklist.empty() ? nullptr : &blacklist;

        cachedCombatStats_ = combatDb->getRankedByActorWithPets(
            CombatMetricType::HealingDone, window.startTime, window.endTime, 100, false, blacklistPtr);
        cachedCombatStats_ = ui::filterFriendlyActors(std::move(cachedCombatStats_), colorGen, 40);
        cachedGrandTotal_ = sumEffectiveAmount(cachedCombatStats_);

        finishStatsRefresh(MeterViewType::HealingDone, window, currentTime_ms);
    }

    if (cachedCombatStats_.empty()) {
        showNothingInWindow("status.no_healing_data");
        return;
    }

    renderCachedCombatChart("HealingChart", ImVec4(0.2f, 0.6f, 0.3f, 1.0f),
                            colorGen, guidToName, iconLoader, combatGuidToName,
                            combatDb, CombatMetricType::HealingDone);
}

void UIMeterPanel::renderDamageTakenView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!combatDb || combatDb->empty()) {
        showNoCombatData();
        return;
    }

    if (statsCacheStale(cachedCombatStats_.empty(), MeterViewType::DamageTaken, currentTime_ms)) {
        ui::TimeWindow window = beginStatsRefresh(currentTime_ms,
            combatDb->getMinTimestamp(), combatDb->getMaxTimestamp());

        // Get damage taken with spell breakdown for ability view
        bool includeBreakdowns = (config_.view_type == MeterViewType::DamageTakenByAbility);
        cachedCombatStats_ = combatDb->getRankedByDamageTakenWithPets(
            window.startTime, window.endTime, 100, includeBreakdowns);

        // Filter to friendly players only. Damage taken isn't merged into
        // owners, so a friendly pet would otherwise appear as its own row -
        // a pet's damage taken is the pet's, not the player's.
        cachedCombatStats_ = ui::filterFriendlyActors(
            std::move(cachedCombatStats_), colorGen, 40, /*excludePets=*/true);
        cachedGrandTotal_ = sumEffectiveAmount(cachedCombatStats_);

        finishStatsRefresh(MeterViewType::DamageTaken, window, currentTime_ms);
    }

    if (cachedCombatStats_.empty()) {
        showNothingInWindow("status.no_damage_taken_data");
        return;
    }

    renderCachedCombatChart("DamageTakenChart", ImVec4(0.8f, 0.3f, 0.3f, 1.0f),
                            colorGen, guidToName, iconLoader, combatGuidToName,
                            combatDb, CombatMetricType::DamageTaken);
}

void UIMeterPanel::renderDamageTakenByView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!combatDb || combatDb->empty()) {
        showNoCombatData();
        return;
    }

    if (selectedTargetGuid_.empty()) {
        renderDamageTakenByTargetPicker(combatDb, colorGen, guidToName, currentTime_ms, combatGuidToName);
        return;
    }

    std::string targetName = lookupActorName(selectedTargetGuid_, guidToName, combatGuidToName, 16);

    ImGui::Text("%s: ", L("meter.damage_to"));
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", targetName.c_str());
    ImGui::SameLine();
    std::string clearBtnId = "X##clear" + std::to_string(instanceId_);
    if (awlui::Button(clearBtnId.c_str(), awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
        selectedTargetGuid_.clear();
        selectedTargetNpcId_ = 0;
        return;
    }
    ImGui::Separator();

    if (statsCacheStale(cachedCombatStats_.empty(), MeterViewType::DamageTakenBy, currentTime_ms)) {
        ui::TimeWindow window = beginStatsRefresh(currentTime_ms,
            combatDb->getMinTimestamp(), combatDb->getMaxTimestamp());

        cachedCombatStats_ = combatDb->getDamageDoneToTargetGroup(
            selectedTargetGuid_, selectedTargetNpcId_, window.startTime, window.endTime, 100);
        cachedGrandTotal_ = sumEffectiveAmount(cachedCombatStats_);

        finishStatsRefresh(MeterViewType::DamageTakenBy, window, currentTime_ms);
    }

    if (cachedCombatStats_.empty()) {
        ImGui::Text("%s", L("status.no_damage_to_target"));
        return;
    }

    renderCachedCombatChart("DamageTakenByChart", ImVec4(0.9f, 0.5f, 0.2f, 1.0f),
                            colorGen, guidToName, iconLoader, combatGuidToName);
}

// No target selected: list every enemy in the current segment. Clicking
// one seeds it as the target and drops into the "who damaged X" view.
// Mirrors Details' click-an-enemy-to-filter flow so users don't need an
// external Enemy Grid to pick from.
void UIMeterPanel::renderDamageTakenByTargetPicker(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    ui::TimeWindow window = queryWindow(currentTime_ms,
        combatDb->getMinTimestamp(), combatDb->getMaxTimestamp());

    auto enemies = combatDb->getRankedByEnemyDamage(window.startTime, window.endTime, 40);
    if (enemies.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", L("status.no_enemy_damage_data"));
        return;
    }

    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", L("meter.select_target"));
    ImGui::Separator();

    int64_t maxAmount = std::max<int64_t>(enemies.front().effective_amount, 1);

    for (size_t i = 0; i < enemies.size(); ++i) {
        const auto& e = enemies[i];

        std::string name = shortenName(
            withEnvironmentLabel(e.actor_guid,
                lookupActorName(e.actor_guid, guidToName, combatGuidToName, 16)),
            20);
        ImVec4 color = actorBarColor(colorGen, e.actor_guid, ImVec4(0.75f, 0.35f, 0.35f, 1.0f));
        float ratio = static_cast<float>(e.effective_amount) / static_cast<float>(maxAmount);
        std::string amountStr = ui::format::formatAmount(e.effective_amount);

        int rowId = static_cast<int>(i * 10 + instanceId_) + 3000;
        if (renderSimpleBarRow(rowId, "##pick", name, color, ratio, 140.0f, amountStr.c_str())) {
            // Seed both the representative guid (for the header name) and
            // the enemy-type group (npc id) so the drill-down covers every
            // spawn of this enemy type, matching the merged picker row.
            selectedTargetGuid_ = e.actor_guid;
            selectedTargetNpcId_ = MobWeightSettings::npcIdFromGuid(e.actor_guid);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(L("meter.show_who_damaged_fmt"), name.c_str());
        }
    }
}

void UIMeterPanel::renderDispelView(
    const DispelDatabase* dispelDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!dispelDb || dispelDb->empty()) {
        ImGui::Text("%s", L("status.no_dispel_data"));
        return;
    }

    if (statsCacheStale(cachedDispelStats_.empty(), MeterViewType::Dispels, currentTime_ms)) {
        ui::TimeWindow window = beginStatsRefresh(currentTime_ms,
            dispelDb->getMinTimestamp(), dispelDb->getMaxTimestamp());

        cachedDispelStats_ = dispelDb->getRankedByActor(window.startTime, window.endTime, 40, true,
                                                         DispelDatabase::FilterType::DispelsOnly);
        cachedDispelTotal_ = 0;
        for (const auto& s : cachedDispelStats_) {
            cachedDispelTotal_ += s.total_count;
        }

        finishStatsRefresh(MeterViewType::Dispels, window, currentTime_ms);
    }

    if (cachedDispelStats_.empty()) {
        ImGui::Text("%s", L("status.no_dispel_data"));
        return;
    }

    ui::TimeWindow window = queryWindow(currentTime_ms,
        dispelDb->getMinTimestamp(), dispelDb->getMaxTimestamp());
    uint32_t dispelCount = dispelDb->getTotalDispelCount(window.startTime, window.endTime);

    beginChart("DispelChart");
    renderDispelBarChart(cachedDispelStats_, colorGen, guidToName, cachedDispelTotal_, iconLoader, combatGuidToName);
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Text("%s: %u", L("meter.dispel_count"), dispelCount);
}

void UIMeterPanel::renderInterruptView(
    const DispelDatabase* dispelDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!dispelDb || dispelDb->empty()) {
        ImGui::Text("%s", L("status.no_interrupt_data"));
        return;
    }

    if (statsCacheStale(cachedDispelStats_.empty(), MeterViewType::Interrupts, currentTime_ms)) {
        ui::TimeWindow window = beginStatsRefresh(currentTime_ms,
            dispelDb->getMinTimestamp(), dispelDb->getMaxTimestamp());

        cachedDispelStats_ = dispelDb->getRankedByActor(window.startTime, window.endTime, 40, true,
                                                         DispelDatabase::FilterType::InterruptsOnly);
        cachedDispelTotal_ = 0;
        for (const auto& s : cachedDispelStats_) {
            cachedDispelTotal_ += s.total_count;
        }

        finishStatsRefresh(MeterViewType::Interrupts, window, currentTime_ms);
    }

    if (cachedDispelStats_.empty()) {
        ImGui::Text("%s", L("status.no_interrupt_data"));
        return;
    }

    ui::TimeWindow window = queryWindow(currentTime_ms,
        dispelDb->getMinTimestamp(), dispelDb->getMaxTimestamp());
    uint32_t interruptCount = dispelDb->getTotalInterruptCount(window.startTime, window.endTime);

    beginChart("InterruptChart");
    renderDispelBarChart(cachedDispelStats_, colorGen, guidToName, cachedDispelTotal_, iconLoader, combatGuidToName);
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Text("%s: %u", L("meter.interrupt_count"), interruptCount);
}

void UIMeterPanel::renderOverhealingView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!combatDb || combatDb->empty()) {
        showNoCombatData();
        return;
    }

    if (statsCacheStale(cachedCombatStats_.empty(), MeterViewType::Overhealing, currentTime_ms)) {
        ui::TimeWindow window = beginStatsRefresh(currentTime_ms,
            combatDb->getMinTimestamp(), combatDb->getMaxTimestamp());

        // effective_amount holds the overheal for this query
        cachedCombatStats_ = combatDb->getRankedByOverhealing(window.startTime, window.endTime, 100);
        cachedCombatStats_ = ui::filterFriendlyActors(std::move(cachedCombatStats_), colorGen, 40);
        cachedGrandTotal_ = sumEffectiveAmount(cachedCombatStats_);

        finishStatsRefresh(MeterViewType::Overhealing, window, currentTime_ms);
    }

    if (cachedCombatStats_.empty()) {
        ImGui::Text("%s", L("status.no_overhealing_data"));
        return;
    }

    renderCachedCombatChart("OverhealingChart", ImVec4(0.8f, 0.6f, 0.2f, 1.0f),
                            colorGen, guidToName, iconLoader, combatGuidToName);
}

void UIMeterPanel::renderHealingTakenView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!combatDb || combatDb->empty()) {
        showNoCombatData();
        return;
    }

    if (statsCacheStale(cachedCombatStats_.empty(), MeterViewType::HealingTaken, currentTime_ms)) {
        ui::TimeWindow window = beginStatsRefresh(currentTime_ms,
            combatDb->getMinTimestamp(), combatDb->getMaxTimestamp());

        cachedCombatStats_ = combatDb->getRankedByHealingTaken(window.startTime, window.endTime, 100);
        cachedCombatStats_ = ui::filterFriendlyActors(std::move(cachedCombatStats_), colorGen, 40);
        cachedGrandTotal_ = sumEffectiveAmount(cachedCombatStats_);

        finishStatsRefresh(MeterViewType::HealingTaken, window, currentTime_ms);
    }

    if (cachedCombatStats_.empty()) {
        ImGui::Text("%s", L("status.no_healing_taken_data"));
        return;
    }

    renderCachedCombatChart("HealingTakenChart", ImVec4(0.3f, 0.7f, 0.4f, 1.0f),
                            colorGen, guidToName, iconLoader, combatGuidToName,
                            combatDb, CombatMetricType::HealingReceived);
}

void UIMeterPanel::renderFriendlyFireView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!combatDb || combatDb->empty()) {
        showNoCombatData();
        return;
    }

    if (statsCacheStale(cachedCombatStats_.empty(), MeterViewType::FriendlyFire, currentTime_ms)) {
        ui::TimeWindow window = beginStatsRefresh(currentTime_ms,
            combatDb->getMinTimestamp(), combatDb->getMaxTimestamp());

        cachedCombatStats_ = combatDb->getRankedByFriendlyFire(window.startTime, window.endTime, 100);
        cachedGrandTotal_ = sumEffectiveAmount(cachedCombatStats_);

        finishStatsRefresh(MeterViewType::FriendlyFire, window, currentTime_ms);
    }

    if (cachedCombatStats_.empty()) {
        ImGui::Text("%s", L("status.no_friendly_fire_data"));
        return;
    }

    renderCachedCombatChart("FriendlyFireChart", ImVec4(0.9f, 0.3f, 0.1f, 1.0f),
                            colorGen, guidToName, iconLoader, combatGuidToName,
                            combatDb, CombatMetricType::FriendlyFire);
}

void UIMeterPanel::renderCCBreaksView(
    const AuraDatabase* auraDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* /*iconLoader*/,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!auraDb || !auraDb->hasCCBreaks()) {
        ImGui::Text("%s", L("status.no_cc_break_data"));
        return;
    }

    if (statsCacheStale(cachedCCBreakStats_.empty(), MeterViewType::CCBreaks, currentTime_ms)) {
        ui::TimeWindow window = beginStatsRefresh(currentTime_ms,
            auraDb->getMinTimestamp(), auraDb->getMaxTimestamp());

        cachedCCBreakStats_ = auraDb->getRankedByCCBreaks(window.startTime, window.endTime, 40);
        cachedCCBreakTotal_ = 0;
        for (const auto& s : cachedCCBreakStats_) {
            cachedCCBreakTotal_ += s.break_count;
        }

        finishStatsRefresh(MeterViewType::CCBreaks, window, currentTime_ms);
    }

    if (cachedCCBreakStats_.empty()) {
        ImGui::Text("%s", L("status.no_cc_break_data"));
        return;
    }

    beginChart("CCBreaksChart");

    uint32_t maxCount = std::max<uint32_t>(cachedCCBreakStats_.front().break_count, 1);

    for (size_t i = 0; i < cachedCCBreakStats_.size(); ++i) {
        const auto& actorStats = cachedCCBreakStats_[i];

        std::string actorName = shortenName(
            lookupActorName(actorStats.actor_guid, guidToName, combatGuidToName, 12), 12);
        ImVec4 barColor = actorBarColor(colorGen, actorStats.actor_guid, ImVec4(0.7f, 0.3f, 0.3f, 1.0f));
        float ratio = static_cast<float>(actorStats.break_count) / maxCount;

        float percent = cachedCCBreakTotal_ > 0
            ? (static_cast<float>(actorStats.break_count) / cachedCCBreakTotal_ * 100.0f) : 0.0f;
        char valueText[48];
        std::snprintf(valueText, sizeof(valueText), "%u (%.1f%%)", actorStats.break_count, percent);

        int rowId = static_cast<int>(i * 10 + instanceId_);
        if (renderSimpleBarRow(rowId, "##bar", actorName, barColor, ratio, 160.0f, valueText)) {
            clickedActor_ = actorStats.actor_guid;
        }

        if (ImGui::IsItemHovered() && !actorStats.breakdown.empty()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s - %s", actorName.c_str(), L("meter.cc_breaks"));
            ImGui::Separator();
            for (const auto& detail : actorStats.breakdown) {
                ImGui::Text("  %s x%u", detail.cc_spell_name.c_str(), detail.count);
            }
            ImGui::EndTooltip();
        }
    }

    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Text("%s: %u", L("meter.total_cc_breaks"), cachedCCBreakTotal_);
}

void UIMeterPanel::renderDeathsView(
    const DeathDatabase* deathDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* /*iconLoader*/,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!deathDb) {
        ImGui::Text("%s", L("status.no_death_data"));
        return;
    }

    if (statsCacheStale(cachedDeathActorGuids_.empty(), MeterViewType::Deaths, currentTime_ms)) {
        ui::TimeWindow window = beginStatsRefresh(currentTime_ms,
            deathDb->getMinTimestamp(), deathDb->getMaxTimestamp());

        cachedDeathActorGuids_.clear();
        for (const auto* death : deathDb->getPlayerDeathsInRange(window.startTime, window.endTime)) {
            cachedDeathActorGuids_.push_back(death->actor_guid);
        }
        cachedDeathTotal_ = static_cast<uint32_t>(cachedDeathActorGuids_.size());

        finishStatsRefresh(MeterViewType::Deaths, window, currentTime_ms);
    }

    if (cachedDeathActorGuids_.empty()) {
        ImGui::Text("%s", L("status.no_death_data"));
        return;
    }

    struct DeathCount {
        std::string actor_guid;
        uint32_t count = 0;
    };
    std::unordered_map<std::string, uint32_t> deathCountMap;
    for (const auto& guid : cachedDeathActorGuids_) {
        deathCountMap[guid]++;
    }

    std::vector<DeathCount> deathCounts;
    deathCounts.reserve(deathCountMap.size());
    for (const auto& [guid, count] : deathCountMap) {
        deathCounts.push_back({guid, count});
    }
    std::sort(deathCounts.begin(), deathCounts.end(),
        [](const DeathCount& a, const DeathCount& b) { return a.count > b.count; });

    uint32_t totalDeaths = cachedDeathTotal_;

    beginChart("DeathsChart");

    uint32_t maxCount = std::max<uint32_t>(deathCounts.front().count, 1);

    for (size_t i = 0; i < deathCounts.size(); ++i) {
        const auto& actorStats = deathCounts[i];

        std::string actorName = shortenName(
            lookupActorName(actorStats.actor_guid, guidToName, combatGuidToName, 12), 12);
        ImVec4 barColor = actorBarColor(colorGen, actorStats.actor_guid, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
        float ratio = static_cast<float>(actorStats.count) / maxCount;

        float percent = totalDeaths > 0 ? (static_cast<float>(actorStats.count) / totalDeaths * 100.0f) : 0.0f;
        char valueText[48];
        std::snprintf(valueText, sizeof(valueText), "%u (%.1f%%)", actorStats.count, percent);

        int rowId = static_cast<int>(i * 10 + instanceId_);
        if (renderSimpleBarRow(rowId, "##bar", actorName, barColor, ratio, 160.0f, valueText)) {
            clickedActor_ = actorStats.actor_guid;
        }
    }

    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Text("%s: %u", L("meter.total_deaths"), totalDeaths);
}

void UIMeterPanel::renderAvoidanceView(
    const AvoidanceDatabase* avoidanceDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* /*iconLoader*/,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!avoidanceDb || avoidanceDb->empty()) {
        ImGui::Text("%s", L("status.no_avoidance_data"));
        return;
    }

    if (statsCacheStale(cachedAvoidanceStats_.empty(), MeterViewType::Avoidance, currentTime_ms)) {
        ui::TimeWindow window = beginStatsRefresh(currentTime_ms,
            avoidanceDb->getMinTimestamp(), avoidanceDb->getMaxTimestamp());

        cachedAvoidanceStats_ = avoidanceDb->getRankedByTarget(window.startTime, window.endTime, 40, true);
        cachedAvoidanceTotal_ = 0;
        cachedAvoidanceAmount_ = 0;
        for (const auto& s : cachedAvoidanceStats_) {
            cachedAvoidanceTotal_ += s.total_count;
            cachedAvoidanceAmount_ += s.total_amount;
        }

        finishStatsRefresh(MeterViewType::Avoidance, window, currentTime_ms);
    }

    if (cachedAvoidanceStats_.empty()) {
        ImGui::Text("%s", L("status.no_avoidance_data"));
        return;
    }

    beginChart("AvoidanceChart");

    // Bar length: prefer total_amount when any absorb/block/resist
    // amount was seen in this window; fall back to count for pure-
    // dodge fights so the meter isn't all zero-length bars.
    bool useAmountBars = cachedAvoidanceAmount_ > 0;
    int64_t maxAmount = std::max<int64_t>(1, cachedAvoidanceStats_.front().total_amount);
    uint32_t maxCount = std::max<uint32_t>(1, cachedAvoidanceStats_.front().total_count);

    for (size_t i = 0; i < cachedAvoidanceStats_.size(); ++i) {
        const auto& actorStats = cachedAvoidanceStats_[i];

        std::string actorName = shortenName(
            lookupActorName(actorStats.actor_guid, guidToName, combatGuidToName, 12), 12);
        ImVec4 barColor = actorBarColor(colorGen, actorStats.actor_guid, ImVec4(0.3f, 0.5f, 0.7f, 1.0f));
        float ratio = useAmountBars
            ? static_cast<float>(actorStats.total_amount) / static_cast<float>(maxAmount)
            : static_cast<float>(actorStats.total_count) / static_cast<float>(maxCount);

        // "12.3M (145)" when amounts are present, just "145" otherwise
        char valueText[64];
        if (actorStats.total_amount > 0) {
            std::string amountStr = ui::format::formatAmount(actorStats.total_amount);
            std::snprintf(valueText, sizeof(valueText), "%s (%u)", amountStr.c_str(), actorStats.total_count);
        } else {
            std::snprintf(valueText, sizeof(valueText), "%u", actorStats.total_count);
        }

        int rowId = static_cast<int>(i * 10 + instanceId_);
        if (renderSimpleBarRow(rowId, "##bar", actorName, barColor, ratio, 200.0f, valueText)) {
            clickedActor_ = actorStats.actor_guid;
        }

        // Per-type breakdown. AvoidanceDatabase sorts it by amount, so the
        // most impactful mitigation type shows first.
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s - %s", actorName.c_str(), L("meter.avoidance"));
            if (actorStats.total_amount > 0) {
                std::string amountStr = ui::format::formatAmount(actorStats.total_amount);
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f),
                    L("meter.damage_avoided_fmt"), amountStr.c_str());
            }
            ImGui::Separator();
            for (const auto& b : actorStats.breakdown) {
                if (b.amount > 0) {
                    std::string a = ui::format::formatAmount(b.amount);
                    ImGui::Text("  %s: %u  (%s)", missTypeLabel(b.type), b.count, a.c_str());
                } else {
                    ImGui::Text("  %s: %u", missTypeLabel(b.type), b.count);
                }
            }
            ImGui::EndTooltip();
        }
    }

    ImGui::EndChild();

    ImGui::Separator();
    if (cachedAvoidanceAmount_ > 0) {
        std::string totalStr = ui::format::formatAmount(cachedAvoidanceAmount_);
        ImGui::Text("%s: %s  (%u)", L("meter.total_avoidance"),
                    totalStr.c_str(), cachedAvoidanceTotal_);
    } else {
        ImGui::Text("%s: %u", L("meter.total_avoidance"), cachedAvoidanceTotal_);
    }
}

void UIMeterPanel::renderAbsorbsView(
    const AbsorbDatabase* absorbDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* /*iconLoader*/,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!absorbDb || absorbDb->empty()) {
        ImGui::Text("%s", L("status.no_absorb_data"));
        return;
    }

    if (statsCacheStale(cachedAbsorbStats_.empty(), MeterViewType::Absorbs, currentTime_ms)) {
        ui::TimeWindow window = beginStatsRefresh(currentTime_ms,
            absorbDb->getMinTimestamp(), absorbDb->getMaxTimestamp());

        cachedAbsorbStats_ = absorbDb->getRankedByAbsorber(window.startTime, window.endTime, 40, true);
        cachedAbsorbTotal_ = 0;
        for (const auto& s : cachedAbsorbStats_) {
            cachedAbsorbTotal_ += s.total_absorbed;
        }

        finishStatsRefresh(MeterViewType::Absorbs, window, currentTime_ms);
    }

    if (cachedAbsorbStats_.empty()) {
        ImGui::Text("%s", L("status.no_absorb_data"));
        return;
    }

    beginChart("AbsorbChart");

    int64_t maxAmount = std::max<int64_t>(cachedAbsorbStats_.front().total_absorbed, 1);

    for (size_t i = 0; i < cachedAbsorbStats_.size(); ++i) {
        const auto& absorbStats = cachedAbsorbStats_[i];

        std::string absorberName = shortenName(
            lookupActorName(absorbStats.actor_guid, guidToName, combatGuidToName, 12), 14);
        ImVec4 barColor = actorBarColor(colorGen, absorbStats.actor_guid, ImVec4(0.35f, 0.65f, 0.85f, 1.0f));
        float ratio = static_cast<float>(absorbStats.total_absorbed) / static_cast<float>(maxAmount);

        std::string amountStr = ui::format::formatAmount(absorbStats.total_absorbed);
        std::string apsStr = ui::format::formatPerSecond(amountPerSecond(absorbStats.total_absorbed, cachedDuration_ms_));
        char valueText[96];
        std::snprintf(valueText, sizeof(valueText), "%s (%s, %.1f%%)", amountStr.c_str(), apsStr.c_str(),
                      absorbStats.percent_of_total);

        int rowId = static_cast<int>(i * 10 + instanceId_);
        if (renderSimpleBarRow(rowId, "##bar", absorberName, barColor, ratio, 180.0f, valueText)) {
            clickedActor_ = absorbStats.actor_guid;
        }

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s - %s", absorberName.c_str(), L("meter.tab.absorbs"));
            ImGui::Separator();
            ImGui::Text("%s: %s", L("meter.total_absorbed"), amountStr.c_str());
            ImGui::Text("%s: %u", L("meter.event_count"), absorbStats.event_count);
            if (!absorbStats.spell_breakdown.empty()) {
                ImGui::Separator();
                for (const auto& sb : absorbStats.spell_breakdown) {
                    const char* spellName = sb.spell_name.empty() ? "?" : sb.spell_name.c_str();
                    std::string sbAmount = ui::format::formatAmount(sb.total_absorbed);
                    ImGui::Text("  %s: %s (%u)", spellName, sbAmount.c_str(), sb.event_count);
                }
            }
            ImGui::EndTooltip();
        }
    }

    ImGui::EndChild();

    ImGui::Separator();
    std::string totalStr = ui::format::formatAmount(cachedAbsorbTotal_);
    ImGui::Text("%s: %s", L("meter.total_absorbed"), totalStr.c_str());
}

// Shared by Enemy Damage and Enemy Healing: ranks hostile source actors
// and draws them as simple bars.
void UIMeterPanel::renderEnemyView(
    MeterViewType viewType,
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    const std::unordered_map<std::string, std::string>* combatGuidToName,
    const char* emptyLabel,
    const char* chartLabel,
    const ImVec4& defaultBarColor
) {
    if (!combatDb || combatDb->empty()) {
        ImGui::Text("%s", emptyLabel);
        return;
    }

    if (statsCacheStale(cachedCombatStats_.empty(), viewType, currentTime_ms)) {
        ui::TimeWindow window = beginStatsRefresh(currentTime_ms,
            combatDb->getMinTimestamp(), combatDb->getMaxTimestamp());

        cachedCombatStats_ = (viewType == MeterViewType::EnemyHealing)
            ? combatDb->getRankedByEnemyHealing(window.startTime, window.endTime, 40)
            : combatDb->getRankedByEnemyDamage(window.startTime, window.endTime, 40);
        cachedGrandTotal_ = sumEffectiveAmount(cachedCombatStats_);

        finishStatsRefresh(viewType, window, currentTime_ms);
    }

    if (cachedCombatStats_.empty()) {
        ImGui::Text("%s", emptyLabel);
        return;
    }

    beginChart(chartLabel);

    int64_t maxAmount = std::max<int64_t>(cachedCombatStats_.front().effective_amount, 1);
    // Only enemy damage has a breakdown to open, so enemy healing rows
    // don't respond to clicks
    bool rowsOpenBreakdown = (viewType == MeterViewType::EnemyDamage);

    for (size_t i = 0; i < cachedCombatStats_.size(); ++i) {
        const auto& stats = cachedCombatStats_[i];

        std::string actorName = shortenName(
            withEnvironmentLabel(stats.actor_guid,
                lookupActorName(stats.actor_guid, guidToName, combatGuidToName, 12)),
            16);
        ImVec4 color = actorBarColor(colorGen, stats.actor_guid, defaultBarColor);
        float ratio = static_cast<float>(stats.effective_amount) / static_cast<float>(maxAmount);

        std::string amountStr = ui::format::formatAmount(stats.effective_amount);
        std::string apsStr = ui::format::formatPerSecond(amountPerSecond(stats.effective_amount, cachedDuration_ms_));
        float percent = cachedGrandTotal_ > 0
            ? (static_cast<float>(stats.effective_amount) / static_cast<float>(cachedGrandTotal_)) * 100.0f
            : 0.0f;
        char valueText[96];
        std::snprintf(valueText, sizeof(valueText), "%s (%s, %.1f%%)", amountStr.c_str(), apsStr.c_str(), percent);

        // Clicking an enemy opens its own damage breakdown - the spells it
        // used against the raid - rather than the "who damaged this enemy" view
        int rowId = static_cast<int>(i * 10 + instanceId_);
        bool clicked = renderSimpleBarRow(rowId, "##bar", actorName, color, ratio, 180.0f, valueText);
        if (clicked && rowsOpenBreakdown) {
            requestActorBreakdown(stats.actor_guid, stats, cachedDuration_ms_);
        }
        if (ImGui::IsItemHovered() && rowsOpenBreakdown) {
            ImGui::SetTooltip(L("meter.click_breakdown_fmt"), actorName.c_str());
        }
    }

    ImGui::EndChild();

    ImGui::Separator();
    std::string totalStr = ui::format::formatAmount(cachedGrandTotal_);
    ImGui::Text("%s: %s", L("meter.total"), totalStr.c_str());
}

void UIMeterPanel::renderEnemyDamageView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* /*iconLoader*/,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    renderEnemyView(MeterViewType::EnemyDamage, combatDb, colorGen, guidToName, currentTime_ms,
                    combatGuidToName, L("status.no_enemy_damage_data"), "EnemyDamageChart",
                    ImVec4(0.75f, 0.35f, 0.35f, 1.0f));
}

void UIMeterPanel::renderEnemyHealingView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* /*iconLoader*/,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    renderEnemyView(MeterViewType::EnemyHealing, combatDb, colorGen, guidToName, currentTime_ms,
                    combatGuidToName, L("status.no_enemy_healing_data"), "EnemyHealingChart",
                    ImVec4(0.35f, 0.65f, 0.45f, 1.0f));
}
