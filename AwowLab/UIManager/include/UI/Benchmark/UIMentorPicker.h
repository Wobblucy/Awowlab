#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/Benchmark/BenchmarkSession.h"

// Picks what to compare: first the encounter, then a stored reference for it,
// then (in the report flow) the pull to measure against it.

namespace benchmark_ui {

// A cached fight the picker can offer as the pull to measure. Mirrors the
// columns of the cached-fights browser.
struct PickerFight {
    std::string reportCode;
    int32_t fightId = -1;
    std::string label;          // Encounter name
    uint32_t encounterId = 0;
    int32_t durationMs = 0;
    int64_t startTimeMs = 0;    // Absolute, for the date
    std::string difficulty;     // "Mythic", "Heroic", ...
    std::string groupLead;      // First player in the group, to tell runs apart
    bool cached = false;        // Events on disk, or metadata only
};

struct MentorPickerContext {
    // Encounters that have at least one stored reference, in the order the
    // list should show them.
    std::vector<std::pair<uint32_t, std::string>> encounters;

    // Cached fights available to measure, filtered by encounterId once an
    // encounter has been chosen.
    std::vector<PickerFight> fights;
};

// What the user settled on.
struct MentorPickerResult {
    bool confirmed = false;
    bool cancelled = false;

    benchmark::BenchmarkSession::MentorKey mentor;

    // Set when the picker was asked for a pull to measure as well.
    std::string reportCode;
    int32_t fightId = -1;
};

class UIMentorPicker {
public:
    // Live mode stops at the reference; the report flow goes on to pick the
    // pull being measured.
    enum class Mode : uint8_t {
        ReferenceOnly,      // Encounter -> reference
        ReferenceAndFight,  // Encounter -> reference -> pull to measure
    };

    void open(Mode mode);
    void close();
    bool isOpen() const { return open_; }

    // Returns true on the frame the user confirmed or cancelled.
    bool render(const MentorPickerContext& context);

    const MentorPickerResult& result() const { return result_; }

private:
    // Which reference the delete confirmation is about, and whether the
    // popup still needs opening. Deferred because removing an entry changes
    // the list being iterated.
    benchmark::BenchmarkSession::MentorKey pendingDelete_{};
    bool confirmDelete_ = false;

    void renderEncounterStep(const MentorPickerContext& context);
    void renderMentorStep(const MentorPickerContext& context);
    void renderFightStep(const MentorPickerContext& context);

    enum class Step : uint8_t { Encounter, Mentor, Fight };

    bool open_ = false;
    bool popupOpened_ = false;
    Mode mode_ = Mode::ReferenceOnly;
    Step step_ = Step::Encounter;

    uint32_t selectedEncounterId_ = 0;
    std::string selectedEncounterName_;
    benchmark::BenchmarkSession::MentorKey selectedMentor_;
    std::string selectedMentorLabel_;

    MentorPickerResult result_;
};

} // namespace benchmark_ui
