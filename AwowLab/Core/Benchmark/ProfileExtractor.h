#pragma once

#include <algorithm>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Core/Benchmark/ClassificationSuggester.h"
#include "Core/Benchmark/MentorProfile.h"
#include "Core/Benchmark/SpellClassification.h"
#include "Core/PhaseSettings.h"
#include "Database/EventTypes.h"

namespace benchmark {

// Reduces one player in one pull to a MentorProfile.
//
// Reads ExtractedData rather than the database facades because both the
// WarcraftLogs import and the local-log path produce it, so a profile can be
// built from either source without the extractor knowing which it got. The
// facades are shaped for "rank every actor", and this needs the inverse -
// everything about one actor - which would mean widening their interface for
// a single caller.
//
// What comes out is small enough to keep in memory and to share as a file.
// The reference pull is summarised here and its raw events are dropped, so only
// one encounter is ever fully loaded.

struct ExtractionRequest {
    // The player being profiled. Everything is filtered to this actor.
    std::string actorGuid;
    std::string playerName;
    uint16_t specId = 0;

    // Provenance, carried through so a shared profile can be traced back.
    std::string sourceLabel;      // Report code or file name
    uint32_t encounterId = 0;
    uint32_t difficulty = 0;
    bool kill = false;

    // Phase rules to cut the pull with. Usually PhaseSettings for this
    // encounter; passed in rather than read from the singleton so the
    // extractor stays testable and the caller controls alignment.
    std::vector<PhaseSettings::PhaseRule> phaseRules;

    // Spells the user has confirmed as major cooldowns for this spec. Defines
    // what counts as burst; empty means the whole pull counts as filler.
    std::vector<uint32_t> majorCooldownSpellIds;

    // Everything that opens a burst window. Separate from
    // majorCooldownSpellIds, which is stored on the profile as the fingerprint
    // for "were these numbers cut under the same rulings" - widening that to
    // include trinkets would make every existing profile look stale.
    std::vector<uint32_t> burstWindowSpellIds;

    // Cooldowns that exist only as a buff. Their windows come from the aura
    // itself rather than from a cast, because the log never records one.
    std::vector<uint32_t> buffOnlyCooldownIds;

    // Spells the user ruled Defensive. Their windows are measured separately
    // from burst: the question is not what damage they did but what damage
    // arrived while they were up.
    std::vector<uint32_t> defensiveSpellIds;

    // Auras that mean "still casting". Combat logs record no channel events,
    // so a channel arrives as one instant cast and the seconds spent
    // channelling look like downtime. Where a spec puts a self-buff up for the
    // channel's duration, that buff is the only record of it.
    std::vector<uint32_t> channelAuraIds;

    // Spells the user marked Ignored, and auras they marked Unimportant.
    // Dropped before anything is measured rather than filtered out of each
    // finding afterwards: a ruling of "this does not matter" has to apply to
    // casts per minute, idle time and the burst split too, or the report keeps
    // counting something the user already said to disregard.
    std::vector<uint32_t> ignoredSpellIds;
    std::vector<uint32_t> ignoredAuraIds;

    // Spells that cost no global. They still happened and still did damage, so
    // they stay in the profile - but counting them as globals would credit a
    // player for pressing something that never occupied one, and would hide a
    // real gap in the rotation behind an off-global press.
    std::vector<uint32_t> offGlobalSpellIds;

    // Aura that marks each major cooldown's window, where one exists.
    // Lets the burst split follow the buff actually being up instead of an
    // assumed duration.
    std::unordered_map<uint32_t, uint32_t> cooldownWindowAuras;

    // Fallback window length per cooldown when no aura tracks it.
    std::unordered_map<uint32_t, int32_t> cooldownWindowMs;
};

// Build the profile. Returns an invalid profile (isValid() == false) when the
// actor has no activity in the data, which the caller should surface rather
// than compare against.
MentorProfile extractProfile(const ExtractedData& data,
                             const ExtractionRequest& request);

// Collect what the cadence heuristics need for one actor, so a freshly loaded
// log can be turned into classification suggestions. Separate from profile
// extraction because classification comes first: without confirmed major
// cooldowns there is no burst/filler split to extract.
SuggesterInput buildSuggesterInput(const ExtractedData& data,
                                   const std::string& actorGuid,
                                   uint16_t specId,
                                   const std::string& specName,
                                   int32_t pullDurationMs,
                                   const std::unordered_set<uint32_t>& ignoredSpellIds = {},
                                   const std::unordered_set<uint32_t>& ignoredAuraIds = {});

// How long a cast occupied the player: its global cooldown, or its own cast
// time when that is longer.
//
// The global comes from the event itself, where the loader has already
// resolved it per spell from the shipped spell data and adjusted it for the
// haste in effect at the time. That matters more than it sounds: whether a
// spell is on the global AT ALL is per-spell data, not something derivable
// from timing. An off-global ability reports zero here, and assuming a global
// for it would invent occupancy that never happened.
//
// gcd_ms of 0 is therefore meaningful - it says "this cost no global" - and
// is only replaced with a default when the field was never populated, which
// happens on paths that skip the encounter loader.
//
// In the header so the tests can check it directly: idle time is derived
// from it, and an error here would not show up anywhere else.
inline int32_t castOccupancyMs(const SpellCastEvent* cast,
                               int32_t baseGcdMs = 1500) {
    // Already resolved upstream on paths that ran the encounter loader.
    int32_t gcd = cast->gcd_ms;

    if (gcd <= 0 && baseGcdMs > 0) {
        // Haste shortens the global, but never past the floor - 750ms for a
        // 1.5s base, 500ms for a 1s one. Without the floor a high-haste pull
        // reads as having gaps between presses that never existed.
        gcd = baseGcdMs;
        if (cast->haste_permille > 0) {
            gcd = static_cast<int32_t>(baseGcdMs * 1000.0 /
                                       (1000.0 + cast->haste_permille));
        }
        const int32_t floorMs = (baseGcdMs >= 1500) ? 750 : 500;
        gcd = std::max(gcd, floorMs);
    }

    // A cast longer than the global occupies the player for the cast, not the
    // global - the two overlap rather than adding.
    return std::max(gcd, cast->cast_duration_ms());
}

// Creature id for a target GUID, or 0 for players and unresolvable actors.
//
// Exposed because names live in the log while the profile stores npcIds, and
// joining the two needs exactly this mapping. Reimplementing the GUID parsing
// at the call site would be a second thing to keep in step with the format.
uint32_t resolveNpcIdForGuid(std::string_view guid,
                             const std::unordered_map<int32_t, uint32_t>& actorIdToNpcId);

// When the player died, or -1 if they survived the pull. This is the cutoff
// the comparison truncates the reference at.
int32_t findDeathTime(const ExtractedData& data, const std::string& actorGuid);

} // namespace benchmark
