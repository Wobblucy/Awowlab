#include "PhaseResolver.h"

#include <algorithm>

namespace phase {

std::optional<int32_t> resolveRuleTime(const PhaseSettings::PhaseRule& rule,
                                       const RuleInputs& inputs) {
    // The offset shifts whatever the rule matched, and is clamped into the
    // pull: a boundary before the pull started belongs at the start, not at a
    // negative time that later arithmetic would read as "never happened".
    const auto shifted = [&](int32_t time_ms) -> std::optional<int32_t> {
        const int32_t adjusted = time_ms + rule.offsetMs;
        return std::clamp(adjusted, inputs.pullStart_ms, inputs.pullEnd_ms);
    };

    switch (rule.kind) {
        case PhaseSettings::RuleKind::SpellCast:
            if (inputs.firstCastTime) {
                if (const auto castTime = inputs.firstCastTime(rule.spellId)) {
                    return shifted(*castTime);
                }
            }
            return std::nullopt;
        case PhaseSettings::RuleKind::Elapsed: {
            int32_t time_ms = inputs.pullStart_ms + rule.elapsedMs;
            if (time_ms < inputs.pullEnd_ms) {
                return time_ms;   // The elapsed value is already the offset.
            }
            return std::nullopt;
        }
        case PhaseSettings::RuleKind::Emote:
            if (inputs.firstEmoteTime) {
                if (const auto emoteTime = inputs.firstEmoteTime(rule.emoteText)) {
                    return shifted(*emoteTime);
                }
            }
            return std::nullopt;
    }
    return std::nullopt;
}

std::vector<ResolvedPhase> resolvePhases(const std::vector<PhaseSettings::PhaseRule>& rules,
                                         const RuleInputs& inputs) {
    // Each rule contributes one boundary; rules whose trigger never
    // fired this pull are dropped.
    struct PhaseBoundary {
        int32_t time_ms;
        const std::string* label;
    };
    std::vector<PhaseBoundary> boundaries;
    boundaries.reserve(rules.size());
    for (const auto& rule : rules) {
        if (auto time = resolveRuleTime(rule, inputs)) {
            boundaries.push_back({*time, &rule.label});
        }
    }
    if (boundaries.empty()) {
        return {};
    }

    std::sort(boundaries.begin(), boundaries.end(),
              [](const PhaseBoundary& a, const PhaseBoundary& b) {
                  return a.time_ms < b.time_ms;
              });

    // Two rules landing on the same millisecond would produce an empty
    // phase window - keep only the first
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end(),
                                 [](const PhaseBoundary& a, const PhaseBoundary& b) {
                                     return a.time_ms == b.time_ms;
                                 }),
                     boundaries.end());

    // P1 always runs from the pull start to the first boundary; each
    // boundary opens the next phase. Unnamed rules get positional
    // "P2"/"P3" labels.
    std::vector<ResolvedPhase> phases;
    phases.reserve(boundaries.size() + 1);
    phases.push_back(ResolvedPhase{"P1", inputs.pullStart_ms, boundaries.front().time_ms});
    for (size_t i = 0; i < boundaries.size(); ++i) {
        int32_t end_ms = (i + 1 < boundaries.size()) ? boundaries[i + 1].time_ms
                                                     : inputs.pullEnd_ms;
        std::string label = boundaries[i].label->empty() ? ("P" + std::to_string(i + 2))
                                                         : *boundaries[i].label;
        phases.push_back(ResolvedPhase{std::move(label), boundaries[i].time_ms, end_ms});
    }
    return phases;
}

} // namespace phase
