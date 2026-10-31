#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Benchmark/BenchmarkEngine.h"
#include "Core/Benchmark/LivePullBridge.h"
#include "Core/Benchmark/MentorProfile.h"
#include "Core/Benchmark/ProfileExtractor.h"
#include "Core/Benchmark/SpellClassification.h"

namespace benchmark {

// Holds the state of one comparison across the several steps it takes:
// import a reference, classify the spec, extract both sides, compare.
//
// Exists because those steps are separated by user decisions - which player to
// profile, what each spell does - and the intermediate results have to survive
// in between. Without somewhere to keep them the UI would end up owning the
// analysis, which is exactly where it would rot.
//
// Either side can come from a WarcraftLogs report or a local log; both arrive
// as ExtractedData, so comparing two cached reports works the same way as
// comparing a report against the log open in the app.

enum class SessionStage : uint8_t {
    Empty = 0,          // No reference chosen
    NeedsClassification, // Reference imported; spells awaiting the user
    Ready,              // Classified; comparison can run
    Compared,           // A result is available
};

class BenchmarkSession {
public:
    static BenchmarkSession& instance();

    // Where the currently loaded log came from, recorded once when it opened.
    // setMentor stamps the reference with it, so a later ruling change can
    // re-cut the same pull without walking the picker again. Called by the
    // flows that open logs; everything downstream just builds references.
    void setLoadedSource(const std::string& reportCode,
                         const std::string& logPath,
                         int32_t fightId) {
        loadedReportCode_ = reportCode;
        loadedLogPath_ = logPath;
        loadedFightId_ = fightId;
    }

    // Re-cut the reference from data still in hand, after the user has ruled
    // on the suggestions.
    //
    // setMentor has to extract before the review, because the review needs the
    // suggestions that extraction produces - but at that moment nothing is
    // confirmed, so every "ignore this" ruling the user is about to make is
    // still absent from the profile. Without this the reference keeps counting
    // spells the user explicitly excluded. Cheap: the events are already
    // parsed and in memory.
    bool refreshMentorAfterReview(const ExtractedData& data);

    // Same thing, for panels that have no parsed events of their own to pass.
    // The pointer is owned by whoever loaded the log and cleared when it goes
    // away; a null one simply means the refresh cannot run here, and the
    // report card's stale banner covers that case.
    void setLiveExtractedData(const ExtractedData* data) { liveData_ = data; }
    bool refreshMentorAfterReview() {
        return liveData_ ? refreshMentorAfterReview(*liveData_) : false;
    }

    // Whether a re-cut could be done from here at all. Distinct from
    // refreshMentorAfterReview returning false, which also means "nothing has
    // changed" - a caller deciding whether to hand the job to someone else
    // needs to tell those two apart.
    bool canRefreshInPlace() const { return liveData_ != nullptr; }

    // The events the reference is being cut from, for panels that need to
    // offer real spells and emotes rather than a blank editor. Null outside a
    // prepare, so callers check before using it.
    const ExtractedData* liveExtractedData() const { return liveData_; }

    // Re-cut the stored reference from a log the caller has already loaded.
    // The caller re-opens the source named by mentorOrigin(); this session
    // owns the part that has to stay consistent - same actor, same spec, same
    // encounter, but under the rulings now on file.
    bool rebuildMentorFrom(const ExtractedData& data);

    // Re-run the comparison after a ruling changed. recompare() alone is not
    // enough there: the user's profile was extracted under the old rulings
    // just like the reference's, and re-scoring it unchanged would diff a
    // fresh reference against a stale player. When the live events are still
    // on hand and hold the same actor, the user's side is re-cut first;
    // otherwise this falls back to plain recompare().
    bool recompareAfterRulingChange();

    const MentorProfile::Origin& mentorOrigin() const { return mentor_.origin; }

    // Reduce one player from an imported report into the reference profile.
    // Also produces classification suggestions for the spec, which the user
    // then rules on. Returns how many entries need a decision. The reference
    // is stamped with the origin recorded by setLoadedSource.
    size_t setMentor(const ExtractedData& data,
                     const std::string& actorGuid,
                     const std::string& playerName,
                     uint16_t specId,
                     const std::string& specName,
                     const std::string& sourceLabel,
                     uint32_t encounterId,
                     uint32_t difficulty);

    // Profile the user's own pull and run the comparison. Safe to call when
    // classification is incomplete: the result comes back invalid with a
    // reason, which is the UI's cue to open the review.
    const BenchmarkResult& compareAgainst(const ExtractedData& data,
                                          const std::string& actorGuid,
                                          const std::string& playerName);

    SessionStage stage() const { return stage_; }
    bool hasMentor() const { return mentor_.isValid(); }

    const MentorProfile* mentorProfile() const {
        return mentor_.isValid() ? &mentor_ : nullptr;
    }
    const MentorProfile* userProfile() const {
        return user_.isValid() ? &user_ : nullptr;
    }
    const BenchmarkResult& result() const { return result_; }

    uint16_t specId() const { return mentor_.specId; }
    const std::string& mentorName() const { return mentor_.playerName; }
    const std::string& userName() const { return user_.playerName; }

    // Names for whatever the report card needs to display, gathered from both
    // sides as they were imported.
    // Creature id -> name, so the report card can say "Voidbound Fanatic"
    // instead of "Enemy 252918". The profile stores target damage by npcId,
    // which is language independent and stable but unreadable on its own.
    const std::unordered_map<uint32_t, std::string>& npcNames() const {
        return npcNames_;
    }

    const std::unordered_map<uint32_t, std::string>& spellNames() const {
        return spellNames_;
    }

    // Re-run the comparison against the profiles already held. Use after the
    // user finishes classifying.
    //
    // Note this does NOT rebuild the profiles: the burst/filler split was cut
    // with whatever was confirmed when they were extracted, so a change to
    // what counts as a major cooldown needs compareAgainst() called again with
    // the log. needsRebuild() reports exactly that case rather than letting a
    // stale split pass for a fresh one.
    bool recompare();

    // True when the confirmed major cooldowns no longer match the ones the
    // held profiles were built with.
    bool needsRebuild() const;

    // Live mode: compare every failed boss pull against the stored reference
    // as it happens. Off by default, since the ordinary overlay should not
    // start doing analysis nobody asked for.
    void setLiveCompareArmed(bool armed) { liveCompareArmed_ = armed; }
    bool isLiveCompareArmed() const { return liveCompareArmed_; }

    // Run a comparison for a pull that just ended live. Re-reads the pull from
    // the log, so it must NOT be called from the poll thread - that thread can
    // be holding the session's actor-map lock, and this does real work.
    //
    // selfName identifies which player to profile; live mode has no inherent
    // notion of who the user is. Returns true when a comparison was produced.
    bool compareLivePull(const std::filesystem::path& logPath,
                         size_t startByteOffset,
                         size_t endByteOffset,
                         uint32_t encounterId,
                         const std::string& selfName);

    // Everyone who fought in a pull, for asking the user which one is them.
    //
    // Live logs carry no "this is you" marker, so the first pull of a session
    // is what supplies the roster. Scans the head of the pull's byte range
    // rather than parsing it, but still touches the disk, so keep it off the
    // poll thread.
    std::vector<RosterEntry> rosterForPull(const std::filesystem::path& logPath,
                                           size_t startByteOffset,
                                           size_t endByteOffset) const;

    // Set when a live comparison finishes, so the UI can raise the report card
    // from its own thread instead of the worker touching ImGui.
    bool consumeLiveResultReady() {
        const bool ready = liveResultReady_;
        liveResultReady_ = false;
        return ready;
    }

    // Raised by the report card when the reference is stale, and acted on by
    // whichever layer can re-open a log; the panel itself does not open
    // reports or files.
    void requestMentorRebuild() { rebuildRequested_ = true; }
    bool isMentorRebuildRequested() const { return rebuildRequested_; }
    void clearMentorRebuildRequest() { rebuildRequested_ = false; }

    // Set when the loaded log is being opened to build a reference rather
    // than to watch, so the encounter view can tell the two apart.
    void setPrepareMentorPending(bool pending) { prepareMentorPending_ = pending; }
    bool isPrepareMentorPending() const { return prepareMentorPending_; }

    // Set when the loaded log is being measured against the active reference,
    // for the same reason.
    void setReportComparePending(bool pending) { reportComparePending_ = pending; }
    bool isReportComparePending() const { return reportComparePending_; }

    void clear();

    // Serialize and restore the chosen reference. Classifications are saved
    // separately by ClassificationStore, to classifications.json.
    std::string mentorToJson() const;
    bool loadMentorFromJson(const std::string& json);

    // References are stored per encounter and spec in mentors.json (see
    // BenchmarkStore), so live mode can pick the right one when a pull ends.
    // Loading also migrates references older builds kept in the settings file,
    // and restores the self player name, which does live in settings.
    void saveMentorsFile() const;
    void loadSavedMentors();

    // Which character is the user. The combat log cannot tell, so live mode
    // is told once and the name is kept in settings.
    const std::string& selfPlayerName() const { return selfPlayerName_; }
    void setSelfPlayerName(const std::string& name);

    // One reference per encounter and spec, so preparing a reference for a
    // second spec does not replace the first.
    struct MentorKey {
        uint32_t encounterId = 0;
        uint16_t specId = 0;

        bool operator==(const MentorKey& other) const {
            return encounterId == other.encounterId && specId == other.specId;
        }
    };

    // A stored reference, described well enough for a picker to show it
    // without parsing the whole profile back.
    struct MentorEntry {
        MentorKey key;
        std::string playerName;
        std::string sourceLabel;    // Report code it came from
        std::string specName;
        int32_t pullDurationMs = 0;
        bool kill = false;
    };

    // Make a stored reference the active one. Returns false when none matches,
    // which the caller reports rather than comparing against whatever happened
    // to be loaded.
    bool activateMentor(const MentorKey& key);

    // Every stored reference, for the pickers. Ordered by encounter then spec
    // so the list is stable between launches.
    std::vector<MentorEntry> storedMentors() const;

    // References for one encounter, which is what the compare and live flows
    // offer once a boss has been chosen.
    std::vector<MentorEntry> mentorsForEncounter(uint32_t encounterId) const;

    bool hasMentorFor(uint32_t encounterId) const;

    // Drop a stored reference. Preparing is cheap to redo, and a list with no
    // way to remove things fills up with mistakes.
    void forgetMentor(const MentorKey& key);

private:
    BenchmarkSession() = default;

    // Rebuild the extraction request from what the user has confirmed, so the
    // burst windows follow the current classification rather than whatever
    // was current when the profile was first built.
    // Fill in any spell name the log did not carry from the shipped spell
    // data. Imported reports name almost nothing, so without this the
    // classification list is a wall of bare ids.
    void resolveSpellNames(const ExtractedData& data);
    void resolveNpcNames(const ExtractedData& data);

    ExtractionRequest buildRequest(const std::string& actorGuid,
                                   const std::string& playerName,
                                   uint16_t specId,
                                   const std::string& sourceLabel,
                                   uint32_t encounterId,
                                   uint32_t difficulty) const;

    // Shared tail of the two rebuild paths: extract the reference again under
    // the rulings now on file, keeping its identity. actorGuid is taken by
    // value because callers pass strings owned by mentor_, which this
    // function reassigns.
    bool recutMentor(const ExtractedData& data, std::string actorGuid);

    void refreshStage();

    MentorProfile mentor_;
    MentorProfile user_;
    BenchmarkResult result_;
    SessionStage stage_ = SessionStage::Empty;

    // Kept so recompare() can rebuild the user's side after classification
    // changes what counts as burst, without asking for the log again.
    std::string userGuid_;
    std::string userName_;
    int32_t userDeathMs_ = -1;

    // Assemble an Origin for one actor out of the recorded source.
    MentorProfile::Origin loadedOrigin(const std::string& actorGuid) const {
        MentorProfile::Origin origin;
        origin.reportCode = loadedReportCode_;
        origin.logPath = loadedLogPath_;
        origin.fightId = loadedFightId_;
        origin.actorGuid = actorGuid;
        return origin;
    }

    bool liveCompareArmed_ = false;
    bool prepareMentorPending_ = false;
    bool rebuildRequested_ = false;
    const ExtractedData* liveData_ = nullptr;
    std::string loadedReportCode_;
    std::string loadedLogPath_;
    int32_t loadedFightId_ = 0;
    bool reportComparePending_ = false;

    // Written by the worker that runs a live comparison, read by the UI
    // thread. Atomic because those are genuinely different threads.
    std::atomic<bool> liveResultReady_{false};

    // Serialized profiles, keyed "<encounterId>:<specId>". Kept as JSON
    // because only the active one is ever parsed. The string key matches the
    // key in mentors.json and avoids needing a hash for the pair.
    std::unordered_map<std::string, std::string> savedMentors_;

    static std::string mentorKeyString(const MentorKey& key);
    static std::optional<MentorKey> mentorKeyFromString(std::string_view text);

    std::string selfPlayerName_;

    std::unordered_map<uint32_t, std::string> spellNames_;
    std::unordered_map<uint32_t, std::string> npcNames_;
};

} // namespace benchmark
