#include "LiveLogSession.h"
#include "Txttokenizer/Txttokenizer.h"
#include "Parser/Parser.h"
#include "Parser/Parser_Damage.h"
#include "Parser/Parser_Heal.h"
#include "Parser/Parser_Encounter.h"
#include "Parser/Parser_ChallengeMode.h"
#include "Parser/Parser_Absorb.h"
#include "Parser/Parser_Missed.h"
#include "Parser/Parser_Dispel.h"
#include "Parser/Parser_Unit.h"
#include "Parser/Parser_Aura.h"
#include "Parser/Parser_Cast.h"
#include "Parser/Parser_Combatant_Info.h"
#include "Parser/Parser_Support.h"
#include "Core/StringInterner.h"
#include "Core/Utils/ColorUtils.h"  // awow::getActorTypeFromGuid
#include "Database/EmoteText.h"     // awow::emote::emoteTextFromTokens

#include <fstream>
#include <iostream>
#include <algorithm>
#include <charconv>

namespace {

// Events that count as combat for pull tracking: damage, healing, absorbs,
// misses, dispels, interrupts and steals. Deaths, auras and encounter
// boundaries do not open a pull.
bool isCombatEventType(std::string_view eventType) {
    return eventType == "SPELL_DAMAGE" || eventType == "RANGE_DAMAGE" ||
           eventType == "SPELL_PERIODIC_DAMAGE" || eventType == "SWING_DAMAGE_LANDED" ||
           eventType == "DAMAGE_SPLIT" ||
           eventType == "SPELL_HEAL" || eventType == "SPELL_PERIODIC_HEAL" ||
           eventType == "SPELL_DAMAGE_SUPPORT" || eventType == "SPELL_PERIODIC_DAMAGE_SUPPORT" ||
           eventType == "SWING_DAMAGE_LANDED_SUPPORT" ||
           eventType == "SPELL_HEAL_SUPPORT" || eventType == "SPELL_PERIODIC_HEAL_SUPPORT" ||
           eventType == "SPELL_ABSORBED" || eventType == "SPELL_ABSORBED_SUPPORT" ||
           eventType == "SPELL_MISSED" || eventType == "SPELL_PERIODIC_MISSED" ||
           eventType == "RANGE_MISSED" || eventType == "SWING_MISSED" ||
           eventType == "SPELL_DISPEL" || eventType == "SPELL_INTERRUPT" ||
           eventType == "SPELL_STOLEN";
}

// The event name of a segment boundary line (ENCOUNTER_START/END or
// CHALLENGE_MODE_START/END), or empty for any other line.
//
// Lines look like "M/D/YYYY HH:MM:SS.mmm-Z  EVENT_NAME,...", so the event
// name runs from the last space before the first comma up to that comma.
// Almost every line is a damage or heal event, and all four boundary events
// start with 'E' or 'C', so one character rejects most lines before any
// string comparison.
std::string_view boundaryEventName(std::string_view line) {
    const size_t commaPos = line.find(',');
    if (commaPos == std::string_view::npos || commaPos == 0) return {};

    size_t nameStart = line.rfind(' ', commaPos - 1);
    if (nameStart == std::string_view::npos) return {};
    ++nameStart;
    if (nameStart >= commaPos) return {};

    const char first = line[nameStart];
    if (first != 'E' && first != 'C') return {};

    const std::string_view name = line.substr(nameStart, commaPos - nameStart);
    if (name == "ENCOUNTER_START" || name == "ENCOUNTER_END" ||
        name == "CHALLENGE_MODE_START" || name == "CHALLENGE_MODE_END") {
        return name;
    }
    return {};
}

} // namespace

LiveLogSession::LiveLogSession() = default;

LiveLogSession::~LiveLogSession() {
    detach();
}

bool LiveLogSession::attach(const std::filesystem::path& logPath, bool scanExisting) {
    if (!std::filesystem::exists(logPath)) {
        return false;
    }

    detach();

    logPath_ = logPath;
    lastParsedOffset_ = 0;
    lastParseTime_ = std::chrono::steady_clock::now();

    liveBundle_.clear();
    historicalBundle_.clear();
    displayHistorical_ = false;
    viewState_ = SessionViewState::Live;
    guidToName_.clear();
    spellIdToName_.clear();
    guidToSpecId_.clear();
    encounters_.clear();
    firstCasts_.clear();
    emoteEvents_.clear();
    pullHistory_.clear();
    currentPull_ = PullSegment{};
    nextPullNumber_ = 1;
    currentTrashNumber_ = 0;
    currentBossNumber_ = 0;
    combatState_ = CombatState::Idle;
    lastCombatEventTime_ms_ = 0;
    logStartTimestampSet_ = false;
    inMythicPlus_ = false;
    currentKeystoneLevel_ = 0;

    // Only segment boundaries are found here, not full events. That still
    // reads the whole file, which takes seconds on a large raid log, so the
    // meter panel's spinner is shown for it.
    if (scanExisting) {
        parsingInProgress_.store(true);
        scanForSegments();
        parsingInProgress_.store(false);
    }

    return true;
}

void LiveLogSession::detach() {
    logPath_.clear();
    lastParsedOffset_ = 0;
}

size_t LiveLogSession::getCurrentFileSize() const {
    if (logPath_.empty()) return 0;

    try {
        return std::filesystem::file_size(logPath_);
    } catch (const std::filesystem::filesystem_error&) {
        return 0;
    }
}

bool LiveLogSession::poll() {
    if (!isAttached()) return false;

    // The live parse runs even while a past segment is on screen. That view
    // reads historicalBundle_, so the live bundle keeps accumulating and
    // nothing is missed when the user returns to Current.
    size_t currentSize = getCurrentFileSize();
    if (currentSize <= lastParsedOffset_) {
        return false;
    }

    std::ifstream file(logPath_, std::ios::binary);
    if (!file) {
        return false;
    }

    file.seekg(lastParsedOffset_);
    if (!file) {
        return false;
    }

    size_t bytesToRead = currentSize - lastParsedOffset_;
    std::string fullBuffer(bytesToRead, '\0');
    file.read(fullBuffer.data(), static_cast<std::streamsize>(bytesToRead));
    size_t bytesRead = static_cast<size_t>(file.gcount());

    if (bytesRead == 0) {
        return false;
    }

    // Only whole lines are parsed. A line WoW is still writing stays past
    // lastParsedOffset_, so the next poll reads it again from its start.
    size_t lastNewline = fullBuffer.rfind('\n', bytesRead - 1);
    if (lastNewline == std::string::npos) {
        return false;
    }
    fullBuffer.resize(lastNewline + 1);
    const size_t bytesConsumed = lastNewline + 1;

    tokenized_segment tokenizer;
    auto result = tokenizer.tokenize(fullBuffer.c_str(), fullBuffer.size());

    if (result.empty()) {
        // Blank lines only; still move past them
        lastParsedOffset_ += bytesConsumed;
        return false;
    }

    // Parse in slices and re-take the ActorMap lock for each one. A busy
    // poll can carry thousands of lines, and holding the lock for all of
    // them stalls the render thread. Slices end on line boundaries, so the
    // UI always sees a consistent prefix of the log.
    constexpr size_t kLinesPerSlice = 256;
    for (size_t firstLine = 0; firstLine < result.size(); firstLine += kLinesPerSlice) {
        std::lock_guard<std::timed_mutex> lock(actorMapMutex_);
        processLines(result, fullBuffer, lastParsedOffset_, firstLine, kLinesPerSlice);
    }

    {
        std::lock_guard<std::timed_mutex> lock(actorMapMutex_);
        lastParsedOffset_ += bytesConsumed;
        lastParseTime_ = std::chrono::steady_clock::now();
        updateSnapshot();
    }

    // Called outside the lock so the callback can query the session
    if (onDataUpdate_) {
        onDataUpdate_();
    }

    return true;
}

void LiveLogSession::processLines(const std::vector<std::vector<std::string_view>>& lines,
                                  std::string_view buffer,
                                  size_t bufferOffset,
                                  size_t firstLine,
                                  size_t lineCount) {
    size_t endLine = (lineCount >= lines.size() - firstLine) ? lines.size()
                                                             : firstLine + lineCount;

    for (size_t lineIdx = firstLine; lineIdx < endLine; ++lineIdx) {
        const auto& tokens = lines[lineIdx];
        if (tokens.size() < 3) {
            continue;
        }

        // Tokens are views into buffer, so the first one gives the line's
        // real file position (quotes and separators are not in the tokens).
        const size_t currentByteOffset =
            bufferOffset + static_cast<size_t>(tokens[0].data() - buffer.data());

        std::string_view eventType = tokens[2];

        // Epoch ms, date-aware so a pull across midnight keeps increasing
        int64_t absolute_ms = parseTimestampMs(tokens[0], tokens[1]);

        if (!logStartTimestampSet_ && absolute_ms > 0) {
            logStartTimestamp_ = absolute_ms;
            logStartTimestampSet_ = true;
        }

        int32_t timestamp_ms = static_cast<int32_t>(absolute_ms - logStartTimestamp_);

        if (eventType == "ENCOUNTER_START") {
            handleEncounterStart(tokens, currentByteOffset);
        } else if (eventType == "ENCOUNTER_END") {
            handleEncounterEnd(tokens, currentByteOffset);
        } else if (eventType == "CHALLENGE_MODE_START") {
            handleChallengeModeStart(tokens);
        } else if (eventType == "CHALLENGE_MODE_END") {
            handleChallengeModeEnd(tokens);
        }

        if (combatState_ == CombatState::InCombat) {
            checkIdleTimeout(timestamp_ms, currentByteOffset);
        }

        // A combat event while idle opens a trash pull, which clears the
        // last pull's data, so it has to happen before the event is stored.
        // parseAndStoreEvent is shared with the historical replay.
        if (isCombatEventType(eventType)) {
            handleCombatEvent(timestamp_ms, currentByteOffset);
        }
        (void)parseAndStoreEvent(liveBundle_, nullptr, tokens, eventType, timestamp_ms);
    }
}

void LiveLogSession::handleCombatEvent(int32_t timestamp_ms, size_t byteOffset) {
    lastCombatEventTime_ms_ = timestamp_ms;

    if (combatState_ == CombatState::Idle) {
        ++currentTrashNumber_;
        std::string label = "Trash #" + std::to_string(currentTrashNumber_);
        startNewPull(timestamp_ms, byteOffset, label);
        currentPull_.segmentType = PullSegmentType::TrashPull;
        currentPull_.trashNumber = currentTrashNumber_;
        combatState_ = CombatState::InCombat;
    }
}

void LiveLogSession::handleEncounterStart(const std::vector<std::string_view>& tokens, size_t byteOffset) {
    if (tokens.size() < parser::EventParser<parser::EncounterStartTag>::expected_token_count()) {
        return;
    }

    auto data = parser::EventParser<parser::EncounterStartTag>::parse_and_return(tokens);
    int32_t timestamp_ms = static_cast<int32_t>(data.timestamp_ms - logStartTimestamp_);

    // Close the preceding trash pull or boss attempt
    if (combatState_ != CombatState::Idle) {
        endCurrentPull(timestamp_ms, byteOffset);
        combatState_ = CombatState::Idle;
    }

    // The first raid encounter of the session starts a run, so a raid
    // night is grouped in the UI the way an M+ key is. pullHistory_ is
    // kept so earlier wipes stay selectable.
    if (!inMythicPlus_ && encounters_.empty()) {
        ++currentDungeonRunId_;
        dungeonStartTime_ms_ = timestamp_ms;
        currentDungeonName_ = std::string(data.encounterName);
        if (onDungeonStart_) {
            onDungeonStart_(currentDungeonRunId_, currentDungeonName_, 0);
        }
    }

    // Keep the preceding trash data (clearData=false): the boss's own
    // records won't flush until the kill, and the live view falls back to
    // session-so-far in the meantime.
    beginBossPull(data.encounterName, data.encounterId, data.difficultyId,
                  timestamp_ms, byteOffset, /*clearData=*/false);
}

void LiveLogSession::handleEncounterEnd(const std::vector<std::string_view>& tokens, size_t byteOffset) {
    if (tokens.size() < parser::EventParser<parser::EncounterEndTag>::expected_token_count()) {
        return;
    }

    auto data = parser::EventParser<parser::EncounterEndTag>::parse_and_return(tokens);
    int32_t timestamp_ms = static_cast<int32_t>(data.timestamp_ms - logStartTimestamp_);
    finishBossPull(data.success == 1, timestamp_ms, byteOffset);
}

void LiveLogSession::beginBossPull(std::string_view encounterName, uint32_t encounterId,
                                   uint32_t difficultyId, int32_t timestamp_ms,
                                   size_t byteOffset, bool clearData) {
    combatState_ = CombatState::InEncounter;
    ++currentBossNumber_;
    std::string label = inMythicPlus_
        ? "Boss " + std::to_string(currentBossNumber_) + ": " + std::string(encounterName)
        : std::string(encounterName);  // Raids use plain boss name
    startNewPull(timestamp_ms, byteOffset, label, clearData);

    currentPull_.isEncounter = true;
    currentPull_.segmentType = PullSegmentType::BossPull;
    currentPull_.bossNumber = currentBossNumber_;
    currentPull_.encounterId = encounterId;
    currentPull_.difficultyId = difficultyId;
    currentPull_.inMythicPlus = inMythicPlus_;
    currentPull_.keystoneLevel = currentKeystoneLevel_;

    EncounterSegment segment;
    segment.encounterName = std::string(encounterName);
    segment.encounterId = encounterId;
    segment.difficultyId = difficultyId;
    segment.startByteOffset = byteOffset;
    segment.startTimestamp_ms = timestamp_ms;
    encounters_.push_back(segment);
}

void LiveLogSession::finishBossPull(bool success, int32_t timestamp_ms, size_t byteOffset) {
    currentPull_.success = success;

    if (!encounters_.empty()) {
        auto& segment = encounters_.back();
        segment.endByteOffset = byteOffset;
        segment.endTimestamp_ms = timestamp_ms;
        segment.isSuccess = success;
    }

    endCurrentPull(timestamp_ms, byteOffset);
    combatState_ = CombatState::Idle;
}

void LiveLogSession::pushTrashSegment(size_t startOffset, size_t endOffset,
                                      int32_t startTime_ms, int32_t endTime_ms) {
    ++currentTrashNumber_;
    PullSegment trash;
    trash.label = "Trash #" + std::to_string(currentTrashNumber_);
    trash.segmentType = PullSegmentType::TrashPull;
    trash.trashNumber = currentTrashNumber_;
    trash.startByteOffset = startOffset;
    trash.endByteOffset = endOffset;
    trash.startTime_ms = startTime_ms;
    trash.endTime_ms = endTime_ms;
    trash.dungeonRunId = currentDungeonRunId_;
    trash.inMythicPlus = true;
    trash.keystoneLevel = currentKeystoneLevel_;
    trash.pullNumber = nextPullNumber_++;
    stampGroupInfo(trash);
    pullHistory_.push_back(trash);
}

const PullSegment& LiveLogSession::pushDungeonOverall(size_t endOffset, int32_t endTime_ms) {
    PullSegment overall;
    overall.label = "Overall - " + currentDungeonName_;
    overall.segmentType = PullSegmentType::DungeonOverall;
    overall.startByteOffset = challengeModeStartOffset_;
    overall.endByteOffset = endOffset;
    overall.startTime_ms = challengeModeStartTime_ms_;
    overall.endTime_ms = endTime_ms;
    overall.dungeonRunId = currentDungeonRunId_;
    overall.inMythicPlus = true;
    overall.keystoneLevel = currentKeystoneLevel_;
    overall.pullNumber = nextPullNumber_++;
    stampGroupInfo(overall);
    pullHistory_.push_back(overall);
    return pullHistory_.back();
}

void LiveLogSession::handleChallengeModeStart(const std::vector<std::string_view>& tokens) {
    if (tokens.size() < parser::EventParser<parser::ChallengeModeStartTag>::expected_token_count()) {
        return;
    }

    auto data = parser::EventParser<parser::ChallengeModeStartTag>::parse_and_return(tokens);
    int32_t timestamp_ms = static_cast<int32_t>(
        parseTimestampMs(tokens[0], tokens[1]) - logStartTimestamp_);

    // An open pull is closed and its data stays on screen until the first
    // trash pull of the new key clears it. With no pull open, events since
    // the last pull (leftover buffs and the like) are dropped now.
    if (combatState_ != CombatState::Idle) {
        endCurrentPull(lastCombatEventTime_ms_, lastParsedOffset_);
        combatState_ = CombatState::Idle;
    } else {
        clearLivePullData();
    }

    // Only the per-run state is reset. pullHistory_ is grouped by
    // dungeonRunId, so earlier runs stay in the selector, and the name
    // caches are kept across runs.
    closeCurrentRun(lastParsedOffset_, timestamp_ms);

    encounters_.clear();
    currentPull_ = PullSegment{};
    currentTrashNumber_ = 0;
    currentBossNumber_ = 0;
    lastCombatEventTime_ms_ = 0;

    ++currentDungeonRunId_;
    dungeonStartTime_ms_ = timestamp_ms;
    currentDungeonName_ = std::string(data.zoneName);
    inMythicPlus_ = true;
    currentKeystoneLevel_ = data.keystoneLevel;

    // Remember where the run began so CHALLENGE_MODE_END can build the
    // "Overall - <dungeon>" segment spanning the whole key.
    challengeModeStartOffset_ = lastParsedOffset_;
    challengeModeStartTime_ms_ = timestamp_ms;

    if (onDungeonStart_) {
        onDungeonStart_(currentDungeonRunId_, currentDungeonName_, currentKeystoneLevel_);
    }
}

void LiveLogSession::handleChallengeModeEnd(const std::vector<std::string_view>& tokens) {
    int32_t timestamp_ms = (tokens.size() >= 2)
        ? static_cast<int32_t>(parseTimestampMs(tokens[0], tokens[1]) - logStartTimestamp_)
        : lastCombatEventTime_ms_;

    if (combatState_ != CombatState::Idle) {
        endCurrentPull(lastCombatEventTime_ms_, lastParsedOffset_);
        combatState_ = CombatState::Idle;
    }

    // The "Overall - <dungeon>" segment spanning the whole key, matching
    // the one the offline scan builds. This checks inMythicPlus_ rather
    // than the start offset, which is 0 when CHALLENGE_MODE_START was in
    // the first poll batch (lastParsedOffset_ only advances after a batch).
    if (inMythicPlus_) {
        // A copy: the callback must not hold a reference into pullHistory_
        const PullSegment overall = pushDungeonOverall(lastParsedOffset_, timestamp_ms);
        if (onPullEnd_) {
            onPullEnd_(overall);
        }
    }

    // The run has an Overall segment by now, so no placeholder is added
    closeCurrentRun(lastParsedOffset_, timestamp_ms);

    inMythicPlus_ = false;
    currentKeystoneLevel_ = 0;
    challengeModeStartOffset_ = 0;
}

void LiveLogSession::startNewPull(int32_t timestamp_ms, size_t byteOffset,
                                  const std::string& label, bool clearData) {
    // endCurrentPull() leaves the last pull's data in place so the live
    // view does not go blank between pulls; it is cleared here instead.
    //
    // A boss ENCOUNTER_START passes clearData=false. Boss records are only
    // written when the kill flushes them, so clearing would leave the view
    // empty for the whole fight. The boss segment's own time window filters
    // the older trash out (getTimeRange, CurrentPull case), and the next
    // trash pull clears, so memory stays bounded.
    if (clearData) {
        clearLivePullData();
    }

    currentPull_ = PullSegment{};
    currentPull_.startByteOffset = byteOffset;
    currentPull_.startTime_ms = timestamp_ms;
    currentPull_.label = label;
    currentPull_.pullNumber = nextPullNumber_++;
    currentPull_.inMythicPlus = inMythicPlus_;
    currentPull_.keystoneLevel = currentKeystoneLevel_;
    currentPull_.dungeonRunId = currentDungeonRunId_;
    stampGroupInfo(currentPull_);

    if (onPullStart_) {
        onPullStart_(currentPull_);
    }
}

void LiveLogSession::stampGroupInfo(PullSegment& seg) const {
    // Only M+ segments are grouped under a header. Raid pulls keep an empty
    // dungeonName and are listed flat. dungeonEndTime_ms stays 0 until the
    // run is closed.
    if (!seg.inMythicPlus) return;
    seg.dungeonName = currentDungeonName_;
    seg.dungeonStartTime_ms = challengeModeStartTime_ms_;
}

void LiveLogSession::closeCurrentRun(size_t endOffset, int32_t endTime_ms) {
    if (!inMythicPlus_ || currentDungeonRunId_ == 0) return;

    bool hasSegments = false;
    for (const auto& seg : pullHistory_) {
        if (seg.dungeonRunId == currentDungeonRunId_) { hasSegments = true; break; }
    }

    // A key that was started and abandoned writes no combat and so has no
    // segments. A placeholder keeps it visible as its own group.
    if (!hasSegments) {
        PullSegment empty;
        empty.label = "No log writes";
        empty.segmentType = PullSegmentType::EmptyRun;
        empty.startByteOffset = challengeModeStartOffset_;
        empty.endByteOffset = endOffset;
        empty.startTime_ms = challengeModeStartTime_ms_;
        empty.endTime_ms = endTime_ms;
        empty.dungeonRunId = currentDungeonRunId_;
        empty.inMythicPlus = true;
        empty.keystoneLevel = currentKeystoneLevel_;
        empty.pullNumber = nextPullNumber_++;
        stampGroupInfo(empty);
        pullHistory_.push_back(empty);
    }

    // Every segment of the run gets its end time so the group header can
    // show the key's duration. WoW rarely writes CHALLENGE_MODE_END for a
    // key that was left, so this runs at any run boundary: the next key
    // start, CHALLENGE_MODE_END or the end of the log.
    for (auto& seg : pullHistory_) {
        if (seg.dungeonRunId == currentDungeonRunId_ && seg.inMythicPlus) {
            seg.dungeonEndTime_ms = endTime_ms;
        }
    }
}

void LiveLogSession::clearLivePullData() {
    liveBundle_.clear();
    firstCasts_.clear();
    emoteEvents_.clear();
}

void LiveLogSession::appendResourceSnapshot(CombatDataBundle& target,
                                            std::string_view source_guid,
                                            std::string_view dest_guid,
                                            const AdvancedUnitInfo& info,
                                            int32_t timestamp_ms) {
    // The advanced info block describes whichever actor info_guid names:
    // usually the target on damage, either side on heals.
    std::string_view actorGuid;
    if (info.info_guid == source_guid) {
        actorGuid = source_guid;
    } else if (info.info_guid == dest_guid) {
        actorGuid = dest_guid;
    } else {
        return;
    }
    if (actorGuid.empty()) return;
    if (info.max_hp == 0) return;  // Advanced logging is off

    ResourceStatusRecord record{};
    record.timestamp_ms = timestamp_ms;
    record.current_health = info.current_hp;
    record.max_health = info.max_hp;
    record.current_power = info.current_power;
    record.max_power = info.max_power;
    record.powertype = info.power_type;

    // ResourceDatabase binary-searches this table, so it must stay sorted.
    // Both the live poll and segment replay append in log order into a
    // table that started empty, so push_back keeps it sorted.
    target.actorMap[guidInterner().intern(actorGuid)].resource_status_table.push_back(record);
}

void LiveLogSession::endCurrentPull(int32_t timestamp_ms, size_t byteOffset) {
    currentPull_.endTime_ms = timestamp_ms;
    currentPull_.endByteOffset = byteOffset;

    pullHistory_.push_back(currentPull_);

    // The data is not cleared here, so the overlay keeps showing the
    // finished fight until the next pull starts.

    if (onPullEnd_) {
        onPullEnd_(currentPull_);
    }
}

void LiveLogSession::checkIdleTimeout(int32_t /*currentTime_ms*/, size_t /*byteOffset*/) {
    // Intentionally empty. Idle time does not end a trash pull: one trash
    // segment covers everything between two bosses, and only a boss start,
    // a new key or a session reset closes it. This keeps a single entry per
    // trash block in the segment list.
}

bool LiveLogSession::parseByteRangeInto(CombatDataBundle& target,
                                        SessionLookups& lookups,
                                        size_t startByteOffset, size_t endByteOffset,
                                        int64_t segmentAbsoluteStart) {
    if (startByteOffset >= endByteOffset) return false;

    std::ifstream file(logPath_, std::ios::binary);
    if (!file) return false;

    file.seekg(static_cast<std::streamoff>(startByteOffset));
    if (!file) return false;

    size_t bytesToRead = endByteOffset - startByteOffset;
    std::string fullBuffer(bytesToRead, '\0');
    file.read(fullBuffer.data(), static_cast<std::streamsize>(bytesToRead));
    size_t bytesRead = static_cast<size_t>(file.gcount());
    if (bytesRead == 0) return false;
    fullBuffer.resize(bytesRead);

    // The byte window can cut lines, so drop a partial first line (unless
    // this is the file start) and a partial last line.
    if (startByteOffset > 0) {
        size_t firstNewline = fullBuffer.find('\n');
        if (firstNewline != std::string::npos) {
            fullBuffer.erase(0, firstNewline + 1);
        }
    }
    size_t lastNewline = fullBuffer.rfind('\n');
    if (lastNewline != std::string::npos && lastNewline < fullBuffer.size() - 1) {
        fullBuffer.resize(lastNewline + 1);
    }
    if (fullBuffer.empty()) return false;

    tokenized_segment tokenizer;
    auto result = tokenizer.tokenize(fullBuffer.c_str(), fullBuffer.size());
    if (result.empty()) return false;

    // Replay only fills the bundle; pull tracking is left alone
    for (const auto& tokens : result) {
        if (tokens.size() < 3) continue;
        std::string_view eventType = tokens[2];
        int32_t timestamp_ms = static_cast<int32_t>(
            parseTimestampMs(tokens[0], tokens[1]) - segmentAbsoluteStart);
        (void)parseAndStoreEvent(target, &lookups, tokens, eventType, timestamp_ms);
    }

    return true;
}

bool LiveLogSession::parseSegment(const PullSegment& segment) {
    if (!isAttached()) return false;
    if (segment.startByteOffset >= segment.endByteOffset) return false;

    // The parse itself does not hold actorMapMutex_, so the live poll keeps
    // running. It builds a scratch bundle under historicalMutex_, and only
    // the swap into place takes actorMapMutex_, so the render thread sees
    // the bundle and the view state change together.
    parsingInProgress_.store(true);

    // Timestamps are rebased to the segment start so charts, duration and
    // per-second numbers are relative to this pull, as in the offline
    // OutputWriter. logStartTimestamp_ is only written during the initial
    // scan, so reading it from the worker thread is safe.
    const int64_t segmentAbsoluteStart = logStartTimestamp_ + segment.startTime_ms;

    CombatDataBundle scratch;
    SessionLookups lookups = copyPetOwners();
    {
        std::lock_guard<std::mutex> histLock(historicalMutex_);
        parseByteRangeInto(scratch, lookups, segment.startByteOffset,
                           segment.endByteOffset, segmentAbsoluteStart);
    }

    {
        // The historical bundle is not modified again until the next
        // selection or returnToLiveMode(), so the render thread can read it
        // under this lock while the live poll carries on.
        std::lock_guard<std::timed_mutex> lock(actorMapMutex_);
        mergeLookups(lookups);
        historicalBundle_ = std::move(scratch);
        displayHistorical_ = true;
        viewState_ = SessionViewState::Historical;
        updateSnapshot();
    }

    parsingInProgress_.store(false);

    if (onDataUpdate_) {
        onDataUpdate_();
    }

    return true;
}

bool LiveLogSession::parseSegments(const std::vector<PullSegment>& segments) {
    if (!isAttached()) return false;
    if (segments.empty()) return false;

    parsingInProgress_.store(true);

    // The Overall view keeps log-relative timestamps, with no per-pull rebase
    CombatDataBundle scratch;
    SessionLookups lookups = copyPetOwners();
    {
        std::lock_guard<std::mutex> histLock(historicalMutex_);
        for (const auto& segment : segments) {
            parseByteRangeInto(scratch, lookups, segment.startByteOffset,
                               segment.endByteOffset, logStartTimestamp_);
        }
    }

    {
        std::lock_guard<std::timed_mutex> lock(actorMapMutex_);
        mergeLookups(lookups);
        historicalBundle_ = std::move(scratch);
        displayHistorical_ = true;
        viewState_ = SessionViewState::Overall;
        updateSnapshot();
    }

    parsingInProgress_.store(false);

    if (onDataUpdate_) {
        onDataUpdate_();
    }

    return true;
}

LiveLogSession::SessionLookups LiveLogSession::copyPetOwners() const {
    // Pets summoned before the replayed window still need their owner.
    std::lock_guard<std::timed_mutex> lock(actorMapMutex_);
    SessionLookups lookups;
    lookups.petToOwnerFromSummons = petToOwnerFromSummons_;
    return lookups;
}

void LiveLogSession::mergeLookups(const SessionLookups& lookups) {
    // Caller holds actorMapMutex_. What the live parse already knows wins.
    for (const auto& [guid, name] : lookups.guidToName) guidToName_.try_emplace(guid, name);
    for (const auto& [id, name] : lookups.spellIdToName) spellIdToName_.try_emplace(id, name);
    for (const auto& [guid, spec] : lookups.guidToSpecId) guidToSpecId_.try_emplace(guid, spec);
    for (const auto& [pet, owner] : lookups.petToOwnerFromSummons) {
        petToOwnerFromSummons_.try_emplace(pet, owner);
    }
}

void LiveLogSession::returnToLiveMode() {
    parsingInProgress_.store(true);

    {
        std::lock_guard<std::timed_mutex> lock(actorMapMutex_);

        // The live poll never stopped, so the live bundle is current and
        // lastParsedOffset_ is left alone; nothing written meanwhile is lost.
        displayHistorical_ = false;
        viewState_ = SessionViewState::Live;

        // Assigning a fresh bundle releases the old one's capacity as well
        historicalBundle_ = CombatDataBundle{};

        updateSnapshot();
    }

    parsingInProgress_.store(false);

    if (onDataUpdate_) {
        onDataUpdate_();
    }
}

void LiveLogSession::resetAll() {
    liveBundle_.clear();
    historicalBundle_.clear();
    displayHistorical_ = false;
    viewState_ = SessionViewState::Live;
    guidToName_.clear();
    spellIdToName_.clear();
    guidToSpecId_.clear();
    encounters_.clear();
    firstCasts_.clear();
    emoteEvents_.clear();
    pullHistory_.clear();
    petToOwnerFromSummons_.clear();
    currentPull_ = PullSegment{};
    nextPullNumber_ = 1;
    combatState_ = CombatState::Idle;
    lastCombatEventTime_ms_ = 0;
}

void LiveLogSession::scanForSegments() {
    // Finds segment boundaries and byte offsets without parsing events.
    // Trash segments exist only inside a key:
    //   Trash #1: CHALLENGE_MODE_START to the first ENCOUNTER_START
    //   Trash #N: ENCOUNTER_END of boss N-1 to ENCOUNTER_START of boss N
    // Outside a key only boss encounters are listed.

    size_t currentSize = getCurrentFileSize();
    if (currentSize == 0) return;

    std::ifstream file(logPath_, std::ios::binary);
    if (!file) return;

    // This takes seconds on a large raid log. The scan flag lets the
    // segment dropdown say that it is scanning while results come in.
    parsingInProgress_.store(true);
    segmentScanInProgress_.store(true);

    // Read in fixed chunks: raid logs reach gigabytes, and chunking keeps
    // memory flat and lets segments appear before the read finishes.
    constexpr size_t kScanChunk = 8 * 1024 * 1024;
    std::string buffer;
    size_t chunkBase = 0;   // Absolute file offset of buffer[0]
    size_t lineStart = 0;   // Local index into buffer

    // Pulls the next chunk in, dropping everything before lineStart
    // (an unfinished line at the buffer end survives as the prefix).
    // Returns false once the file is exhausted.
    auto refill = [&]() -> bool {
        // lineStart can sit one past the end after the EOF partial line
        // (lineEnd == buffer.size(), then +1); erase throws on pos > size
        size_t consumed = (std::min)(lineStart, buffer.size());
        buffer.erase(0, consumed);
        chunkBase += consumed;
        lineStart = 0;

        size_t alreadyBuffered = buffer.size();
        size_t fileConsumed = chunkBase + alreadyBuffered;
        if (fileConsumed >= currentSize) {
            return false;
        }
        size_t toRead = (std::min)(kScanChunk, currentSize - fileConsumed);
        buffer.resize(alreadyBuffered + toRead);
        file.read(buffer.data() + alreadyBuffered, static_cast<std::streamsize>(toRead));
        size_t got = static_cast<size_t>(file.gcount());
        buffer.resize(alreadyBuffered + got);
        return got > 0;
    };

    // Where the next trash segment would start (absolute offset, M+ only)
    size_t lastBoundaryOffset = 0;
    int32_t lastBoundaryTime_ms = 0;

    // New segments are published after each chunk
    size_t publishedPulls = 0;
    auto publishProgress = [&]() {
        if (pullHistory_.size() != publishedPulls) {
            publishedPulls = pullHistory_.size();
            updateSnapshot();
            if (onDataUpdate_) {
                onDataUpdate_();
            }
        }
    };

    refill();

    while (true) {
        if (lineStart >= buffer.size()) {
            publishProgress();
            if (!refill() || buffer.empty()) {
                break;
            }
        }

        size_t lineEnd = buffer.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
            // Line may continue into the next chunk
            publishProgress();
            if (refill()) {
                continue;
            }
            if (lineStart >= buffer.size()) {
                break;
            }
            lineEnd = buffer.size();  // Partial final line at EOF
        }

        std::string_view line(buffer.data() + lineStart, lineEnd - lineStart);

        // lineStart and lineEnd are relative to the buffer window
        const size_t absLineStart = chunkBase + lineStart;
        const size_t absLineEnd = chunkBase + lineEnd;

        const std::string_view eventType = boundaryEventName(line);
        if (!eventType.empty()) {
            handleScannedBoundary(eventType, line, absLineStart, absLineEnd,
                                  lastBoundaryOffset, lastBoundaryTime_ms);
        }

        lineStart = lineEnd + 1;
    }

    // poll() continues from here
    lastParsedOffset_ = chunkBase + buffer.size();

    // A key still open at the end of the log is closed here, timed to the
    // last event seen (or given a placeholder if it had no combat).
    closeCurrentRun(lastParsedOffset_, lastCombatEventTime_ms_);

    segmentScanInProgress_.store(false);
    parsingInProgress_.store(false);

    updateSnapshot();

    if (onDataUpdate_) {
        onDataUpdate_();
    }
}

void LiveLogSession::handleScannedBoundary(std::string_view eventType, std::string_view line,
                                           size_t absLineStart, size_t absLineEnd,
                                           size_t& lastBoundaryOffset,
                                           int32_t& lastBoundaryTime_ms) {
    tokenized_segment tokenizer;
    std::string lineStr(line);
    auto result = tokenizer.tokenize(lineStr.c_str(), lineStr.size());
    if (result.empty() || result[0].size() < 3) {
        return;
    }
    const auto& tokens = result[0];

    int64_t absolute_ms = parseTimestampMs(tokens[0], tokens[1]);
    if (!logStartTimestampSet_ && absolute_ms > 0) {
        logStartTimestamp_ = absolute_ms;
        logStartTimestampSet_ = true;
    }
    int32_t timestamp_ms = static_cast<int32_t>(absolute_ms - logStartTimestamp_);

    // Trash runs from the previous boundary of the key to this line
    const bool trashBeforeThisLine =
        inMythicPlus_ && lastBoundaryOffset > 0 && lastBoundaryOffset < absLineStart;

    if (eventType == "CHALLENGE_MODE_START") {
        // Close the previous run and reset only the per-run state. Earlier
        // runs stay in the list under their own dungeonRunId.
        closeCurrentRun(absLineStart, timestamp_ms);
        encounters_.clear();
        currentPull_ = PullSegment{};
        currentTrashNumber_ = 0;
        currentBossNumber_ = 0;
        combatState_ = CombatState::Idle;

        if (tokens.size() >= parser::EventParser<parser::ChallengeModeStartTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::ChallengeModeStartTag>::parse_and_return(tokens);
            ++currentDungeonRunId_;
            dungeonStartTime_ms_ = timestamp_ms;
            currentDungeonName_ = std::string(data.zoneName);
            inMythicPlus_ = true;
            currentKeystoneLevel_ = data.keystoneLevel;

            challengeModeStartOffset_ = absLineStart;
            challengeModeStartTime_ms_ = timestamp_ms;

            lastBoundaryOffset = absLineEnd + 1;  // Trash #1 starts after this line
            lastBoundaryTime_ms = timestamp_ms;
        }
    } else if (eventType == "CHALLENGE_MODE_END") {
        // Trash after the last boss
        if (trashBeforeThisLine) {
            pushTrashSegment(lastBoundaryOffset, absLineStart, lastBoundaryTime_ms, timestamp_ms);
        }

        if (inMythicPlus_ && challengeModeStartOffset_ > 0) {
            // Include the CHALLENGE_MODE_END line
            pushDungeonOverall(absLineEnd + 1, timestamp_ms);
        }

        // The run has an Overall segment, so no placeholder is added
        closeCurrentRun(absLineEnd + 1, timestamp_ms);

        inMythicPlus_ = false;
        lastBoundaryOffset = 0;
        challengeModeStartOffset_ = 0;
    } else if (eventType == "ENCOUNTER_START") {
        if (tokens.size() >= parser::EventParser<parser::EncounterStartTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::EncounterStartTag>::parse_and_return(tokens);

            // Trash leading up to this boss
            if (trashBeforeThisLine) {
                pushTrashSegment(lastBoundaryOffset, absLineStart, lastBoundaryTime_ms, timestamp_ms);
            }

            // If not in M+, this starts a new run (raid)
            if (!inMythicPlus_ && encounters_.empty()) {
                ++currentDungeonRunId_;
                dungeonStartTime_ms_ = timestamp_ms;
                currentDungeonName_ = std::string(data.encounterName);
            }

            beginBossPull(data.encounterName, data.encounterId, data.difficultyId,
                          timestamp_ms, absLineStart, /*clearData=*/true);
        }
    } else if (eventType == "ENCOUNTER_END") {
        if (tokens.size() >= parser::EventParser<parser::EncounterEndTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::EncounterEndTag>::parse_and_return(tokens);
            finishBossPull(data.success == 1, timestamp_ms, absLineEnd);

            if (inMythicPlus_) {
                lastBoundaryOffset = absLineEnd + 1;  // Next trash starts after this line
                lastBoundaryTime_ms = timestamp_ms;
            }
        }
    }

    lastCombatEventTime_ms_ = timestamp_ms;
}

int64_t LiveLogSession::parseTimestampMs(std::string_view date, std::string_view time) {
    // Uses the parser's conversion so pull times and the parser's
    // encounter timestamps share one epoch base. The parser caches the
    // date part, so this is cheap per line.
    thread_local std::vector<std::string_view> tokens(2);
    tokens[0] = date;
    tokens[1] = time;
    return static_cast<int64_t>(parser::EventParser<void>::parse_timestamp_ms(tokens));
}

LiveLogSession::Snapshot LiveLogSession::getSnapshot() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_;
}

std::unique_lock<std::timed_mutex> LiveLogSession::tryLockActorMap() const {
    // A short wait rather than try_lock: a poll holds the lock a few
    // milliseconds per slice, so waiting usually gets it without the caller
    // drawing a spinner frame. Longer holds still time out.
    return std::unique_lock<std::timed_mutex>(actorMapMutex_, std::chrono::milliseconds(10));
}

std::unique_lock<std::timed_mutex> LiveLogSession::lockActorMap() const {
    return std::unique_lock<std::timed_mutex>(actorMapMutex_);
}

void LiveLogSession::updateSnapshot() {
    std::lock_guard<std::mutex> lock(snapshotMutex_);

    snapshot_.guidToName = guidToName_;
    snapshot_.spellIdToName = spellIdToName_;

    snapshot_.currentPull = currentPull_;
    snapshot_.inCombat = (combatState_ != CombatState::Idle);
    snapshot_.inEncounter = (combatState_ == CombatState::InEncounter);
    snapshot_.inMythicPlus = inMythicPlus_;

    // Segment metadata only, so the dropdown needs no ActorMap copy
    snapshot_.pullHistory = pullHistory_;

    // Phase-rule inputs and spec ids (all small; see the member docs)
    snapshot_.firstCasts = firstCasts_;
    snapshot_.emotes = emoteEvents_;
    snapshot_.guidToSpecId = guidToSpecId_;
}

uint32_t LiveLogSession::resolveOwnerGuidId(
        const std::unordered_map<std::string, std::string>& petOwners,
        std::string_view source_guid,
        std::string_view advanced_owner_guid) {
    // The advanced log block's owner is used only when it is a player,
    // which rules out the "0000000000000000" placeholder and NPC guardians.
    if (!advanced_owner_guid.empty() &&
        advanced_owner_guid != "0000000000000000" &&
        advanced_owner_guid.starts_with("Player-")) {
        return guidInterner().intern(advanced_owner_guid);
    }

    // Otherwise use SPELL_SUMMON lineage. Short-lived pets such as Wild
    // Imps often have no owner in the advanced block.
    auto it = petOwners.find(std::string(source_guid));
    if (it != petOwners.end()) {
        return guidInterner().intern(it->second);
    }

    return 0;
}

std::unordered_map<StringInterner::Id, StringInterner::Id>
LiveLogSession::getSummonPetToOwnerMap() const {
    std::unordered_map<StringInterner::Id, StringInterner::Id> out;
    out.reserve(petToOwnerFromSummons_.size());
    for (const auto& [pet_guid, owner_guid] : petToOwnerFromSummons_) {
        out[guidInterner().intern(pet_guid)] = guidInterner().intern(owner_guid);
    }
    return out;
}

bool LiveLogSession::parseAndStoreEvent(CombatDataBundle& target,
                                        SessionLookups* historical,
                                        const std::vector<std::string_view>& tokens,
                                        std::string_view eventType,
                                        int32_t timestamp_ms) {
    const bool isCombatEvent = isCombatEventType(eventType);

    // A historical parse runs on its own thread while poll() keeps writing
    // the session maps, so it learns names into its own set instead.
    const bool live = (historical == nullptr);
    auto& names = live ? guidToName_ : historical->guidToName;
    auto& spellNames = live ? spellIdToName_ : historical->spellIdToName;
    auto& specIds = live ? guidToSpecId_ : historical->guidToSpecId;
    auto& petOwners = live ? petToOwnerFromSummons_ : historical->petToOwnerFromSummons;

    // The first name seen for a guid or spell id is the one kept
    const auto learnName = [&names](std::string_view guid, std::string_view name) {
        if (!guid.empty() && !name.empty()) {
            names.try_emplace(std::string(guid), std::string(name));
        }
    };
    const auto learnSpellName = [&spellNames](auto spellId, std::string_view name) {
        if (spellId > 0 && !name.empty()) {
            spellNames.try_emplace(spellId, std::string(name));
        }
    };

    if (eventType == "SPELL_DAMAGE" || eventType == "RANGE_DAMAGE" ||
        eventType == "SPELL_PERIODIC_DAMAGE" || eventType == "SWING_DAMAGE_LANDED" ||
        eventType == "DAMAGE_SPLIT") {

        // The spell, range and periodic damage structs share these fields
        const auto storeSpellDamage = [&](const auto& data, uint16_t extraFlags) {
            learnName(data.source_guid, data.source_name);
            learnName(data.dest_guid, data.dest_name);
            learnSpellName(data.spell.spell_id, data.spell.spell_name);
            CombatRecord record{};
            record.timestamp_ms = timestamp_ms;
            record.spell_id = data.spell.spell_id;
            record.amount = data.amount;
            record.effective_amount = data.mitigated_amount;
            record.absorbed = data.absorbed;
            record.blocked = data.blocked;
            record.resisted = data.resisted;
            record.target_guid_id = guidInterner().intern(data.dest_guid);
            record.owner_guid_id = resolveOwnerGuidId(petOwners, data.source_guid,
                                                      data.advanced_info.owner_guid);
            record.spell_school = static_cast<uint8_t>(data.damage_school);
            record.event_type = CombatEventType::DamageDealt;
            record.flags |= extraFlags;
            if (data.critical) record.flags |= static_cast<uint16_t>(CombatEventFlags::Critical);
            // Friendly fire counts as damage taken, not damage done
            if (data.source_flags.isFriendly() && data.dest_flags.isFriendly()) {
                record.flags |= static_cast<uint16_t>(CombatEventFlags::FriendlyFire);
            }
            target.actorMap[guidInterner().intern(data.source_guid)].damage_dealt_table.push_back(record);
            appendResourceSnapshot(target, data.source_guid, data.dest_guid,
                                   data.advanced_info, timestamp_ms);
        };

        if (eventType == "SPELL_DAMAGE" || eventType == "RANGE_DAMAGE") {
            if (tokens.size() >= parser::EventParser<parser::SpellDamageTag>::expected_token_count()) {
                storeSpellDamage(parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens), 0);
            }
        } else if (eventType == "SPELL_PERIODIC_DAMAGE") {
            if (tokens.size() >= parser::EventParser<parser::SpellPeriodicDamageTag>::expected_token_count()) {
                storeSpellDamage(parser::EventParser<parser::SpellPeriodicDamageTag>::parse_and_return(tokens),
                                 static_cast<uint16_t>(CombatEventFlags::Periodic));
            }
        } else if (eventType == "SWING_DAMAGE_LANDED") {
            if (tokens.size() >= parser::EventParser<parser::SwingDamageLandedTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SwingDamageLandedTag>::parse_and_return(tokens);
                learnName(data.source_guid, data.source_name);
                learnName(data.dest_guid, data.dest_name);
                CombatRecord record{};
                record.timestamp_ms = timestamp_ms;
                record.spell_id = 0;
                record.amount = data.amount;
                record.effective_amount = data.mitigated_amount;
                record.absorbed = data.absorbed;
                record.blocked = data.blocked;
                record.resisted = data.resisted;
                record.target_guid_id = guidInterner().intern(data.dest_guid);
                record.owner_guid_id = resolveOwnerGuidId(petOwners, data.source_guid,
                                                          data.advanced_info.owner_guid);
                record.spell_school = static_cast<uint8_t>(data.spell.spell_school);
                record.event_type = CombatEventType::DamageDealt;
                record.flags |= static_cast<uint16_t>(CombatEventFlags::IsSwing);
                if (data.critical) record.flags |= static_cast<uint16_t>(CombatEventFlags::Critical);
                // Friendly fire counts as damage taken, not damage done
                if (data.source_flags.isFriendly() && data.dest_flags.isFriendly()) {
                    record.flags |= static_cast<uint16_t>(CombatEventFlags::FriendlyFire);
                }
                target.actorMap[guidInterner().intern(data.source_guid)].damage_dealt_table.push_back(record);
                appendResourceSnapshot(target, data.source_guid, data.dest_guid,
                                       data.advanced_info, timestamp_ms);
            }
        }
    }
    else if (eventType == "SPELL_HEAL" || eventType == "SPELL_PERIODIC_HEAL") {
        const auto storeHeal = [&](const auto& data, uint16_t extraFlags) {
            learnName(data.source_guid, data.source_name);
            learnName(data.dest_guid, data.dest_name);
            learnSpellName(data.spell.spell_id, data.spell.spell_name);
            CombatRecord record{};
            record.timestamp_ms = timestamp_ms;
            record.spell_id = data.spell.spell_id;
            record.amount = data.amount;
            record.effective_amount = data.amount - data.overhealing;
            record.absorbed = data.absorbed;
            record.target_guid_id = guidInterner().intern(data.dest_guid);
            record.owner_guid_id = resolveOwnerGuidId(petOwners, data.source_guid,
                                                      data.advanced_info.owner_guid);
            record.spell_school = static_cast<uint8_t>(data.spell.spell_school);
            record.event_type = CombatEventType::HealingDone;
            record.flags |= extraFlags;
            if (data.critical) record.flags |= static_cast<uint16_t>(CombatEventFlags::Critical);
            if (data.overhealing > 0) record.flags |= static_cast<uint16_t>(CombatEventFlags::Overheal);
            target.actorMap[guidInterner().intern(data.source_guid)].healing_done_table.push_back(record);
            appendResourceSnapshot(target, data.source_guid, data.dest_guid,
                                   data.advanced_info, timestamp_ms);
        };

        if (eventType == "SPELL_HEAL") {
            if (tokens.size() >= parser::EventParser<parser::SpellHealTag>::expected_token_count()) {
                storeHeal(parser::EventParser<parser::SpellHealTag>::parse_and_return(tokens), 0);
            }
        } else if (tokens.size() >= parser::EventParser<parser::SpellPeriodicHealTag>::expected_token_count()) {
            storeHeal(parser::EventParser<parser::SpellPeriodicHealTag>::parse_and_return(tokens),
                      static_cast<uint16_t>(CombatEventFlags::Periodic));
        }
    }
    // *_SUPPORT lines repeat part of a parent event so the supporter (an
    // Augmentation Evoker, for example) can be credited for the share its
    // buff added. The record goes to supporter_guid with the Support flag.
    else if (eventType == "SPELL_DAMAGE_SUPPORT" ||
             eventType == "SPELL_PERIODIC_DAMAGE_SUPPORT" ||
             eventType == "SWING_DAMAGE_LANDED_SUPPORT") {

        auto storeSupport = [&](const parser::SupportEventData& data,
                                bool isPeriodic, bool isSwing) {
            learnName(data.source_guid, data.source_name);
            learnName(data.dest_guid, data.dest_name);
            learnSpellName(data.spell.spell_id, data.spell.spell_name);
            if (data.supporter_guid.empty()) return;

            // Stored the same way as live parent records (raw amount and
            // raw mitigated amount), so the deduction below subtracts like
            // from like. Applying the offline crit and overkill adjustments
            // here would over-credit the supporter on every crit.
            const int64_t supportAmount = data.amount;
            const int64_t supportEffective = data.mitigated_amount;

            // Take the support share off the amplified actor's parent
            // record. The parent has the same source, target and school,
            // the same swing/spell kind, is not itself a support record,
            // and is at most 2ms older. The newest match wins. With no
            // match the supporter is still credited and the parent keeps
            // its full amount.
            const uint32_t target_id = guidInterner().intern(data.dest_guid);
            const uint8_t school = static_cast<uint8_t>(
                isSwing ? data.spell.spell_school : data.damage_school);
            if (!data.source_guid.empty()) {
                auto srcIt = target.actorMap.find(guidInterner().find(data.source_guid));
                if (srcIt != target.actorMap.end()) {
                    auto& parent_table = srcIt->second.damage_dealt_table;
                    for (auto rit = parent_table.rbegin();
                         rit != parent_table.rend();
                         ++rit) {
                        if (timestamp_ms - rit->timestamp_ms > 2) break;
                        if (rit->flags & CombatEventFlags::Support) continue;
                        if (rit->target_guid_id != target_id) continue;
                        if (rit->spell_school != school) continue;
                        const bool parent_is_swing =
                            (rit->flags & CombatEventFlags::IsSwing) != 0;
                        if (parent_is_swing != isSwing) continue;
                        rit->amount -= supportAmount;
                        rit->effective_amount -= supportEffective;
                        break;
                    }
                }
            }

            CombatRecord record{};
            record.timestamp_ms = timestamp_ms;
            // Swing support lines carry the supporter's aura spell id
            // (Ebon Might, for example), not melee. Keeping it puts spell
            // and swing support under the same meter row.
            record.spell_id = data.spell.spell_id;
            record.amount = supportAmount;
            record.effective_amount = supportEffective;
            record.absorbed = data.absorbed;
            record.blocked = data.blocked;
            record.resisted = data.resisted;
            record.target_guid_id = target_id;
            record.owner_guid_id = 0;  // Credited to the supporter directly
            record.spell_school = school;
            record.event_type = CombatEventType::DamageDealt;
            record.flags |= static_cast<uint16_t>(CombatEventFlags::Support);
            if (isPeriodic) record.flags |= static_cast<uint16_t>(CombatEventFlags::Periodic);
            if (isSwing)    record.flags |= static_cast<uint16_t>(CombatEventFlags::IsSwing);
            if (data.critical) record.flags |= static_cast<uint16_t>(CombatEventFlags::Critical);

            target.actorMap[guidInterner().intern(data.supporter_guid)].damage_dealt_table.push_back(record);
        };

        if (eventType == "SPELL_DAMAGE_SUPPORT") {
            if (tokens.size() >= parser::EventParser<parser::SpellDamageSupportTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellDamageSupportTag>::parse_and_return(tokens);
                storeSupport(data, false, false);
            }
        } else if (eventType == "SPELL_PERIODIC_DAMAGE_SUPPORT") {
            if (tokens.size() >= parser::EventParser<parser::SpellPeriodicDamageSupportTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellPeriodicDamageSupportTag>::parse_and_return(tokens);
                // Not flagged periodic, matching the offline OutputWriter
                // and Warcraft Logs, which treat support ticks as direct
                storeSupport(data, false, false);
            }
        } else {  // SWING_DAMAGE_LANDED_SUPPORT
            if (tokens.size() >= parser::EventParser<parser::SwingDamageLandedSupportTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SwingDamageLandedSupportTag>::parse_and_return(tokens);
                storeSupport(data, false, true);
            }
        }
    }
    else if (eventType == "SPELL_HEAL_SUPPORT" ||
             eventType == "SPELL_PERIODIC_HEAL_SUPPORT") {

        auto storeHealSupport = [&](const parser::HealSupportEventData& data) {
            learnName(data.source_guid, data.source_name);
            learnName(data.dest_guid, data.dest_name);
            learnSpellName(data.spell.spell_id, data.spell.spell_name);
            if (data.supporter_guid.empty()) return;

            const int64_t supportAmount = data.amount;
            const int64_t supportEffective = data.amount - data.overhealing;
            const uint32_t target_id = guidInterner().intern(data.dest_guid);
            const uint8_t school = static_cast<uint8_t>(data.spell.spell_school);

            // Same matching as damage support, against the parent heal
            if (!data.source_guid.empty()) {
                auto srcIt = target.actorMap.find(guidInterner().find(data.source_guid));
                if (srcIt != target.actorMap.end()) {
                    auto& parent_table = srcIt->second.healing_done_table;
                    for (auto rit = parent_table.rbegin();
                         rit != parent_table.rend();
                         ++rit) {
                        if (timestamp_ms - rit->timestamp_ms > 2) break;
                        if (rit->flags & CombatEventFlags::Support) continue;
                        if (rit->target_guid_id != target_id) continue;
                        if (rit->spell_school != school) continue;
                        rit->amount -= supportAmount;
                        rit->effective_amount -= supportEffective;
                        break;
                    }
                }
            }

            CombatRecord record{};
            record.timestamp_ms = timestamp_ms;
            record.spell_id = data.spell.spell_id;
            record.amount = supportAmount;
            record.effective_amount = supportEffective;
            record.absorbed = data.absorbed;
            record.target_guid_id = target_id;
            record.owner_guid_id = 0;
            record.spell_school = school;
            record.event_type = CombatEventType::HealingDone;
            record.flags |= static_cast<uint16_t>(CombatEventFlags::Support);
            if (data.critical) record.flags |= static_cast<uint16_t>(CombatEventFlags::Critical);
            if (data.overhealing > 0) record.flags |= static_cast<uint16_t>(CombatEventFlags::Overheal);

            target.actorMap[guidInterner().intern(data.supporter_guid)].healing_done_table.push_back(record);
        };

        if (eventType == "SPELL_HEAL_SUPPORT") {
            if (tokens.size() >= parser::EventParser<parser::SpellHealSupportTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellHealSupportTag>::parse_and_return(tokens);
                storeHealSupport(data);
            }
        } else {  // SPELL_PERIODIC_HEAL_SUPPORT
            if (tokens.size() >= parser::EventParser<parser::SpellPeriodicHealSupportTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellPeriodicHealSupportTag>::parse_and_return(tokens);
                storeHealSupport(data);
            }
        }
    }
    else if (eventType == "SPELL_ABSORBED") {
        if (tokens.size() >= parser::EventParser<parser::SpellAbsorbedTag>::minimum_token_count()) {
            auto data = parser::EventParser<parser::SpellAbsorbedTag>::parse_and_return(tokens);
            learnName(data.source_guid, data.source_name);
            learnName(data.dest_guid, data.dest_name);
            learnName(data.absorber_guid, data.absorber_name);
            learnSpellName(data.absorb_spell_id, data.absorb_spell_name);
            AbsorbEvent ev;
            ev.source_guid = std::string(data.source_guid);
            ev.source_name = std::string(data.source_name);
            ev.target_guid = std::string(data.dest_guid);
            ev.target_name = std::string(data.dest_name);
            ev.absorber_guid = std::string(data.absorber_guid);
            ev.absorber_name = std::string(data.absorber_name);
            ev.source_spell_id = data.spell.spell_id;
            ev.source_spell_name = std::string(data.spell.spell_name);
            ev.absorb_spell_id = data.absorb_spell_id;
            ev.absorb_spell_name = std::string(data.absorb_spell_name);
            ev.timestamp_ms = timestamp_ms;
            ev.absorbed_amount = data.amount;
            ev.total_damage = 0;
            target.absorbEvents.push_back(std::move(ev));
        }
    }
    else if (eventType == "SPELL_ABSORBED_SUPPORT") {
        if (tokens.size() >= parser::EventParser<parser::SpellAbsorbedSupportTag>::minimum_token_count()) {
            auto data = parser::EventParser<parser::SpellAbsorbedSupportTag>::parse_and_return(tokens);
            learnName(data.source_guid, data.source_name);
            learnName(data.dest_guid, data.dest_name);
            learnName(data.absorber_guid, data.absorber_name);
            learnSpellName(data.absorb_spell_id, data.absorb_spell_name);
            AbsorbEvent ev;
            ev.source_guid = std::string(data.source_guid);
            ev.source_name = std::string(data.source_name);
            ev.target_guid = std::string(data.dest_guid);
            ev.target_name = std::string(data.dest_name);
            ev.absorber_guid = std::string(data.absorber_guid);
            ev.absorber_name = std::string(data.absorber_name);
            ev.source_spell_id = data.spell.spell_id;
            ev.source_spell_name = std::string(data.spell.spell_name);
            ev.absorb_spell_id = data.absorb_spell_id;
            ev.absorb_spell_name = std::string(data.absorb_spell_name);
            ev.timestamp_ms = timestamp_ms;
            ev.absorbed_amount = data.amount;
            ev.total_damage = 0;
            ev.supporter_guid = std::string(data.supporter_guid);
            target.absorbEvents.push_back(std::move(ev));
        }
    }
    else if (eventType == "SPELL_MISSED" || eventType == "SPELL_PERIODIC_MISSED"
          || eventType == "RANGE_MISSED") {
        if (tokens.size() >= parser::EventParser<parser::SpellMissedTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellMissedTag>::parse_and_return(tokens);
            learnName(data.source_guid, data.source_name);
            learnName(data.dest_guid, data.dest_name);
            learnSpellName(data.spell.spell_id, data.spell.spell_name);
            MissedEvent ev;
            ev.source_guid = std::string(data.source_guid);
            ev.source_name = std::string(data.source_name);
            ev.target_guid = std::string(data.dest_guid);
            ev.target_name = std::string(data.dest_name);
            ev.spell_id = data.spell.spell_id;
            ev.spell_name = std::string(data.spell.spell_name);
            ev.timestamp_ms = timestamp_ms;
            ev.miss_type = data.miss_type;
            ev.amount_missed = data.amount_missed;
            ev.blocked_amount = data.blocked_amount;
            ev.is_offhand = data.is_off_hand;
            ev.is_critical = data.critical;
            ev.is_periodic = (eventType == "SPELL_PERIODIC_MISSED");
            target.missedEvents.push_back(std::move(ev));
        }
    }
    else if (eventType == "SWING_MISSED") {
        if (tokens.size() >= parser::EventParser<parser::SwingMissedTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SwingMissedTag>::parse_and_return(tokens);
            learnName(data.source_guid, data.source_name);
            learnName(data.dest_guid, data.dest_name);
            MissedEvent ev;
            ev.source_guid = std::string(data.source_guid);
            ev.source_name = std::string(data.source_name);
            ev.target_guid = std::string(data.dest_guid);
            ev.target_name = std::string(data.dest_name);
            ev.spell_id = 1;
            ev.spell_name = "Melee";
            ev.timestamp_ms = timestamp_ms;
            ev.miss_type = data.miss_type;
            ev.amount_missed = data.amount_missed;
            ev.blocked_amount = 0;
            ev.is_offhand = data.is_off_hand;
            ev.is_critical = data.critical;
            ev.is_periodic = false;
            target.missedEvents.push_back(std::move(ev));
        }
    }
    else if (eventType == "SPELL_DISPEL" || eventType == "SPELL_INTERRUPT" ||
             eventType == "SPELL_STOLEN") {
        const auto storeDispel = [&](const auto& data,
                                     DispelInterruptEvent::EventType kind,
                                     AuraType auraType) {
            learnName(data.source_guid, data.source_name);
            learnName(data.dest_guid, data.dest_name);
            learnSpellName(data.spell.spell_id, data.spell.spell_name);
            DispelInterruptEvent ev;
            ev.source_guid = std::string(data.source_guid);
            ev.source_name = std::string(data.source_name);
            ev.target_guid = std::string(data.dest_guid);
            ev.target_name = std::string(data.dest_name);
            ev.timestamp_ms = timestamp_ms;
            ev.spell_id = data.spell.spell_id;
            ev.spell_name = std::string(data.spell.spell_name);
            ev.extra_spell_id = data.extra_spell_id;
            ev.extra_spell_name = std::string(data.extra_spell_name);
            ev.extra_spell_school = data.extra_spell_school;
            ev.event_type = kind;
            ev.aura_type = auraType;
            target.dispelEvents.push_back(std::move(ev));
        };

        if (eventType == "SPELL_DISPEL") {
            if (tokens.size() >= parser::EventParser<parser::SpellDispelTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellDispelTag>::parse_and_return(tokens);
                storeDispel(data, DispelInterruptEvent::EventType::Dispel, data.aura_type);
            }
        } else if (eventType == "SPELL_INTERRUPT") {
            if (tokens.size() >= parser::EventParser<parser::SpellInterruptTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellInterruptTag>::parse_and_return(tokens);
                storeDispel(data, DispelInterruptEvent::EventType::Interrupt, AuraType::Unknown);
            }
        } else if (tokens.size() >= parser::EventParser<parser::SpellStolenTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellStolenTag>::parse_and_return(tokens);
            storeDispel(data, DispelInterruptEvent::EventType::Stolen, data.aura_type);
        }
    }
    else if (eventType == "UNIT_DIED" || eventType == "UNIT_DESTROYED"
          || eventType == "UNIT_DISSIPATES") {
        if (tokens.size() >= parser::EventParser<parser::UnitDiedTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::UnitDiedTag>::parse_and_return(tokens);
            learnName(data.dest_guid, data.dest_name);
            DeathEvent ev;
            ev.actor_guid = std::string(data.dest_guid);
            ev.actor_name = std::string(data.dest_name);
            ev.actor_type = awow::getActorTypeFromGuid(data.dest_guid);
            ev.timestamp_ms = timestamp_ms;
            ev.killing_spell_id = 0;
            ev.killing_spell_name = "";
            target.deathEvents.push_back(std::move(ev));
        }
    }
    else if (eventType == "SPELL_INSTAKILL") {
        if (tokens.size() >= parser::EventParser<parser::SpellInstakillTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellInstakillTag>::parse_and_return(tokens);
            learnName(data.dest_guid, data.dest_name);
            learnSpellName(data.spell_id, data.spell_name);
            DeathEvent ev;
            ev.actor_guid = std::string(data.dest_guid);
            ev.actor_name = std::string(data.dest_name);
            ev.actor_type = awow::getActorTypeFromGuid(data.dest_guid);
            ev.timestamp_ms = timestamp_ms;
            ev.killing_spell_id = data.spell_id;
            ev.killing_spell_name = std::string(data.spell_name);
            target.deathEvents.push_back(std::move(ev));
        }
    }
    // Auras are kept only for the spell ids tracked as defensives, so the
    // death recap can tell whether the player had one up. With nothing
    // tracked (the default) these lines are skipped before parsing.
    else if (eventType == "SPELL_AURA_APPLIED" || eventType == "SPELL_AURA_REMOVED" ||
             eventType == "SPELL_AURA_REFRESH") {
        if (!trackedDefensiveIds_.empty() &&
            tokens.size() >= parser::EventParser<parser::SpellAuraAppliedTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellAuraAppliedTag>::parse_and_return(tokens);
            // Defensives are buffs on players, so debuffs and other
            // targets are skipped
            if (data.aura_type == AuraType::BUFF &&
                data.spell.spell_id > 0 &&
                trackedDefensiveIds_.count(data.spell.spell_id) &&
                awow::getActorTypeFromGuid(data.dest_guid) == ActorType::Player) {
                learnName(data.dest_guid, data.dest_name);
                spellNames.try_emplace(data.spell.spell_id, std::string(data.spell.spell_name));

                AuraEvent aura;
                aura.target_guid = std::string(data.dest_guid);
                aura.source_guid = std::string(data.source_guid);
                aura.spell_id = data.spell.spell_id;
                aura.spell_name = std::string(data.spell.spell_name);
                aura.aura_type = data.aura_type;
                aura.timestamp_ms = timestamp_ms;
                aura.event_type = (eventType == "SPELL_AURA_REMOVED")
                    ? AuraEventType::Removed
                    : (eventType == "SPELL_AURA_REFRESH" ? AuraEventType::Refresh
                                                         : AuraEventType::Applied);
                aura.stacks = 1;
                target.auraEvents.push_back(std::move(aura));
            }
        }
    }
    // For the CC breaks meter
    else if (eventType == "SPELL_AURA_BROKEN") {
        if (tokens.size() >= parser::EventParser<parser::SpellAuraBrokenTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellAuraBrokenTag>::parse_and_return(tokens);
            learnName(data.source_guid, data.source_name);
            learnName(data.dest_guid, data.dest_name);
            learnSpellName(data.spell.spell_id, data.spell.spell_name);
            AuraEvent aura;
            aura.target_guid = std::string(data.dest_guid);
            aura.source_guid = std::string(data.source_guid);
            aura.spell_id = data.spell.spell_id;
            aura.spell_name = std::string(data.spell.spell_name);
            aura.aura_type = data.aura_type;
            aura.timestamp_ms = timestamp_ms;
            aura.event_type = AuraEventType::Broken;
            aura.stacks = 1;
            target.auraEvents.push_back(std::move(aura));
        }
    }
    else if (eventType == "SPELL_AURA_BROKEN_SPELL") {
        if (tokens.size() >= parser::EventParser<parser::SpellAuraBrokenSpellTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellAuraBrokenSpellTag>::parse_and_return(tokens);
            learnName(data.source_guid, data.source_name);
            learnName(data.dest_guid, data.dest_name);
            learnSpellName(data.spell.spell_id, data.spell.spell_name);
            AuraEvent aura;
            aura.target_guid = std::string(data.dest_guid);
            aura.source_guid = std::string(data.source_guid);
            aura.spell_id = data.spell.spell_id;
            aura.spell_name = std::string(data.spell.spell_name);
            aura.aura_type = data.aura_type;
            aura.timestamp_ms = timestamp_ms;
            aura.event_type = AuraEventType::BrokenSpell;
            aura.stacks = 1;
            aura.breaking_spell_id = data.extra_spell_id;
            aura.breaking_spell_name = std::string(data.extra_spell_name);
            target.auraEvents.push_back(std::move(aura));
        }
    }
    // Pet ownership for resolveOwnerGuidId() when a pet's own events have
    // no owner in the advanced block
    else if (eventType == "SPELL_SUMMON") {
        if (tokens.size() >= parser::EventParser<parser::SpellSummonTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellSummonTag>::parse_and_return(tokens);
            learnName(data.source_guid, data.source_name);
            learnName(data.dest_guid, data.dest_name);
            // Player summons of non-players only. WoW also writes
            // SPELL_SUMMON with a player as the target (buff proxies, Time
            // Skip clones), and keeping those would hand that player's
            // damage to the caster.
            if (!data.dest_guid.empty() &&
                data.source_guid.starts_with("Player-") &&
                !data.dest_guid.starts_with("Player-")) {
                petOwners[std::string(data.dest_guid)] =
                    std::string(data.source_guid);
            }
        }
    }
    // "First cast of X" phase rules need only the earliest cast of each
    // spell. The cast-start tag reads just the base and spell fields, so
    // this also works on logs without advanced logging.
    else if (eventType == "SPELL_CAST_SUCCESS") {
        if (tokens.size() >= parser::EventParser<parser::SpellCastStartTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellCastStartTag>::parse_and_return(tokens);
            learnName(data.source_guid, data.source_name);
            learnSpellName(data.spell.spell_id, data.spell.spell_name);
            // First casts and emotes feed the live phase rules; a replayed
            // segment must not add to them.
            if (live && data.spell.spell_id > 0) {
                auto [it, inserted] = firstCasts_.try_emplace(data.spell.spell_id);
                if (inserted) {
                    it->second.time_ms = timestamp_ms;
                    it->second.spell_name = std::string(data.spell.spell_name);
                    it->second.hostile_source = data.source_flags.isHostile();
                }
            }
        }
    }
    // Boss speech, for "first emote saying X" phase rules and the phase
    // editor's list of split points. EMOTE lines have no flag fields
    // (source guid at [3], text from [7]) and the text is unquoted, so it
    // is rebuilt from the tokens the same way the offline extractor does.
    else if (eventType == "EMOTE") {
        if (live && tokens.size() >= 8 && emoteEvents_.size() < kMaxEmotesPerSegment) {
            EmoteEvent emote;
            emote.timestamp_ms = timestamp_ms;
            emote.source_guid = std::string(tokens[3]);
            emote.source_name = std::string(tokens[4]);
            emote.text = awow::emote::emoteTextFromTokens(tokens);
            if (!emote.text.empty()) {
                names.try_emplace(emote.source_guid, emote.source_name);
                emoteEvents_.push_back(std::move(emote));
            }
        }
    }
    // One line per player at each encounter or key start. Only the spec id
    // is read, for class-coloured bars; gear and talents are skipped.
    else if (eventType == "COMBATANT_INFO") {
        if (tokens.size() >= parser::EventParser<parser::CombatantInfoTag>::minimum_token_count()) {
            std::string_view playerGuid =
                parser::EventParser<parser::CombatantInfoTag>::extract_player_guid(tokens);
            uint16_t specId =
                parser::EventParser<parser::CombatantInfoTag>::extract_spec_id(tokens);
            if (!playerGuid.empty() && specId > 0) {
                specIds[std::string(playerGuid)] = specId;
            }
        }
    }

    return isCombatEvent;
}
