#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace benchmark {

// What a spell or aura does, so the comparison knows how to weigh it.
//
// This lets the benchmark work for any spec without per-spec analysis code:
// the user classifies their spec's spells once, and every later log for that
// spec reuses the result.
//
// Nothing here is decided automatically. Loading a mentor log produces
// suggestions from cast-cadence heuristics, and the user confirms or corrects
// each one, so a wrong guess never enters the analysis unseen.

// Auto-attack. Logs synthesize a cast event for every swing, and the id they
// use belongs to an unrelated retired spell in the game's own data - so it
// needs naming by hand as well as excluding from cast rates.
constexpr uint32_t kMeleeSpellId = 1;

enum class CastRole : uint8_t {
    Unclassified = 0,
    Filler,          // The default spender; what you press with nothing else up
    ProcConsumer,    // Cast in response to a proc
    ResourceSpender, // Dumps a resource to avoid capping
    MajorCooldown,   // Long cooldown, defines a burst window
    MinorCooldown,   // Short cooldown, folded into the rotation
    Defensive,       // Mitigation / survival
    Utility,         // Movement, dispel, interrupt, raid support
    Ignored,         // Not worth comparing (mount, addon noise, etc.)

    // Appended, not inserted: the numeric values are what gets serialized, so
    // reordering this list would silently re-role every ruling already saved.
    Trinket,         // On-use trinket - real damage, but on its own cooldown
                     // rather than part of the rotation
    Cantrip,         // Off-global; pressing it costs nothing, so a missed one
                     // is a pure loss rather than a trade
    Melee,           // Auto-attack. Real damage that keeps counting, but it
                     // swings on its own - it is not a global anyone spends,
                     // and its rate says nothing about how a pull was played
};

enum class AuraRole : uint8_t {
    Unclassified = 0,
    Proc,          // Grants a window to consume; expiring unused is a loss
    Maintenance,   // Should be kept up; uptime is the metric
    CooldownBuff,  // The buff side of a major cooldown; defines its window
    Unimportant,   // Present but not worth reporting

    // Appended, not inserted - the numeric values are serialized.
    MajorCooldown, // A buff that IS the cooldown window.
                   //
                   // Gaining it opens a burst window and losing it closes one,
                   // whether or not a cast was ever logged. Several of these
                   // overlapping hold a single window open until the last one
                   // drops - press a cooldown, then a trinket, then a potion
                   // and that is one burst, not three.
                   //
                   // This is the reliable way to mark a window: the buff is
                   // what the damage actually rode on, and some abilities are
                   // never logged as a cast at all.

    // Appended, not inserted: the values are serialized.
    Channel,       // The buff IS the cast still going.
                   //
                   // Combat logs record no channel events at all - a channel
                   // arrives as a single instant cast, and the seconds spent
                   // channelling then read as downtime the player never had.
                   // Several specs put a self-buff up for exactly the channel's
                   // duration, and where that happens the buff states what the
                   // log does not.
                   //
                   // Ruled by the user rather than guessed, because which buff
                   // means "still casting" differs per spec and inferring it
                   // from timings would be wrong somewhere unobserved.
};

// Why a suggestion was made, so the review UI can explain itself and the user
// can judge it. A guess the user cannot interrogate is a guess they cannot
// meaningfully confirm.
enum class SuggestionBasis : uint8_t {
    None = 0,
    CadenceMajor,    // Sub-1/min and regular: looks like a long cooldown
    CadenceMinor,    // 1-2/min and regular: looks like a short cooldown
    CadenceFiller,   // High rate, no obvious spacing
    Irregular,       // Used, but with no consistent cadence - situational
    SingleUse,       // Only cast once; cadence says nothing
    AuraSelfBuff,    // Aura the player applied to themselves
    FasterThanGcd,   // Repeats faster than a global; a proc, tick or resource
                     // event the log reports as a cast, not a pressed ability
    AutoAttack,      // Melee swing, synthesized as a cast by the log
};

struct SpellClass {
    uint32_t spellId = 0;
    std::string name;          // Display only
    CastRole role = CastRole::Unclassified;

    // False until the user has explicitly accepted or changed it. The
    // comparison treats unconfirmed entries as unusable rather than guessing.
    bool confirmed = false;

    SuggestionBasis basis = SuggestionBasis::None;
    // 0..1, how well the observed cadence matched the suggested role. Only
    // used to put the weakest guesses first in the review queue; it never
    // skips confirmation.
    float suggestionConfidence = 0.0f;

    // Observed in the log the suggestion came from, shown as evidence.
    float castsPerMinute = 0.0f;
    int32_t observedCasts = 0;
    int32_t medianGapMs = 0;

    // For MajorCooldown: the aura that marks its window, when there is one.
    // Lets the burst/filler split use real buff uptime instead of assuming the
    // window length from the cooldown's tooltip.
    uint32_t windowAuraSpellId = 0;
    int32_t assumedWindowMs = 0;  // Fallback when no aura tracks the window
};

struct AuraClass {
    uint32_t spellId = 0;
    std::string name;
    AuraRole role = AuraRole::Unclassified;
    bool confirmed = false;

    SuggestionBasis basis = SuggestionBasis::None;
    float suggestionConfidence = 0.0f;

    int32_t applications = 0;
    int32_t totalUptimeMs = 0;
    float uptimeShare = 0.0f;

    // For Proc: the spell expected to consume it. Lets "proc expired unused"
    // be detected without hardcoding spec knowledge.
    uint32_t consumedBySpellId = 0;
};

// Everything the user has confirmed for one spec. Keyed by spec so a later log
// for the same spec reuses the work and only asks about genuinely new spells.
struct SpecClassification {
    uint16_t specId = 0;
    std::string specName;        // Display only
    std::string gameVersion;     // Classifications can go stale across patches

    std::unordered_map<uint32_t, SpellClass> spells;
    std::unordered_map<uint32_t, AuraClass> auras;

    // Entries the user has not yet ruled on. The comparison refuses to run
    // while this is non-empty for spells that materially affect it.
    std::vector<uint32_t> pendingSpellIds;
    std::vector<uint32_t> pendingAuraIds;

    bool fullyConfirmed() const {
        return pendingSpellIds.empty() && pendingAuraIds.empty();
    }

    const SpellClass* findSpell(uint32_t id) const {
        auto it = spells.find(id);
        return it != spells.end() ? &it->second : nullptr;
    }
    const AuraClass* findAura(uint32_t id) const {
        auto it = auras.find(id);
        return it != auras.end() ? &it->second : nullptr;
    }

    // Major cooldowns drive the burst/filler split.
    std::vector<uint32_t> majorCooldownSpellIds() const;

    // Auras ruled a major cooldown in their own right, i.e. ones with no cast
    // to hang a window on. Their windows come straight from the buff being up.
    std::vector<uint32_t> buffOnlyCooldownIds() const;

    // Everything that opens a burst window: major cooldowns, and the trinkets
    // stacked into them. A trinket fired inside a cooldown is part of that
    // burst, and one fired outside opens a small burst of its own - either way
    // the damage belongs in the burst half rather than counted as filler.
    // Overlapping windows are merged downstream, so pressing three cooldowns
    // together yields one window running to whichever ends last.
    std::vector<uint32_t> burstWindowSpellIds() const;

    // Spells and auras the user ruled not worth measuring. Everything that
    // reads the profile has to honour these, so they are collected once here
    // rather than each caller remembering to check the role.
    std::vector<uint32_t> ignoredSpellIds() const;
    std::vector<uint32_t> ignoredAuraIds() const;

    // The aura that marks a cooldown's window, or 0 when nothing does.
    //
    // An explicit link wins. Failing that, a cast and a buff sharing an id are
    // the same ability - you press it to enter the window and the window ends
    // when the buff drops - so the pair is used directly. Resolved on demand
    // rather than stored, because the role it depends on can change after the
    // suggestion was made and a cached answer would not follow it.
    uint32_t windowAuraFor(uint32_t spellId) const;

    // Spells ruled Cantrip: cast, but off the global cooldown. They count
    // everywhere except in "globals pressed", which is the one figure they
    // would misrepresent.
    std::vector<uint32_t> offGlobalSpellIds() const;

    // Auras ruled Channel: while one is up the player is still casting, so
    // that time is occupancy rather than downtime.
    std::vector<uint32_t> channelAuraIds() const;

    // Spells ruled Defensive, so their windows can be measured against the
    // damage that actually arrived during them.
    std::vector<uint32_t> defensiveSpellIds() const;
};

// All specs the user has classified. Saved to classifications.json (see
// BenchmarkStore), not to the settings file.
class ClassificationStore {
public:
    static ClassificationStore& instance();

    SpecClassification* find(uint16_t specId);
    const SpecClassification* find(uint16_t specId) const;
    SpecClassification& getOrCreate(uint16_t specId, const std::string& specName);

    // Merge suggestions from a newly loaded log into the stored spec.
    // Confirmed entries are never overwritten, and anything new lands in
    // pending for review.
    // Returns how many new entries need a decision.
    size_t mergeSuggestions(const SpecClassification& suggested);

    // Record the user's decision. Passing the role back in rather than just a
    // flag means accepting a suggestion and correcting one take the same path,
    // so there is no way to mark something confirmed while it still holds a
    // role nobody agreed to.
    void confirmSpell(uint16_t specId, uint32_t spellId, CastRole role);
    void confirmAura(uint16_t specId, uint32_t auraId, AuraRole role);

    // Rule on an aura the suggester never offered.
    //
    // The review list only proposes auras the player applied to themselves,
    // because everything a raid casts on them would bury the ones they
    // control. But those externals still show up in cooldown windows, and a
    // reader who wants one out of the way has to be able to say so - which
    // means creating the entry rather than looking one up and giving up.
    void ruleUnlistedAura(uint16_t specId, uint32_t auraId, AuraRole role);

    // Write the store to disk. Called by the store itself whenever a ruling
    // changes, so no caller can forget to save.
    void save() const;

    // Defers saving until the last guard on the stack goes out of scope, so
    // applying many rulings at once writes the file once.
    class BatchGuard {
    public:
        explicit BatchGuard(ClassificationStore& store);
        ~BatchGuard();
        BatchGuard(const BatchGuard&) = delete;
        BatchGuard& operator=(const BatchGuard&) = delete;
    private:
        ClassificationStore& store_;
    };

    // Accept every outstanding suggestion for a spec as-is. Still an explicit
    // action by the user, so it satisfies the confirmation rule.
    void confirmAllSuggested(uint16_t specId);

    std::string toJson() const;
    void fromJson(const std::string& json);

private:
    ClassificationStore() = default;

    // Refresh the pending lists from the confirmed flags, ordered weakest
    // suggestion first. Called after anything that can change a flag, so the
    // lists never drift out of step with the entries they describe.
    static void rebuildPending(SpecClassification& spec);

    std::unordered_map<uint16_t, SpecClassification> specs_;

    // Non-zero while a batch is open; the pending flag records that something
    // changed so the guard knows whether a write is owed at all.
    int batchDepth_ = 0;
    mutable bool savePending_ = false;
};

} // namespace benchmark
