#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "Core/Benchmark/BenchmarkComparison.h"
#include "Core/Benchmark/MentorProfile.h"

// Report card comparing the user's pull with a reference log.
//
// Reads a finished BenchmarkResult and draws it. The analysis lives in
// Core/Benchmark; this panel only words and lays it out.
//
// Findings are phrased as differences, not mistakes. The log cannot see group
// composition, assignments, or who a mechanic targeted, so "3.2s later than
// the reference" is stated but "too slow" is not.

class SpellIconLoader;

namespace benchmark_ui {

// What the user asked to do next.
enum class ReportAction : uint8_t {
    None = 0,
    OpenReplay,       // Open the replay with both actors overlaid
    JumpToTime,       // Scrub the replay to a specific finding
    ReviewClasses,    // Blocked on unconfirmed spells; go and rule on them
    ChangeMentor,     // Pick a different reference
    RebuildMentor,    // Re-cut the reference from its own log under new rulings
    Dismiss,
};

struct ReportContext {
    const benchmark::BenchmarkResult* result = nullptr;
    const benchmark::MentorProfile* userProfile = nullptr;
    const benchmark::MentorProfile* mentorProfile = nullptr;

    // Display names, resolved by the caller so this panel needs no database.
    // Pointers rather than copies because the context is rebuilt every frame;
    // the session that owns the maps outlives the card.
    const std::unordered_map<uint32_t, std::string>* spellNames = nullptr;
    const std::unordered_map<uint32_t, std::string>* npcNames = nullptr;

    std::string userName;
    std::string mentorName;
    std::string encounterName;

    SpellIconLoader* iconLoader = nullptr;

    // Set when the rulings have changed since these profiles were built.
    // Re-running the comparison is not enough, because which spells count as
    // burst (or count at all) is decided during extraction, so the card shows
    // a notice instead.
    bool profilesStale = false;

    // Live mode only: what the watcher is doing right now. Re-reading a raid
    // pull takes seconds, so the card shows this instead of looking frozen.
    // Empty outside live mode.
    std::string liveStatus;

    // False when the reference predates origin tracking, or came from a flow
    // that never had a path to record. The rebuild offer is hidden in that
    // case rather than shown and then failing.
    bool canRebuild = false;
};

class UIBenchmarkReportCard {
public:
    UIBenchmarkReportCard() = default;

    // Draws the card. Returns what the user asked for, if anything.
    ReportAction render(const ReportContext& context);

    bool isVisible() const { return visible_; }
    void setVisible(bool visible);
    void toggleVisible() { setVisible(!visible_); }

    // Time the user clicked through to, valid when render() returned
    // JumpToTime.
    int32_t getRequestedTimeMs() const { return requestedTimeMs_; }

private:
    void renderHeadline(const ReportContext& context);
    void renderBlocked(const ReportContext& context, ReportAction& action);
    void renderFindings(const ReportContext& context, ReportAction& action);
    void renderCooldownSection(const ReportContext& context);
    void renderThroughputSection(const ReportContext& context);
    void renderTimelineSection(const ReportContext& context);

    // Row-by-row differences: damage per target, damage per cast second, and
    // where the globals went.
    void renderBreakdownSection(const ReportContext& context);

    // Both paths through the pull, optionally re-expressed relative to the
    // boss so two pulls fought in different corners still line up.
    void renderMovementSection(const ReportContext& context);

    // One comparison drawn as a pair of bars.
    //
    // scaleMax lets a group of rows share an axis; normalising each row to its
    // own peak would draw a 13M row and a 400K row at the same width. Pass 0
    // for a row with no neighbours to share with.
    void renderPairBar(const char* label,
                       double userValue,
                       double mentorValue,
                       const std::string& userText,
                       const std::string& mentorText,
                       bool higherIsBetter,
                       float width,
                       double scaleMax = 0.0);

    std::string spellName(const ReportContext& context, uint32_t spellId) const;
    std::string npcName(const ReportContext& context, uint32_t npcId) const;

    bool visible_ = false;
    bool justOpened_ = false;
    int activeTab_ = 0;
    int32_t requestedTimeMs_ = -1;

    // Icon mappings are requested once per open; the set of ids the card can
    // show does not change while it is open.
    bool iconsPreloaded_ = false;

    // Set by a section that has no way to return an action of its own, and
    // consumed by render() at the end of the frame.
    ReportAction pendingAction_ = ReportAction::None;

    // Window buffs are off by default: they matter when a window looks
    // unexplainable, and shown always they crowd out the cast sequence.
    bool showWindowBuffs_ = false;

    // Blacklisted buffs are hidden by default. Revealing them is how a
    // blacklisting gets found again and undone.
    bool showBlacklistedBuffs_ = false;

    // Set by a right-click on a buff row, applied after the frame closes -
    // rewriting the classification mid-draw would invalidate the very list
    // being iterated.
    uint32_t pendingBlacklist_ = 0;

    // Which burst window the timeline is narrowed to. 0 is the whole pull.
    int timelineWindow_ = 0;

    // Absolute coordinates or offsets from the boss. Relative is the default
    // because two pulls of the same fight rarely happen in the same spot.
    bool movementRelative_ = true;

    // Movement playback runs on its own clock rather than the replay's, so it
    // can go faster (20x) than the replay allows.
    bool moviePlaying_ = false;
    float movieSpeed_ = 4.0f;
    int32_t movieTimeMs_ = 0;
    int movieTrailIndex_ = 2;   // Into kTrailOptions
};

} // namespace benchmark_ui
