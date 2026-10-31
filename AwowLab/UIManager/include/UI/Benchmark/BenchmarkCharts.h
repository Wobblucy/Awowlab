#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

// Chart helpers for the benchmark report card.
//
// Wraps ImPlot so the plots use the awlui palette. Each chart draws the user's
// series against the reference's. The user's series uses the amber accent
// (reserved for data series, see AwlUI/Theme.h) and the reference a neutral
// tone.

namespace benchmark_ui {

// Push/pop the awlui-derived ImPlot style.
void PushPlotStyle();
void PopPlotStyle();

// Two series over the same time axis, the reference drawn behind the user's,
// with the gap between them shaded.
void DrawComparisonLine(const char* id,
                        const char* userLabel,
                        const char* mentorLabel,
                        const std::vector<double>& timesSec,
                        const std::vector<double>& userValues,
                        const std::vector<double>& mentorValues,
                        ImVec2 size);

// One metric as a tile: both values, a bar each on a shared track, and the
// delta.
struct VersusStat {
    const char* label = "";
    std::string userText;       // Formatted, e.g. "1.2M"
    std::string mentorText;
    double userValue = 0.0;     // Raw, for sizing the bars
    double mentorValue = 0.0;
    std::string deltaText;      // Pre-signed, e.g. "-180k"
    bool higherIsBetter = true; // Damage taken reads the other way round
};

void DrawVersusStat(const VersusStat& stat, ImVec2 size);

// Splits the damage shortfall into its burst and filler parts as one bar.
void DrawShortfallSplit(int64_t burstDelta,
                        int64_t fillerDelta,
                        const char* burstLabel,
                        const char* fillerLabel,
                        ImVec2 size);

// One cooldown use, expanded: what was cast inside the window and which buffs
// were up.
struct CooldownDetailRow {
    std::string title;        // "Metamorphosis #3"
    std::string userTime;     // "(3:04 - 3:55)", empty when never used
    std::string mentorTime;
    std::string userOutput;   // "6.09m damage (119k DPS)"
    std::string mentorOutput;

    // Casts in order, with a short label for spells that have no icon.
    struct Cast {
        uint32_t spellId = 0;
        std::string shortLabel;
    };
    std::vector<Cast> userCasts;
    std::vector<Cast> mentorCasts;

    // Buffs that overlapped the window, drawn as bars on the window's axis.
    struct WindowBuff {
        uint32_t spellId = 0;
        std::string label;    // Name and duration, for the row
        float start = 0.0f;   // 0..1 across the window
        float end = 1.0f;
        bool blacklisted = false;   // Shown greyed when revealed
    };
    std::vector<WindowBuff> userBuffs;
    std::vector<WindowBuff> mentorBuffs;

    // Running damage through each window, one sample per second from the
    // moment that window opened, so both curves start at zero.
    std::vector<double> userCurve;
    std::vector<double> mentorCurve;

    // Damage per target inside the window, largest first, already formatted.
    std::vector<std::string> userTargets;
    std::vector<std::string> mentorTargets;
};

// Two windows plotted from a shared zero.
void DrawWindowCurve(const char* id,
                     const std::vector<double>& userCurve,
                     const std::vector<double>& mentorCurve,
                     const char* userLabel,
                     const char* mentorLabel,
                     ImVec2 size);

// Draws one expanded cooldown use, the user's side above the reference's.
// `iconFor` returns 0 when a spell has no icon, and the short label is drawn
// instead. Returns the spell id the user right-clicked to blacklist, or 0.
uint32_t DrawCooldownDetail(const CooldownDetailRow& row,
                            const char* userLabel,
                            const char* mentorLabel,
                            const std::function<ImTextureID(uint32_t)>& iconFor,
                            bool showBuffs,
                            float width);

// Cooldown uses from both players on one time axis, as icons with the
// window drawn as a bar behind each.
struct CooldownMark {
    int32_t timeMs = 0;
    int32_t windowMs = 0;
    uint32_t spellId = 0;
    std::string shortLabel;   // Drawn when no icon exists
};

void DrawCooldownTimelineRows(const char* id,
                              const std::vector<CooldownMark>& userMarks,
                              const std::vector<CooldownMark>& mentorMarks,
                              int32_t pullDurationMs,
                              const char* userLabel,
                              const char* mentorLabel,
                              const std::function<ImTextureID(uint32_t)>& iconFor,
                              ImVec2 size);

// Both players' paths through the pull, drawn on one plane.
//
// In relative mode each sample is an offset from the boss at that moment, so
// pulls fought in different parts of the room line up.
struct PositionPath {
    std::vector<float> x;
    std::vector<float> y;
    std::vector<int32_t> timeMs;   // Pull-relative, for playback
};

// How much of the path to draw at a given moment during playback.
struct PositionPlayback {
    bool active = false;
    int32_t currentMs = 0;

    // 0 keeps everything drawn so far.
    int32_t trailMs = 20000;
};

void DrawPositionMap(const char* id,
                     const PositionPath& userPath,
                     const PositionPath& mentorPath,
                     const char* userLabel,
                     const char* mentorLabel,
                     bool relativeToBoss,
                     const PositionPlayback& playback,
                     ImVec2 size);

// Two compositions side by side, as rings, with a shared legend.
struct CompositionSlice {
    std::string label;
    double userValue = 0.0;
    double mentorValue = 0.0;
};

void DrawCompositionRings(const char* id,
                          std::vector<CompositionSlice> slices,
                          const char* userLabel,
                          const char* mentorLabel,
                          const std::function<std::string(double)>& formatValue,
                          ImVec2 size);

// One stretch of downtime, with the casts that led into it drawn to scale.
struct DowntimeStrip {
    struct Cast {
        uint32_t spellId = 0;
        int32_t occupancyMs = 0;
        std::string shortLabel;
    };
    std::vector<Cast> leadIn;

    uint32_t resumedSpellId = 0;
    std::string resumedLabel;

    int32_t gapMs = 0;
    std::string caption;      // Timestamp and duration, already formatted
    std::string context;      // Debuffs up for the whole gap, or empty
    bool inCooldown = false;
};

void DrawDowntimeStrip(const char* id,
                       const DowntimeStrip& strip,
                       const std::function<ImTextureID(uint32_t)>& iconFor,
                       float width);

} // namespace benchmark_ui
