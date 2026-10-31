#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <chrono>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>

#include "PullSegment.h"
#include "Parser/Include/Actor/ActorMap.h"
#include "Parser/Include/Encounter/EncounterSegment.h"
#include "Database/EventTypes.h"
#include "Database/AuraDatabase.h"   // for AuraEvent

class CombatDatabase;
class DispelDatabase;
class ResourceDatabase;

// First completed cast of a spell within the loaded data window
// (current live pull, or the re-parsed historical segment). Phase
// rules of the "first cast of X" kind resolve against these, and the
// phase editor lists the hostile ones as candidate split points.
struct LiveFirstCast {
    int32_t time_ms = 0;
    std::string spell_name;
    bool hostile_source = false;  // Cast by a boss or add
};

// The combat data behind one view: actor stats plus the event streams the
// meter panels read. The session has two, one filled by the live poll and
// one rebuilt when a past segment is opened, so viewing history never
// pauses the live parse. Names, specs and pet lineage are shared on the
// session instead.
struct CombatDataBundle {
    ActorMap actorMap;
    std::vector<AbsorbEvent> absorbEvents;
    std::vector<MissedEvent> missedEvents;
    std::vector<DispelInterruptEvent> dispelEvents;
    std::vector<DeathEvent> deathEvents;
    std::vector<AuraEvent> auraEvents;  // Broken/BrokenSpell (CC breaks) +
                                        // Applied/Removed/Refresh for tracked
                                        // defensive spell ids only

    void clear() {
        actorMap.clear();
        absorbEvents.clear();
        missedEvents.clear();
        dispelEvents.clear();
        deathEvents.clear();
        auraEvents.clear();
    }
};

// Incremental parser for a combat log that WoW is still writing. Each poll()
// reads only the bytes added since the last one and leaves a partly written
// last line for the next poll. Pull and encounter boundaries are recorded with
// byte offsets so a past pull can be re-parsed on demand.
//
//   LiveLogSession session;
//   session.attach("C:/.../_retail_/Logs/WoWCombatLog-123.txt");
//   if (session.poll()) {
//       // new events were parsed
//   }
class LiveLogSession {
public:
    // Copy of the session state for the UI, taken under a lock at the end of
    // each poll(). Past pulls appear only as PullSegment metadata; their
    // combat data is re-parsed from the file when opened.
    struct Snapshot {
        std::unordered_map<std::string, std::string> guidToName;
        std::unordered_map<uint32_t, std::string> spellIdToName;

        PullSegment currentPull;
        bool inCombat = false;
        bool inEncounter = false;
        bool inMythicPlus = false;

        // Labels, offsets and times only, so cheap to copy
        std::vector<PullSegment> pullHistory;

        // Phase-rule inputs for the loaded data window: first cast per
        // spell id and the emote lines seen. Cleared with the rest of
        // the pull data, so they always describe whatever segment the
        // meters are showing. Both stay small (distinct spell ids per
        // pull; emotes capped at kMaxEmotesPerSegment).
        std::unordered_map<uint32_t, LiveFirstCast> firstCasts;
        std::vector<EmoteEvent> emotes;

        // Spec ids from COMBATANT_INFO, kept for the whole session like
        // the name lookups. Used for class-coloured meter bars.
        std::unordered_map<std::string, uint16_t> guidToSpecId;
    };

    using PullStartCallback = std::function<void(const PullSegment&)>;
    using PullEndCallback = std::function<void(const PullSegment&)>;
    using DataUpdateCallback = std::function<void()>;
    using DungeonStartCallback = std::function<void(uint32_t runId, const std::string& name, uint32_t level)>;

    LiveLogSession();
    ~LiveLogSession();

    LiveLogSession(const LiveLogSession&) = delete;
    LiveLogSession& operator=(const LiveLogSession&) = delete;

    // With scanExisting, the existing content is scanned for segment
    // boundaries only; events before the attach are not parsed.
    bool attach(const std::filesystem::path& logPath, bool scanExisting = false);

    // Records byte offsets and metadata for ENCOUNTER_START/END and
    // CHALLENGE_MODE_START/END without parsing combat data
    void scanForSegments();

    void detach();
    bool isAttached() const { return !logPath_.empty(); }
    const std::filesystem::path& getLogPath() const { return logPath_; }

    // Parses bytes added since the last poll. True if any lines were parsed.
    bool poll();

    const std::vector<PullSegment>& getPullHistory() const { return pullHistory_; }

    // Re-parses one segment from its byte range into the historical bundle
    // and switches the display to it. The live bundle is not touched.
    bool parseSegment(const PullSegment& segment);

    // Same for several segments combined (the M+ "Overall" view)
    bool parseSegments(const std::vector<PullSegment>& segments);

    // Switches the display back to the live bundle and frees the historical
    // one. The live poll was never paused, so nothing needs re-parsing.
    void returnToLiveMode();

    SessionViewState getViewState() const { return viewState_; }

    // May still be in progress
    const PullSegment& getCurrentPull() const { return currentPull_; }

    // Thread-safe; call once per frame from the UI thread
    Snapshot getSnapshot() const;

    // While true the displayed data is being rebuilt and the UI should not
    // iterate it this frame
    bool isParsingInProgress() const { return parsingInProgress_.load(); }

    // True during the initial segment scan, so the UI can say it is
    // scanning the log rather than show the generic spinner
    bool isScanningSegments() const { return segmentScanInProgress_.load(); }

    // Lets LiveCombatStats hold the parsing flag across parseSegment() and
    // its own database rebuild that follows, so the render thread never
    // reads a half-rebuilt database in between.
    void beginExternalParse() { parsingInProgress_.store(true); }
    void endExternalParse()   { parsingInProgress_.store(false); }

    // Waits a few milliseconds for a parse slice to finish. The returned
    // lock may not own the mutex if a longer parse still holds it.
    std::unique_lock<std::timed_mutex> tryLockActorMap() const;

    // Blocking version for worker threads that rebuild what the render
    // thread reads. Not for the render thread.
    std::unique_lock<std::timed_mutex> lockActorMap() const;

    // The getters below return the bundle on display: live for Current,
    // historical for a past segment or an M+ Overall. They are not
    // thread-safe on their own; hold the ActorMap lock (or use the
    // snapshot for metadata). A historical bundle is complete before it is
    // displayed and does not change afterwards.
    const ActorMap& getActorMap() const { return displayBundle().actorMap; }

    // SPELL_SUMMON pet -> owner lineage as interned ids, so CombatDatabase
    // can merge summoned Creature/Vehicle pets that the advanced log never
    // tags with an owner. Shared by both bundles.
    std::unordered_map<StringInterner::Id, StringInterner::Id>
    getSummonPetToOwnerMap() const;

    const std::vector<AbsorbEvent>& getAbsorbEvents() const { return displayBundle().absorbEvents; }
    const std::vector<MissedEvent>& getMissedEvents() const { return displayBundle().missedEvents; }
    const std::vector<DispelInterruptEvent>& getDispelEvents() const { return displayBundle().dispelEvents; }
    const std::vector<DeathEvent>& getDeathEvents() const { return displayBundle().deathEvents; }
    const std::vector<AuraEvent>& getAuraEvents() const { return displayBundle().auraEvents; }

    // Phase-rule inputs (same threading rules; prefer the snapshot
    // copies from the render thread)
    const std::unordered_map<uint32_t, LiveFirstCast>& getFirstCasts() const { return firstCasts_; }
    const std::vector<EmoteEvent>& getEmoteEvents() const { return emoteEvents_; }
    const std::unordered_map<std::string, uint16_t>& getGuidToSpecIdMap() const { return guidToSpecId_; }

    const std::unordered_map<std::string, std::string>& getGuidToNameMap() const { return guidToName_; }

    const std::unordered_map<uint32_t, std::string>& getSpellNameMap() const { return spellIdToName_; }

    // Buff applies and removes are stored only for these ids, for the
    // death recap's defensive tracking; an empty set stores none. Safe to
    // call between polls.
    void setTrackedDefensiveIds(std::unordered_set<uint32_t> ids) {
        trackedDefensiveIds_ = std::move(ids);
    }

    size_t getLastParsedOffset() const { return lastParsedOffset_; }
    size_t getCurrentFileSize() const;
    std::chrono::steady_clock::time_point getLastParseTime() const { return lastParseTime_; }

    const std::vector<EncounterSegment>& getEncounters() const { return encounters_; }
    bool isInEncounter() const { return combatState_ == CombatState::InEncounter; }
    bool isInCombat() const { return combatState_ != CombatState::Idle; }

    void resetAll();

    void setOnPullStart(PullStartCallback callback) { onPullStart_ = std::move(callback); }
    void setOnPullEnd(PullEndCallback callback) { onPullEnd_ = std::move(callback); }
    void setOnDataUpdate(DataUpdateCallback callback) { onDataUpdate_ = std::move(callback); }
    void setOnDungeonStart(DungeonStartCallback callback) { onDungeonStart_ = std::move(callback); }

    uint32_t getCurrentDungeonRunId() const { return currentDungeonRunId_; }
    int32_t getDungeonStartTime() const { return dungeonStartTime_ms_; }
    const std::string& getCurrentDungeonName() const { return currentDungeonName_; }

private:
    std::filesystem::path logPath_;
    size_t lastParsedOffset_ = 0;
    std::chrono::steady_clock::time_point lastParseTime_;

    mutable std::mutex snapshotMutex_;
    Snapshot snapshot_;

    // Filled by the poll thread whatever is on screen
    CombatDataBundle liveBundle_;

    // Rebuilt when a past segment or M+ Overall is opened, then unchanged
    // until the next selection or the return to live
    CombatDataBundle historicalBundle_;

    // Shared by both bundles
    std::unordered_map<std::string, std::string> guidToName_;
    std::unordered_map<uint32_t, std::string> spellIdToName_;
    std::vector<EncounterSegment> encounters_;

    // Phase-rule inputs for the loaded data window. firstCasts_ keeps
    // only the earliest completed cast per spell id; emoteEvents_ is
    // capped at kMaxEmotesPerSegment. Both cleared with the rest of
    // the pull data in clearLivePullData().
    static constexpr size_t kMaxEmotesPerSegment = 256;
    std::unordered_map<uint32_t, LiveFirstCast> firstCasts_;
    std::vector<EmoteEvent> emoteEvents_;

    // Spell ids to capture buff applies/removes for (death-recap defensive
    // tracking). Empty by default: no aura-apply capture, no overhead.
    std::unordered_set<uint32_t> trackedDefensiveIds_;

    // Player GUID -> spec id from COMBATANT_INFO. Kept across pulls and
    // segment replays, like guidToName_.
    std::unordered_map<std::string, uint16_t> guidToSpecId_;

    std::vector<PullSegment> pullHistory_;
    PullSegment currentPull_;
    uint32_t nextPullNumber_ = 1;
    uint32_t currentTrashNumber_ = 0;
    uint32_t currentBossNumber_ = 0;

    // The name, spec and pet maps a historical parse fills in on its own
    // thread, kept apart from the session maps until it is published.
    struct SessionLookups {
        std::unordered_map<std::string, std::string> guidToName;
        std::unordered_map<uint32_t, std::string> spellIdToName;
        std::unordered_map<std::string, uint16_t> guidToSpecId;
        std::unordered_map<std::string, std::string> petToOwnerFromSummons;
    };

    // Pet -> owner from SPELL_SUMMON, the fallback for short-lived pets
    // (Wild Imps and the like) whose events have no owner in the advanced
    // block. The offline OutputWriter does the same. Kept across pulls,
    // since a summon can outlive the pull it was cast in.
    std::unordered_map<std::string, std::string> petToOwnerFromSummons_;

    CombatState combatState_ = CombatState::Idle;
    int32_t lastCombatEventTime_ms_ = 0;
    int64_t logStartTimestamp_ = 0;  // Epoch ms of the first line
    bool logStartTimestampSet_ = false;

    // Changed under actorMapMutex_ together with displayHistorical_
    SessionViewState viewState_ = SessionViewState::Live;

    // Set under actorMapMutex_ together with the historical bundle it
    // points at
    bool displayHistorical_ = false;

    const CombatDataBundle& displayBundle() const {
        return displayHistorical_ ? historicalBundle_ : liveBundle_;
    }

    bool inMythicPlus_ = false;
    uint32_t currentKeystoneLevel_ = 0;

    uint32_t currentDungeonRunId_ = 0;    // Bumped at each key start and at the session's first raid boss
    int32_t dungeonStartTime_ms_ = 0;
    std::string currentDungeonName_;      // Zone name, or the boss name for a raid

    // Where the current key began, for its Overall segment
    size_t challengeModeStartOffset_ = 0;
    int32_t challengeModeStartTime_ms_ = 0;

    PullStartCallback onPullStart_;
    PullEndCallback onPullEnd_;
    DataUpdateCallback onDataUpdate_;
    DungeonStartCallback onDungeonStart_;

    // Guards the live bundle and the display switch. The render thread
    // takes it to read, the poll thread to append. Timed so the render
    // thread can wait out a short parse slice.
    mutable std::timed_mutex actorMapMutex_;

    // Held by the history worker during its parse, so a long re-parse
    // never blocks the live poll. The worker takes actorMapMutex_ only
    // briefly to publish. If both are ever held, actorMapMutex_ comes first.
    mutable std::mutex historicalMutex_;
    std::atomic<bool> parsingInProgress_{false};
    std::atomic<bool> segmentScanInProgress_{false};

    // Handles lines [firstLine, firstLine + lineCount) so poll() can work
    // in slices. lines are views into buffer, which starts at file offset
    // bufferOffset.
    void processLines(const std::vector<std::vector<std::string_view>>& lines,
                      std::string_view buffer,
                      size_t bufferOffset,
                      size_t firstLine = 0,
                      size_t lineCount = SIZE_MAX);
    void handleCombatEvent(int32_t timestamp_ms, size_t byteOffset);

    // Stores one line's data in target. Returns true for the event types
    // that count toward pull tracking (see isCombatEventType). Pull
    // tracking and timestamp handling are left to the caller.
    //
    // historical is null for the live parse, which writes the session's
    // name/spec/pet maps and the phase-rule inputs. A historical parse
    // passes its own lookups and leaves the phase-rule inputs alone.
    bool parseAndStoreEvent(CombatDataBundle& target,
                            SessionLookups* historical,
                            const std::vector<std::string_view>& tokens,
                            std::string_view eventType,
                            int32_t timestamp_ms);

    // Owner from the advanced block when it is a player, else from the
    // SPELL_SUMMON map, else 0
    static uint32_t resolveOwnerGuidId(
        const std::unordered_map<std::string, std::string>& petOwners,
        std::string_view source_guid,
        std::string_view advanced_owner_guid);

    // Records HP and power for the actor the advanced block describes.
    // Does nothing if that is neither source nor dest, or max HP is 0.
    void appendResourceSnapshot(CombatDataBundle& target,
                                std::string_view source_guid,
                                std::string_view dest_guid,
                                const struct AdvancedUnitInfo& info,
                                int32_t timestamp_ms);

    // Clears the live bundle and the phase-rule inputs. Names, pull
    // history, encounters and run state are session context and stay.
    // Called from startNewPull() and at a key start with no open pull.
    void clearLivePullData();

    // Parses [startByteOffset, endByteOffset) into target.
    // segmentAbsoluteStart is subtracted from each event's epoch time:
    // the segment start rebases a single pull to 0, logStartTimestamp_
    // keeps times log-relative for the Overall view.
    bool parseByteRangeInto(CombatDataBundle& target,
                            SessionLookups& lookups,
                            size_t startByteOffset, size_t endByteOffset,
                            int64_t segmentAbsoluteStart);

    // A historical parse starts from the known pet owners, and what it
    // learns is merged back on publish (caller holds actorMapMutex_)
    SessionLookups copyPetOwners() const;
    void mergeLookups(const SessionLookups& lookups);
    void handleEncounterStart(const std::vector<std::string_view>& tokens, size_t byteOffset);
    void handleEncounterEnd(const std::vector<std::string_view>& tokens, size_t byteOffset);
    void handleChallengeModeStart(const std::vector<std::string_view>& tokens);
    void handleChallengeModeEnd(const std::vector<std::string_view>& tokens);
    // clearData=false keeps the current data, for a boss start: the boss's
    // own records are only written at the kill.
    void startNewPull(int32_t timestamp_ms, size_t byteOffset, const std::string& label,
                      bool clearData = true);
    void endCurrentPull(int32_t timestamp_ms, size_t byteOffset);
    // ENCOUNTER_START and ENCOUNTER_END, shared by the live poll and the
    // initial scan. clearData is passed through to startNewPull().
    void beginBossPull(std::string_view encounterName, uint32_t encounterId,
                       uint32_t difficultyId, int32_t timestamp_ms,
                       size_t byteOffset, bool clearData);
    void finishBossPull(bool success, int32_t timestamp_ms, size_t byteOffset);
    // Adds a scanned trash segment of the current key to pullHistory_
    void pushTrashSegment(size_t startOffset, size_t endOffset,
                          int32_t startTime_ms, int32_t endTime_ms);
    // Adds the "Overall - <dungeon>" segment spanning the current key
    const PullSegment& pushDungeonOverall(size_t endOffset, int32_t endTime_ms);
    // One boundary line found by scanForSegments(). lastBoundaryOffset and
    // lastBoundaryTime_ms mark where the key's next trash segment starts.
    void handleScannedBoundary(std::string_view eventType, std::string_view line,
                               size_t absLineStart, size_t absLineEnd,
                               size_t& lastBoundaryOffset, int32_t& lastBoundaryTime_ms);
    void checkIdleTimeout(int32_t currentTime_ms, size_t byteOffset);
    // Sets the group header fields from the current key; no-op outside M+
    void stampGroupInfo(PullSegment& seg) const;

    // Ends the current key: adds an EmptyRun placeholder if it left no
    // segments, then sets the end time on all its segments. Called at the
    // next key start, at CHALLENGE_MODE_END and at the end of a scan.
    void closeCurrentRun(size_t endOffset, int32_t endTime_ms);
    // Epoch milliseconds including the date. Time of day alone would make
    // a pull across midnight end before it started.
    int64_t parseTimestampMs(std::string_view date, std::string_view time);
    void updateSnapshot();
};
