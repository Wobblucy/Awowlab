#include "UI/Panels/Navigation/BossTimerStrip.h"
#include "UI/BossTimerSettings.h"
#include "UI/SpellIconLoader.h"
#include "UI/RaidNoteIconMenu.h"
#include "Playback/PlaybackController.h"
#include "Database/SpellCastDatabase.h"
#include "Database/AuraDatabase.h"
#include "Database/CombatDatabase.h"
#include "Color/ActorColorGenerator.h"
#include "AnnotationManager/include/AutoAnnotationTypes.h"
#include "Core/SpellNameDatabase.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

BossTimerStrip::BossTimerStrip() = default;
BossTimerStrip::~BossTimerStrip() = default;

float BossTimerStrip::getHeight() const {
    if (!settings_ || !settings_->enabled) return 0.0f;
    if (eventGroups_.empty()) return 0.0f;
    float iconSize = settings_ ? settings_->iconSize : 24.0f;
    return (iconSize + 4.0f) * rowCount_ + 4.0f;  // Icon + padding per row + top padding
}

void BossTimerStrip::setHostileCreatureSpells(const std::vector<HostileCreatureSpells>& creatureSpells) {
    // Build lookup sets from the hostile creature spells
    hostileCastIds_.clear();
    hostileDebuffIds_.clear();
    hostileDamageIds_.clear();

    for (const auto& creature : creatureSpells) {
        for (uint32_t id : creature.spell_ids) {
            hostileCastIds_.insert(id);
        }
        for (uint32_t id : creature.debuff_ids) {
            hostileDebuffIds_.insert(id);
        }
        for (uint32_t id : creature.damage_ids) {
            hostileDamageIds_.insert(id);
        }
    }

    // Mark cache as dirty so events are rebuilt
    cacheDirty_ = true;
}

void BossTimerStrip::setDatabases(
    const SpellCastDatabase* spellCastDb,
    const AuraDatabase* auraDb,
    const CombatDatabase* combatDb,
    const ActorColorGenerator* colorGenerator
) {
    spellCastDb_ = spellCastDb;
    auraDb_ = auraDb;
    combatDb_ = combatDb;
    colorGenerator_ = colorGenerator;
    cacheDirty_ = true;
}

void BossTimerStrip::render() {
    if (!settings_ || !settings_->enabled) return;
    if (!playback_) return;

    // Rebuild cache if dirty
    if (cacheDirty_) {
        rebuildEventCache();
    }

    if (eventGroups_.empty()) return;

    float iconSize = settings_->iconSize;
    float stripHeight = getHeight();

    ImVec2 contentMin = ImGui::GetCursorScreenPos();
    float availWidth = ImGui::GetContentRegionAvail().x;
    float padding = 8.0f;
    float timelineWidth = availWidth - (padding * 2);

    if (timelineWidth < 100.0f) return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(
        ImVec2(contentMin.x, contentMin.y),
        ImVec2(contentMin.x + availWidth, contentMin.y + stripHeight),
        IM_COL32(22, 22, 30, 220),
        4.0f
    );

    int32_t currentTime = playback_->getCurrentTime();
    float duration = static_cast<float>(maxTime_ - minTime_);

    if (duration > 0) {
        float playheadNorm = static_cast<float>(currentTime - minTime_) / duration;
        float playheadX = contentMin.x + padding + (playheadNorm * timelineWidth);

        drawList->AddLine(
            ImVec2(playheadX, contentMin.y),
            ImVec2(playheadX, contentMin.y + stripHeight),
            IM_COL32(255, 255, 255, 150),
            1.5f
        );
    }

    for (const auto& group : eventGroups_) {
        float xPos = contentMin.x + padding + (group.normalizedPos * timelineWidth);
        float yPos = contentMin.y + 2.0f + (group.row * (iconSize + 4.0f));

        // Skip if off-screen
        if (xPos < contentMin.x - iconSize || xPos > contentMin.x + availWidth) {
            continue;
        }

        renderEventIcon(group, xPos, yPos, iconSize);
    }

    // Reserve space in ImGui layout
    ImGui::Dummy(ImVec2(availWidth, stripHeight));
}

void BossTimerStrip::rebuildEventCache() {
    if (!playback_) return;

    minTime_ = playback_->getMinTime();
    maxTime_ = playback_->getMaxTime();

    rawEvents_.clear();
    eventGroups_.clear();

    collectHostileEvents();
    groupAndFilterEvents();
    calculateRowAssignments();

    cacheDirty_ = false;
}

void BossTimerStrip::collectHostileEvents() {
    if (!colorGenerator_) return;

    const auto& actorFlags = colorGenerator_->getActorFlags();

    if (spellCastDb_) {
        const auto& allCasts = spellCastDb_->getAllActorCasts();
        for (const auto& [guid, casts] : allCasts) {
            auto it = actorFlags.find(std::string_view(guid));
            if (it == actorFlags.end()) continue;
            if (!it->second.isHostile() && !it->second.isNeutral()) continue;

            std::string_view actorName = colorGenerator_->getActorName(guid);
            std::string sourceName = actorName.empty() ? guid : std::string(actorName);

            for (const auto& cast : casts) {
                // Only include if this spell is in our hostile cast set
                if (hostileCastIds_.find(cast.spell_id) == hostileCastIds_.end()) continue;

                BossTimerEvent event;
                event.timestamp_ms = cast.end_timestamp_ms;
                event.spell_id = cast.spell_id;
                event.source_name = sourceName;
                event.type = BossTimerEventType::Cast;
                rawEvents_.push_back(event);
            }
        }
    }

    // Collect debuff applications from hostile/neutral sources
    if (auraDb_) {
        const auto& allAuras = auraDb_->getAllTargetAuras();
        for (const auto& [targetGuid, instances] : allAuras) {
            for (const auto& aura : instances) {
                if (aura.aura_type != AuraType::DEBUFF) continue;

                // Only include if this spell is in our hostile debuff set
                if (hostileDebuffIds_.find(aura.spell_id) == hostileDebuffIds_.end()) continue;

                auto it = actorFlags.find(std::string_view(aura.source_guid));
                if (it == actorFlags.end()) continue;
                if (!it->second.isHostile() && !it->second.isNeutral()) continue;

                std::string_view actorName = colorGenerator_->getActorName(aura.source_guid);
                std::string sourceName = actorName.empty() ? aura.source_guid : std::string(actorName);

                BossTimerEvent event;
                event.timestamp_ms = aura.applied_at_ms;
                event.spell_id = aura.spell_id;
                event.source_name = sourceName;
                event.type = BossTimerEventType::Debuff;
                rawEvents_.push_back(event);
            }
        }
    }

    // Collect damage events from hostile/neutral actors
    if (combatDb_) {
        const ActorMap* actorMap = combatDb_->getActorMap();
        if (actorMap) {
            for (const auto& [guidId, actorTable] : *actorMap) {
                // Resolve the interned id to the GUID string for flag/name lookups
                std::string_view guid = guidInterner().lookup(guidId);

                auto it = actorFlags.find(guid);
                if (it == actorFlags.end()) continue;
                if (!it->second.isHostile() && !it->second.isNeutral()) continue;

                std::string_view actorName = colorGenerator_->getActorName(guid);
                std::string sourceName = std::string(actorName.empty() ? guid : actorName);

                for (const auto& record : actorTable.damage_dealt_table) {
                    // Only include if this spell is in our hostile damage set
                    if (hostileDamageIds_.find(record.spell_id) == hostileDamageIds_.end()) continue;

                    BossTimerEvent event;
                    event.timestamp_ms = record.timestamp_ms;
                    event.spell_id = record.spell_id;
                    event.source_name = sourceName;
                    event.type = BossTimerEventType::Damage;
                    rawEvents_.push_back(event);
                }
            }
        }
    }

    std::sort(rawEvents_.begin(), rawEvents_.end(),
        [](const BossTimerEvent& a, const BossTimerEvent& b) {
            return a.timestamp_ms < b.timestamp_ms;
        });
}

void BossTimerStrip::groupAndFilterEvents() {
    if (rawEvents_.empty() || !settings_) return;

    float duration = static_cast<float>(maxTime_ - minTime_);
    if (duration <= 0) return;

    // Filter events based on settings
    std::vector<BossTimerEvent> filteredEvents;
    for (const auto& event : rawEvents_) {
        if (event.type == BossTimerEventType::Cast && !settings_->showCasts) continue;
        if (event.type == BossTimerEventType::Damage && !settings_->showDamage) continue;
        if (event.type == BossTimerEventType::Debuff && !settings_->showDebuffs) continue;

        // Check whitelist - spell must be explicitly enabled
        if (!settings_->isSpellEnabled(event.spell_id)) continue;

        filteredEvents.push_back(event);
    }

    if (filteredEvents.empty()) return;

    // User-configurable grouping window for identical spell IDs
    int32_t groupingWindowMs = static_cast<int32_t>(settings_->groupingWindowMs);

    // Track which events have been grouped (by index)
    std::vector<bool> grouped(filteredEvents.size(), false);

    // Group identical spell_id + type events within 5 second window
    for (size_t i = 0; i < filteredEvents.size(); ++i) {
        if (grouped[i]) continue;

        const auto& first = filteredEvents[i];
        grouped[i] = true;

        BossTimerEventGroup group;
        group.timestamp_ms = first.timestamp_ms;  // Use earliest timestamp
        group.spell_id = first.spell_id;
        group.source_name = first.source_name;
        group.type = first.type;
        group.count = 1;
        group.groupedTimestamps.push_back(first.timestamp_ms);  // Track first timestamp

        // Find all subsequent events with same spell_id and type within grouping window
        for (size_t j = i + 1; j < filteredEvents.size(); ++j) {
            if (grouped[j]) continue;

            const auto& next = filteredEvents[j];

            // Stop searching if we're past the grouping window
            if (next.timestamp_ms > first.timestamp_ms + groupingWindowMs) break;

            // Group if same spell_id and type
            if (next.spell_id == first.spell_id && next.type == first.type) {
                group.count++;
                group.groupedTimestamps.push_back(next.timestamp_ms);  // Track grouped timestamp
                grouped[j] = true;
            }
        }

        // Calculate normalized position using earliest timestamp
        group.normalizedPos = static_cast<float>(first.timestamp_ms - minTime_) / duration;
        group.normalizedPos = std::clamp(group.normalizedPos, 0.0f, 1.0f);

        eventGroups_.push_back(group);
    }
}

void BossTimerStrip::calculateRowAssignments() {
    if (eventGroups_.empty() || !settings_) {
        rowCount_ = 1;
        return;
    }

    float iconSize = settings_->iconSize;
    float duration = static_cast<float>(maxTime_ - minTime_);
    if (duration <= 0) {
        rowCount_ = 1;
        return;
    }

    // Estimate timeline width (assume typical window width)
    float estimatedWidth = 800.0f;
    float iconWidthNorm = iconSize / estimatedWidth;

    // Check for overlaps and assign rows
    // Simple greedy algorithm: for each event, find first row where it doesn't overlap
    std::vector<float> rowEndPositions;  // Track rightmost position in each row

    for (auto& group : eventGroups_) {
        float groupStart = group.normalizedPos - iconWidthNorm / 2.0f;
        float groupEnd = group.normalizedPos + iconWidthNorm / 2.0f;

        // Find first row where this event fits
        int assignedRow = -1;
        for (size_t r = 0; r < rowEndPositions.size(); r++) {
            if (groupStart >= rowEndPositions[r]) {
                assignedRow = static_cast<int>(r);
                rowEndPositions[r] = groupEnd;
                break;
            }
        }

        // No existing row works - add new row (up to 3 max)
        if (assignedRow < 0) {
            if (rowEndPositions.size() < 3) {
                assignedRow = static_cast<int>(rowEndPositions.size());
                rowEndPositions.push_back(groupEnd);
            } else {
                // Force into row 0 if we hit max rows
                assignedRow = 0;
            }
        }

        group.row = assignedRow;
    }

    rowCount_ = static_cast<int>(std::max(size_t(1), rowEndPositions.size()));
}

void BossTimerStrip::renderEventIcon(const BossTimerEventGroup& group, float x, float y, float iconSize) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    float iconX = x - (iconSize / 2.0f);
    float iconY = y;

    uint32_t borderColor = getEventTypeColor(group.type);
    drawList->AddRect(
        ImVec2(iconX - 1, iconY - 1),
        ImVec2(iconX + iconSize + 1, iconY + iconSize + 1),
        borderColor,
        2.0f,
        0,
        2.0f
    );

    bool hasIcon = false;
    if (iconLoader_) {
        ImTextureID iconTex = iconLoader_->getSpellIcon(group.spell_id);
        if (iconTex) {
            drawList->AddImage(
                iconTex,
                ImVec2(iconX, iconY),
                ImVec2(iconX + iconSize, iconY + iconSize)
            );
            hasIcon = true;
        }
    }

    // Fallback: draw colored placeholder
    if (!hasIcon) {
        drawList->AddRectFilled(
            ImVec2(iconX, iconY),
            ImVec2(iconX + iconSize, iconY + iconSize),
            IM_COL32(60, 60, 70, 255),
            2.0f
        );
    }

    if (group.count > 1) {
        char countStr[8];
        snprintf(countStr, sizeof(countStr), "%u", group.count);

        ImVec2 textSize = ImGui::CalcTextSize(countStr);
        float badgeX = iconX + iconSize - textSize.x - 2;
        float badgeY = iconY + iconSize - textSize.y;

        drawList->AddRectFilled(
            ImVec2(badgeX - 2, badgeY),
            ImVec2(badgeX + textSize.x + 2, badgeY + textSize.y),
            IM_COL32(0, 0, 0, 220),
            2.0f
        );
        drawList->AddText(ImVec2(badgeX, badgeY), IM_COL32(255, 255, 255, 255), countStr);
    }

    // Tooltip on hover and Ctrl+click for Wowhead
    ImVec2 mousePos = ImGui::GetMousePos();
    if (mousePos.x >= iconX && mousePos.x <= iconX + iconSize &&
        mousePos.y >= iconY && mousePos.y <= iconY + iconSize) {
        renderTooltip(group);
        SpellIconLoader::checkWowheadLookupRect(group.spell_id,
            ImVec2(iconX, iconY), ImVec2(iconX + iconSize, iconY + iconSize));

        // Right-click to add to Raid Notes (boss abilities are hostile)
        // Pass grouped timestamps so "all instances" only adds this group, not all casts
        ui::checkRaidNoteContextMenuRect(
            group.spell_id,
            SPELL_NAME(group.spell_id),
            group.timestamp_ms,
            true,  // Boss casts are hostile
            ImVec2(iconX, iconY), ImVec2(iconX + iconSize, iconY + iconSize),
            group.groupedTimestamps);
    }
}

void BossTimerStrip::renderTooltip(const BossTimerEventGroup& group) {
    ImGui::BeginTooltip();

    const char* spellName = SPELL_NAME(group.spell_id);
    ImGui::Text("%s", spellName);

    ImGui::TextDisabled("Source: %s", group.source_name.c_str());

    const char* typeStr = "Unknown";
    switch (group.type) {
        case BossTimerEventType::Cast: typeStr = "Cast"; break;
        case BossTimerEventType::Damage: typeStr = "Damage"; break;
        case BossTimerEventType::Debuff: typeStr = "Debuff"; break;
    }
    ImGui::TextDisabled("Type: %s", typeStr);

    int32_t timeMs = group.timestamp_ms;
    bool negative = timeMs < 0;
    if (negative) timeMs = -timeMs;
    int minutes = timeMs / 60000;
    int seconds = (timeMs % 60000) / 1000;
    int millis = timeMs % 1000;

    if (negative) {
        ImGui::TextDisabled("Time: -%d:%02d.%03d", minutes, seconds, millis);
    } else {
        ImGui::TextDisabled("Time: %d:%02d.%03d", minutes, seconds, millis);
    }

    if (group.count > 1) {
        ImGui::TextDisabled("Count: %u events", group.count);
    }

    ImGui::EndTooltip();
}

uint32_t BossTimerStrip::getEventTypeColor(BossTimerEventType type) const {
    switch (type) {
        case BossTimerEventType::Cast:
            return IM_COL32(70, 130, 180, 255);   // Steel blue
        case BossTimerEventType::Damage:
            return IM_COL32(178, 34, 34, 255);    // Firebrick red
        case BossTimerEventType::Debuff:
            return IM_COL32(148, 0, 211, 255);    // Dark violet
        default:
            return IM_COL32(100, 100, 100, 255);
    }
}
