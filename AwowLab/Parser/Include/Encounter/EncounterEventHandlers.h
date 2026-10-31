#pragma once
#include "Encounter/EncounterSegment.h"
#include "Parser/Parser_Encounter.h"
#include "Parser/Parser_ChallengeMode.h"
#include "Parser/Parser_Meta.h"
#include <vector>
#include <string_view>
#include <stack>
#include <optional>
#include <iostream>

struct EncounterScanContext {
    std::vector<EncounterSegment>& encounters;
    std::stack<std::pair<size_t, parser::EventParser<parser::EncounterStartTag>::EncounterStartData>>& activeEncounters;
    std::optional<std::pair<size_t, parser::EventParser<parser::ChallengeModeStartTag>::ChallengeModeStartData>>& activeChallengeMode;
    std::vector<EncounterSegment>& encountersWithinChallengeMode;
    size_t currentLineIndex;
};

// Close the open key as a dungeon segment ending at endLineIndex. WoW often
// writes no CHALLENGE_MODE_END for a key that was left or abandoned, so a
// raid pull, a zone change or the end of the log closes it as well.
inline void closeChallengeMode(EncounterScanContext& ctx, size_t endLineIndex,
                               uint64_t endTimestamp, bool success) {
    auto [startLineIndex, startData] = ctx.activeChallengeMode.value();

    EncounterSegment segment;
    segment.segmentType = SegmentType::MythicPlusDungeon;
    segment.startLineIndex = startLineIndex + 1;
    segment.endLineIndex = endLineIndex;
    segment.encounterId = 0;
    segment.encounterName = std::string(startData.zoneName);
    segment.difficultyId = 8;
    segment.groupSize = 5;
    segment.keystoneLevel = startData.keystoneLevel;
    segment.instanceId = startData.instanceId;
    segment.challengeModeId = startData.challengeModeId;
    segment.affixIds = startData.affixIds;
    segment.startTimestamp_ms = static_cast<uint32_t>(
        startData.timestamp_ms - parser::get_log_start_timestamp_ms()
    );
    segment.endTimestamp_ms = static_cast<uint32_t>(
        endTimestamp - parser::get_log_start_timestamp_ms()
    );
    segment.isSuccess = success;

    ctx.encounters.push_back(segment);
    ctx.activeChallengeMode.reset();
    ctx.encountersWithinChallengeMode.clear();
}

class EncounterEventHandler {
public:
    virtual ~EncounterEventHandler() = default;
    virtual void handle(const std::vector<std::string_view>& tokens, EncounterScanContext& ctx) = 0;
};

class EncounterStartHandler : public EncounterEventHandler {
public:
    void handle(const std::vector<std::string_view>& tokens, EncounterScanContext& ctx) override {
        if (tokens.size() < parser::EventParser<parser::EncounterStartTag>::expected_token_count()) {
            return;
        }

        auto startData = parser::EventParser<parser::EncounterStartTag>::parse_and_return(tokens);
        // A boss outside Mythic Keystone difficulty means the key is over
        if (ctx.activeChallengeMode.has_value() && startData.difficultyId != 8) {
            closeChallengeMode(ctx, ctx.currentLineIndex - 1, startData.timestamp_ms, false);
        }
        ctx.activeEncounters.push({ctx.currentLineIndex, startData});
    }
};

class EncounterEndHandler : public EncounterEventHandler {
public:
    void handle(const std::vector<std::string_view>& tokens, EncounterScanContext& ctx) override {
        if (tokens.size() < parser::EventParser<parser::EncounterEndTag>::expected_token_count()) {
            return;
        }

        auto endData = parser::EventParser<parser::EncounterEndTag>::parse_and_return(tokens);

        if (ctx.activeEncounters.empty()) {
            return;
        }

        auto [startLineIndex, startData] = ctx.activeEncounters.top();
        ctx.activeEncounters.pop();

        if (startData.encounterId != endData.encounterId) {
            return;
        }

        EncounterSegment segment;
        segment.segmentType = SegmentType::BossEncounter;
        segment.startLineIndex = startLineIndex + 1;
        segment.endLineIndex = ctx.currentLineIndex - 1;
        segment.encounterId = startData.encounterId;
        segment.encounterName = std::string(startData.encounterName);
        segment.difficultyId = startData.difficultyId;
        segment.groupSize = startData.groupSize;
        segment.startTimestamp_ms = static_cast<uint32_t>(
            startData.timestamp_ms - parser::get_log_start_timestamp_ms()
        );
        segment.endTimestamp_ms = static_cast<uint32_t>(
            endData.timestamp_ms - parser::get_log_start_timestamp_ms()
        );
        segment.isSuccess = (endData.success == 1);

        if (ctx.activeChallengeMode.has_value()) {
            ctx.encountersWithinChallengeMode.push_back(segment);
        } else {
            ctx.encounters.push_back(segment);
        }
    }
};

class ChallengeModeStartHandler : public EncounterEventHandler {
public:
    void handle(const std::vector<std::string_view>& tokens, EncounterScanContext& ctx) override {
        if (tokens.size() < parser::EventParser<parser::ChallengeModeStartTag>::expected_token_count()) {
            return;
        }

        auto startData = parser::EventParser<parser::ChallengeModeStartTag>::parse_and_return(tokens);

        ctx.activeChallengeMode = {ctx.currentLineIndex, startData};
        ctx.encountersWithinChallengeMode.clear();
    }
};

class ChallengeModeEndHandler : public EncounterEventHandler {
public:
    void handle(const std::vector<std::string_view>& tokens, EncounterScanContext& ctx) override {
        if (tokens.size() < parser::EventParser<parser::ChallengeModeEndTag>::expected_token_count()) {
            return;
        }

        auto endData = parser::EventParser<parser::ChallengeModeEndTag>::parse_and_return(tokens);

        if (!ctx.activeChallengeMode.has_value()) {
            return;
        }

        closeChallengeMode(ctx, ctx.currentLineIndex - 1, endData.timestamp_ms,
                           endData.success == 1);
    }
};

class ZoneChangeHandler : public EncounterEventHandler {
public:
    void handle(const std::vector<std::string_view>& tokens, EncounterScanContext& ctx) override {
        if (tokens.size() < parser::EventParser<parser::ZoneChangeTag>::expected_token_count()) {
            return;
        }

        auto zoneData = parser::EventParser<parser::ZoneChangeTag>::parse_and_return(tokens);
        if (ctx.activeChallengeMode.has_value() &&
            zoneData.zone_id != ctx.activeChallengeMode->second.instanceId) {
            closeChallengeMode(ctx, ctx.currentLineIndex - 1,
                               parser::EventParser<void>::parse_timestamp_ms(tokens), false);
        }
    }
};
