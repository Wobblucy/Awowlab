#include "Encounter/EncounterScanner.h"
#include "Encounter/EncounterEventHandlers.h"
#include "Parser/Parser_Meta.h"
#include "Txttokenizer/Txttokenizer.h"
#include "../../Core/ErrorLogger.h"
#include <stack>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <memory>
#include <string>
#include <cstring>
#include <sstream>

// Helper to quickly check if a line contains an encounter-related event
// Returns pointer to event type string, or nullptr if not relevant
static const char* findEncounterEvent(const char* line, size_t len) {
    // Look for the event types we care about
    // WoW combat log format: "DATE TIME  EVENT_TYPE,..." (two spaces before the event type)
    // Example: "12/4/2025 21:32:21.665-7  ENCOUNTER_START,3135,..."
    //
    // We need to find the double-space separator, then check the event type

    const char* end = line + len;
    const char* p = line;

    // Find the double-space that separates timestamp from event data
    while (p + 1 < end) {
        if (*p == ' ' && *(p + 1) == ' ') {
            // Found double space, skip it
            p += 2;
            break;
        }
        p++;
    }

    if (p >= end) return nullptr;

    // Now p points to the start of the event type
    // Check if it matches our target events
    if (end - p >= 15 && std::memcmp(p, "ENCOUNTER_START", 15) == 0) return "ENCOUNTER_START";
    if (end - p >= 13 && std::memcmp(p, "ENCOUNTER_END", 13) == 0) return "ENCOUNTER_END";
    if (end - p >= 20 && std::memcmp(p, "CHALLENGE_MODE_START", 20) == 0) return "CHALLENGE_MODE_START";
    if (end - p >= 18 && std::memcmp(p, "CHALLENGE_MODE_END", 18) == 0) return "CHALLENGE_MODE_END";
    if (end - p >= 11 && std::memcmp(p, "ZONE_CHANGE", 11) == 0) return "ZONE_CHANGE";

    return nullptr;
}

// Lightweight scan - only tokenizes encounter marker lines
std::vector<EncounterSegment> EncounterScanner::scanForEncountersLightweight(
    const char* data,
    size_t size
) {
    // Reset log start timestamp for fresh scan to ensure consistent baseline
    parser::get_log_start_timestamp_ms() = 0;

    std::vector<EncounterSegment> encounters;
    // Store: lineIndex, nextLineByteOffset (where content AFTER the START line begins), startData
    std::stack<std::tuple<size_t, size_t, parser::EventParser<parser::EncounterStartTag>::EncounterStartData>> activeEncounters;
    std::optional<std::tuple<size_t, size_t, parser::EventParser<parser::ChallengeModeStartTag>::ChallengeModeStartData>> activeChallengeMode;
    std::vector<EncounterSegment> encountersWithinChallengeMode;

    tokenized_segment tokenizer;
    size_t lineIndex = 0;
    const char* lineStart = data;
    const char* end = data + size;

    // Close the open key as a dungeon segment ending just before the given
    // line. WoW often writes no CHALLENGE_MODE_END for a key that was left
    // or abandoned, so a raid pull, a zone change or the end of the log
    // closes it too, as the live session does.
    auto closeChallengeMode = [&](size_t endLineIndex, size_t endByteOffset,
                                  uint64_t endTimestamp, bool success) {
        auto [startLineIndex, contentStartOffset, startData] = activeChallengeMode.value();

        EncounterSegment segment;
        segment.segmentType = SegmentType::MythicPlusDungeon;
        segment.startLineIndex = startLineIndex + 1;
        segment.endLineIndex = endLineIndex;
        segment.startByteOffset = contentStartOffset;
        segment.endByteOffset = endByteOffset;
        segment.encounterId = 0;
        segment.encounterName = std::string(startData.zoneName);
        segment.difficultyId = 8;
        segment.groupSize = 5;
        segment.keystoneLevel = startData.keystoneLevel;
        segment.instanceId = startData.instanceId;
        segment.challengeModeId = startData.challengeModeId;
        segment.affixIds = startData.affixIds;

        // For encounter metadata, timestamps are relative to the encounter start
        // (startTimestamp_ms = 0, endTimestamp_ms = duration)
        // This is for display in the encounter selector list
        segment.startTimestamp_ms = 0;
        int64_t duration = static_cast<int64_t>(endTimestamp) - static_cast<int64_t>(startData.timestamp_ms);
        segment.endTimestamp_ms = (duration > 0) ? static_cast<int32_t>(duration) : 0;

        segment.isSuccess = success;

        encounters.push_back(segment);
        activeChallengeMode.reset();
        encountersWithinChallengeMode.clear();
    };

    // Last non-empty line, for the end time of a key still open at EOF
    const char* lastLineStart = nullptr;
    size_t lastLineLen = 0;

    while (lineStart < end) {
        // Find end of line
        const char* lineEnd = lineStart;
        while (lineEnd < end && *lineEnd != '\n') {
            lineEnd++;
        }

        size_t lineLen = lineEnd - lineStart;
        size_t lineByteOffset = lineStart - data;
        // Byte offset where NEXT line starts (after the newline)
        size_t nextLineByteOffset = (lineEnd < end) ? (lineEnd + 1 - data) : size;

        if (lineLen > 1) {
            lastLineStart = lineStart;
            lastLineLen = lineLen;
        }

        // Quick check if this line is an encounter event
        const char* eventType = findEncounterEvent(lineStart, lineLen);

        if (eventType) {
            // This line is an encounter event - tokenize just this line
            auto result = tokenizer.tokenize(lineStart, lineLen);
            if (!result.empty() && result[0].size() >= 3) {
                const auto& tokens = result[0];
                std::string eventStr(tokens[2]);

                if (eventStr == "ENCOUNTER_START" &&
                    tokens.size() >= parser::EventParser<parser::EncounterStartTag>::expected_token_count()) {

                    auto startData = parser::EventParser<parser::EncounterStartTag>::parse_and_return(tokens);
                    // A boss outside Mythic Keystone difficulty means the key is over
                    if (activeChallengeMode.has_value() && startData.difficultyId != 8) {
                        closeChallengeMode(lineIndex - 1, lineByteOffset, startData.timestamp_ms, false);
                    }
                    // Store the byte offset of the NEXT line (content starts after ENCOUNTER_START)
                    activeEncounters.push({lineIndex, nextLineByteOffset, startData});
                }
                else if (eventStr == "ENCOUNTER_END" &&
                         tokens.size() >= parser::EventParser<parser::EncounterEndTag>::expected_token_count()) {

                    auto endData = parser::EventParser<parser::EncounterEndTag>::parse_and_return(tokens);

                    if (!activeEncounters.empty()) {
                        auto [startLineIndex, contentStartOffset, startData] = activeEncounters.top();
                        activeEncounters.pop();

                        if (startData.encounterId == endData.encounterId) {
                            EncounterSegment segment;
                            segment.segmentType = SegmentType::BossEncounter;
                            segment.startLineIndex = startLineIndex + 1;
                            segment.endLineIndex = lineIndex - 1;
                            // Content starts AFTER ENCOUNTER_START, ends BEFORE ENCOUNTER_END
                            segment.startByteOffset = contentStartOffset;
                            segment.endByteOffset = lineByteOffset;  // Up to (not including) ENCOUNTER_END
                            segment.encounterId = startData.encounterId;
                            segment.encounterName = std::string(startData.encounterName);
                            segment.difficultyId = startData.difficultyId;
                            segment.groupSize = startData.groupSize;

                            // For encounter metadata, timestamps are relative to the encounter start
                            // (startTimestamp_ms = 0, endTimestamp_ms = duration)
                            // This is for display in the encounter selector list
                            segment.startTimestamp_ms = 0;
                            int64_t duration = static_cast<int64_t>(endData.timestamp_ms) - static_cast<int64_t>(startData.timestamp_ms);
                            segment.endTimestamp_ms = (duration > 0) ? static_cast<int32_t>(duration) : 0;

                            segment.isSuccess = (endData.success == 1);

                            if (activeChallengeMode.has_value()) {
                                encountersWithinChallengeMode.push_back(segment);
                            } else {
                                encounters.push_back(segment);
                            }
                        }
                    }
                }
                else if (eventStr == "CHALLENGE_MODE_START" &&
                         tokens.size() >= parser::EventParser<parser::ChallengeModeStartTag>::expected_token_count()) {

                    auto startData = parser::EventParser<parser::ChallengeModeStartTag>::parse_and_return(tokens);
                    // Use lineByteOffset (not nextLineByteOffset) to INCLUDE the CHALLENGE_MODE_START line
                    // This allows OutputWriter to find it and set the correct start timestamp
                    activeChallengeMode = {lineIndex, lineByteOffset, startData};
                    encountersWithinChallengeMode.clear();
                }
                else if (eventStr == "CHALLENGE_MODE_END" &&
                         tokens.size() >= parser::EventParser<parser::ChallengeModeEndTag>::expected_token_count()) {

                    auto endData = parser::EventParser<parser::ChallengeModeEndTag>::parse_and_return(tokens);

                    if (activeChallengeMode.has_value()) {
                        closeChallengeMode(lineIndex - 1, lineByteOffset, endData.timestamp_ms,
                                           endData.success == 1);
                    }
                }
                else if (eventStr == "ZONE_CHANGE" &&
                         tokens.size() >= parser::EventParser<parser::ZoneChangeTag>::expected_token_count()) {

                    auto zoneData = parser::EventParser<parser::ZoneChangeTag>::parse_and_return(tokens);
                    if (activeChallengeMode.has_value() &&
                        zoneData.zone_id != std::get<2>(activeChallengeMode.value()).instanceId) {
                        closeChallengeMode(lineIndex - 1, lineByteOffset,
                                           parser::EventParser<void>::parse_timestamp_ms(tokens), false);
                    }
                }
            }
        }

        // Move to next line
        lineIndex++;
        lineStart = lineEnd + 1;  // Skip the newline
    }

    // A key still open at the end of the log runs to the last line
    if (activeChallengeMode.has_value()) {
        uint64_t lastTimestamp = std::get<2>(activeChallengeMode.value()).timestamp_ms;
        if (lastLineStart) {
            auto result = tokenizer.tokenize(lastLineStart, lastLineLen);
            if (!result.empty() && result[0].size() >= 2) {
                lastTimestamp = parser::EventParser<void>::parse_timestamp_ms(result[0]);
            }
        }
        closeChallengeMode(lineIndex > 0 ? lineIndex - 1 : 0, size, lastTimestamp, false);
    }

    AWOW_LOG_INFO("SCAN", "Found " + std::to_string(encounters.size()) + " segment(s)");
    return encounters;
}

std::vector<EncounterSegment> EncounterScanner::scanForEncounters(
    const std::vector<std::vector<std::string_view>>& all_lines
) {
    std::vector<EncounterSegment> encounters;
    std::stack<std::pair<size_t, parser::EventParser<parser::EncounterStartTag>::EncounterStartData>> activeEncounters;
    std::optional<std::pair<size_t, parser::EventParser<parser::ChallengeModeStartTag>::ChallengeModeStartData>> activeChallengeMode;
    std::vector<EncounterSegment> encountersWithinChallengeMode;

    std::unordered_map<std::string, std::unique_ptr<EncounterEventHandler>> handlers;
    handlers["ENCOUNTER_START"] = std::make_unique<EncounterStartHandler>();
    handlers["ENCOUNTER_END"] = std::make_unique<EncounterEndHandler>();
    handlers["CHALLENGE_MODE_START"] = std::make_unique<ChallengeModeStartHandler>();
    handlers["CHALLENGE_MODE_END"] = std::make_unique<ChallengeModeEndHandler>();
    handlers["ZONE_CHANGE"] = std::make_unique<ZoneChangeHandler>();

    for (size_t i = 0; i < all_lines.size(); ++i) {
        const auto& tokens = all_lines[i];

        if (tokens.size() < 3) {
            continue;
        }

        std::string eventType(tokens[2]);

        auto it = handlers.find(eventType);
        if (it != handlers.end()) {
            EncounterScanContext ctx{
                encounters,
                activeEncounters,
                activeChallengeMode,
                encountersWithinChallengeMode,
                i
            };
            it->second->handle(tokens, ctx);
        }
    }

    // A key still open at the end of the log runs to the last line
    if (activeChallengeMode.has_value()) {
        uint64_t lastTimestamp = activeChallengeMode->second.timestamp_ms;
        for (size_t i = all_lines.size(); i-- > 0;) {
            if (all_lines[i].size() >= 2) {
                lastTimestamp = parser::EventParser<void>::parse_timestamp_ms(all_lines[i]);
                break;
            }
        }
        EncounterScanContext ctx{
            encounters,
            activeEncounters,
            activeChallengeMode,
            encountersWithinChallengeMode,
            all_lines.size()
        };
        closeChallengeMode(ctx, all_lines.empty() ? 0 : all_lines.size() - 1, lastTimestamp, false);
    }

    return encounters;
}
