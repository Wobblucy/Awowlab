#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Benchmark/SpellClassification.h"

// Panel where the user rules on what each of their spells and buffs does.
//
// The comparison waits until every spell is confirmed. Suggestions are
// pre-filled from cast cadence, but nothing counts until the user confirms
// it, because a wrong role changes every window built on it.

class SpellIconLoader;

namespace benchmark_ui {

struct ClassificationReviewContext {
    uint16_t specId = 0;
    std::string specName;

    // Display names, resolved by the caller so this panel needs no database.
    // A pointer rather than a copy because the context is rebuilt every
    // frame; the session that owns the map outlives the panel.
    const std::unordered_map<uint32_t, std::string>* spellNames = nullptr;

    SpellIconLoader* iconLoader = nullptr;
};

class UIClassificationReview {
public:
    UIClassificationReview() = default;

    // Draws the review. Returns true on the frame the user finished
    // confirming everything, so the caller can retry the comparison.
    bool render(const ClassificationReviewContext& context);

    bool isVisible() const { return visible_; }
    void setVisible(bool visible);
    void toggleVisible() { setVisible(!visible_); }

    // True while anything is still awaiting a ruling for the given spec.
    static bool hasPendingWork(uint16_t specId);

private:
    void renderSpellRow(const ClassificationReviewContext& context,
                        benchmark::SpellClass& spell,
                        bool& changed);
    void renderAuraRow(const ClassificationReviewContext& context,
                       benchmark::AuraClass& aura,
                       bool& changed);

    // Why a suggestion was made, shown under the suggestion.
    std::string describeEvidence(const benchmark::SpellClass& spell) const;
    std::string describeEvidence(const benchmark::AuraClass& aura) const;

    // Confirmed rows sorted by name. The store keeps rulings in an unordered
    // map, so iterating it directly gives a different order each session.
    std::vector<benchmark::SpellClass*> sortedConfirmedSpells(
        const ClassificationReviewContext& context,
        benchmark::SpecClassification& spec) const;
    std::vector<benchmark::AuraClass*> sortedConfirmedAuras(
        const ClassificationReviewContext& context,
        benchmark::SpecClassification& spec) const;

    // Case-insensitive substring test against the filter box.
    bool matchesFilter(const std::string& name, uint32_t id) const;

    // Whether this id exists as both a cast and a buff. Such a pair is one
    // ability: the cast opens the cooldown window and the buff ending closes
    // it, which gives the window its real length.
    bool pairedWithAura(const ClassificationReviewContext& context,
                        uint32_t spellId) const;
    bool pairedWithCast(const ClassificationReviewContext& context,
                        uint32_t spellId) const;

    // True when another spell in this log has the same display name. Rulings
    // are per id, so such rows also show the id.
    bool nameIsAmbiguous(const ClassificationReviewContext& context,
                         uint32_t spellId,
                         const std::string& name) const;

    bool visible_ = false;
    bool justOpened_ = false;
    bool showConfirmed_ = false;
    bool hadPendingOnOpen_ = true;
    char filter_[64] = {};
};

} // namespace benchmark_ui
