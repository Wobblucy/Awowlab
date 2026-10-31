#include "Core/Benchmark/BenchmarkEngine.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace benchmark {

namespace {

// A finding has to clear both a ratio and an absolute floor. Ratios alone turn
// trivia into headlines: missing 3 casts of something cast 4 times is a 25%
// shortfall and almost never the reason a pull failed.
constexpr int64_t kMinDamageDeltaToReport = 100000;
constexpr int32_t kMinCastsToCompare = 3;

// How far apart two nth-uses can be before the difference is reported. Below
// this, both pressed it at effectively the same point in the fight.
constexpr int32_t kTimingNoiseMs = 1500;

FindingWeight weighDamage(int64_t delta, int64_t totalDelta) {
    const int64_t magnitude = std::abs(delta);
    if (totalDelta > 0) {
        const double share = static_cast<double>(magnitude) / totalDelta;
        if (share >= 0.35) return FindingWeight::Major;
        if (share >= 0.15) return FindingWeight::Notable;
    }
    if (magnitude >= 2000000) return FindingWeight::Major;
    if (magnitude >= 500000)  return FindingWeight::Notable;
    return FindingWeight::Info;
}

FindingWeight weighDelay(int32_t delayMs) {
    if (delayMs >= 15000) return FindingWeight::Major;
    if (delayMs >= 7000)  return FindingWeight::Notable;
    return FindingWeight::Info;
}

// Cooldown uses grouped by spell, in press order, so nth can be lined up
// against nth.
//
// The result points into the vector passed in, so it must outlive the group.
// The rvalue overload below is deleted to make binding a temporary here a
// compile error rather than a dangling read that yields plausible nonsense.
std::unordered_map<uint32_t, std::vector<const ProfileCooldownUse*>>
groupBySpell(const std::vector<ProfileCooldownUse>&& uses) = delete;

std::unordered_map<uint32_t, std::vector<const ProfileCooldownUse*>>
groupBySpell(const std::vector<ProfileCooldownUse>& uses) {
    std::unordered_map<uint32_t, std::vector<const ProfileCooldownUse*>> grouped;
    for (const auto& use : uses) {
        grouped[use.spellId].push_back(&use);
    }
    for (auto& [spellId, list] : grouped) {
        std::sort(list.begin(), list.end(),
                  [](const ProfileCooldownUse* a, const ProfileCooldownUse* b) {
                      return a->useIndex < b->useIndex;
                  });
    }
    return grouped;
}

// Per-spell and per-target totals over [0, cutoffMs), used where the timeline
// does not carry the breakdown. Phases are scaled by how much of them the
// cutoff covers, the same way windowUpTo cuts the burst/filler split, so these
// cover the same stretch as the durations they are divided by.
struct SpellTotals {
    std::unordered_map<uint32_t, double> castsBySpell;
    std::unordered_map<uint32_t, ProfileDamageTaken> takenBySpell;
    std::unordered_map<uint32_t, ProfileDamageDone> doneByNpc;
    int64_t totalDamageDone = 0;
};

SpellTotals collectTotals(const MentorProfile& profile, int32_t cutoffMs) {
    SpellTotals totals;
    int32_t phaseStart = 0;
    for (const auto& phase : profile.phases) {
        if (phaseStart >= cutoffMs) break;

        const int32_t phaseEnd = phaseStart + phase.durationMs;
        const int32_t covered = std::min(phaseEnd, cutoffMs) - phaseStart;
        phaseStart = phaseEnd;
        if (covered <= 0 || phase.durationMs <= 0) continue;

        const double share = static_cast<double>(covered) / phase.durationMs;

        for (const auto& spell : phase.spells) {
            totals.castsBySpell[spell.spellId] += spell.casts * share;
        }
        for (const auto& taken : phase.damageTaken) {
            auto& entry = totals.takenBySpell[taken.spellId];
            entry.spellId = taken.spellId;
            entry.hits += static_cast<int32_t>(taken.hits * share);
            entry.total += static_cast<int64_t>(taken.total * share);
        }
        for (const auto& done : phase.damageDone) {
            auto& entry = totals.doneByNpc[done.npcId];
            entry.npcId = done.npcId;
            entry.total += static_cast<int64_t>(done.total * share);
            entry.casts += static_cast<int32_t>(done.casts * share);
        }
        totals.totalDamageDone += static_cast<int64_t>(phase.totalDamageDone * share);
    }
    return totals;
}

bool rulesMatch(const std::vector<PhaseSettings::PhaseRule>& a,
                const std::vector<PhaseSettings::PhaseRule>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!a[i].matches(b[i])) return false;
    }
    return true;
}

} // namespace

BlockReason checkComparable(const ComparisonRequest& request) {
    if (!request.user || !request.mentor) return BlockReason::MissingProfile;
    if (!request.user->isValid() || !request.mentor->isValid()) {
        return BlockReason::MissingProfile;
    }

    if (request.user->encounterId != request.mentor->encounterId) {
        return BlockReason::EncounterMismatch;
    }

    // Two specs press different buttons for different reasons. A cross-spec
    // diff would be arithmetic without meaning.
    if (request.user->specId != 0 && request.mentor->specId != 0 &&
        request.user->specId != request.mentor->specId) {
        return BlockReason::SpecMismatch;
    }

    if (!rulesMatch(request.user->phaseRules, request.mentor->phaseRules)) {
        return BlockReason::PhaseRulesChanged;
    }

    // The classification is what gave "burst" and "filler" their meaning when
    // these profiles were built. Comparing while it is still in flux would
    // measure the two sides against different definitions.
    if (!request.classification || !request.classification->fullyConfirmed()) {
        return BlockReason::UnconfirmedSpells;
    }

    const int32_t cutoff = request.userDeathMs > 0 ? request.userDeathMs
                                                   : request.user->pullDurationMs;
    if (cutoff <= 0) return BlockReason::NoOverlap;

    return BlockReason::None;
}

BenchmarkResult compare(const ComparisonRequest& request) {
    BenchmarkResult result;

    const BlockReason block = checkComparable(request);
    if (block != BlockReason::None) {
        result.valid = false;
        switch (block) {
            case BlockReason::MissingProfile:    result.blockingReason = "missing_profile"; break;
            case BlockReason::SpecMismatch:      result.blockingReason = "spec_mismatch"; break;
            case BlockReason::EncounterMismatch: result.blockingReason = "encounter_mismatch"; break;
            case BlockReason::PhaseRulesChanged: result.blockingReason = "phase_rules_changed"; break;
            case BlockReason::UnconfirmedSpells: result.blockingReason = "unconfirmed_spells"; break;
            case BlockReason::NoOverlap:         result.blockingReason = "no_overlap"; break;
            default: break;
        }
        if (block == BlockReason::UnconfirmedSpells && request.classification) {
            result.unclassifiedSpellIds = request.classification->pendingSpellIds;
            result.unclassifiedAuraIds = request.classification->pendingAuraIds;
        }
        return result;
    }

    const MentorProfile& user = *request.user;
    const MentorProfile& mentor = *request.mentor;

    // Both sides are measured over the same stretch, otherwise the longer
    // pull would simply show more damage.
    const int32_t cutoff = request.userDeathMs > 0 ? request.userDeathMs
                                                   : user.pullDurationMs;

    const auto userWindow = user.windowUpTo(cutoff);
    const auto mentorWindow = mentor.windowUpTo(cutoff);

    result.valid = true;
    result.summary.comparedDurationMs = std::min(userWindow.durationMs,
                                                 mentorWindow.durationMs);
    result.summary.userDied = request.userDeathMs > 0;
    result.summary.userDeathMs = request.userDeathMs;

    // Which side ran out first. A margin avoids reporting a cap for two pulls
    // that ended within a second of each other, which is not a difference
    // anyone needs told about.
    constexpr int32_t kCapMarginMs = 1000;
    result.summary.cappedByMentor =
        mentorWindow.durationMs + kCapMarginMs < userWindow.durationMs;

    result.summary.userDps = userWindow.dps();
    result.summary.mentorDps = mentorWindow.dps();
    result.summary.dpsDelta = result.summary.userDps - result.summary.mentorDps;

    result.summary.userCastsPerMinute = userWindow.castsPerMinute();
    result.summary.mentorCastsPerMinute = mentorWindow.castsPerMinute();
    result.summary.castsPerMinuteDelta =
        result.summary.userCastsPerMinute - result.summary.mentorCastsPerMinute;

    result.summary.userDamageTaken = userWindow.damageTaken;
    result.summary.mentorDamageTaken = mentorWindow.damageTaken;

    result.summary.userBurstDps = userWindow.inCooldowns.dps();
    result.summary.mentorBurstDps = mentorWindow.inCooldowns.dps();
    result.summary.userFillerDps = userWindow.outsideCooldowns.dps();
    result.summary.mentorFillerDps = mentorWindow.outsideCooldowns.dps();

    result.summary.burstDamageDelta =
        userWindow.inCooldowns.damage - mentorWindow.inCooldowns.damage;
    result.summary.fillerDamageDelta =
        userWindow.outsideCooldowns.damage - mentorWindow.outsideCooldowns.damage;

    const int64_t totalShortfall =
        std::abs(result.summary.burstDamageDelta) +
        std::abs(result.summary.fillerDamageDelta);

    // Cooldowns: nth use against nth use
    //
    // Timing and output are separate facts and get separate findings. A
    // cooldown pressed late that still produced full value is a timing
    // difference, not a damage loss, and saying otherwise would be wrong
    // twice over.
    // Held by value: groupBySpell stores pointers into these, so the vectors
    // have to outlive the loop below rather than dying with the call.
    const std::vector<ProfileCooldownUse> userUseList = user.cooldownsUpTo(cutoff);
    const std::vector<ProfileCooldownUse> mentorUseList = mentor.cooldownsUpTo(cutoff);

    const auto userUses = groupBySpell(userUseList);
    const auto mentorUses = groupBySpell(mentorUseList);

    for (const auto& [spellId, mentorList] : mentorUses) {
        auto userIt = userUses.find(spellId);

        if (userIt == userUses.end()) {
            Finding finding;
            finding.kind = FindingKind::CooldownUnused;
            finding.weight = FindingWeight::Major;
            finding.spellId = spellId;
            finding.pullOffsetMs = mentorList.front()->pullOffsetMs;
            finding.userValue = 0.0;
            finding.mentorValue = static_cast<double>(mentorList.size());
            result.overall.push_back(std::move(finding));
            continue;
        }

        const auto& userList = userIt->second;

        if (userList.size() < mentorList.size()) {
            Finding finding;
            finding.kind = FindingKind::CooldownFewerUses;
            finding.weight = FindingWeight::Notable;
            finding.spellId = spellId;
            finding.pullOffsetMs = mentorList[userList.size()]->pullOffsetMs;
            finding.userValue = static_cast<double>(userList.size());
            finding.mentorValue = static_cast<double>(mentorList.size());
            result.overall.push_back(std::move(finding));
        }

        const size_t pairs = std::min(userList.size(), mentorList.size());
        for (size_t i = 0; i < pairs; ++i) {
            const ProfileCooldownUse* u = userList[i];
            const ProfileCooldownUse* m = mentorList[i];

            const int32_t delay = u->pullOffsetMs - m->pullOffsetMs;
            if (std::abs(delay) >= request.thresholds.cooldownDelayMs &&
                std::abs(delay) > kTimingNoiseMs) {
                Finding finding;
                finding.kind = FindingKind::CooldownDelayed;
                finding.weight = weighDelay(std::abs(delay));
                finding.spellId = spellId;
                finding.pullOffsetMs = u->pullOffsetMs;
                finding.userValue = u->pullOffsetMs;
                finding.mentorValue = m->pullOffsetMs;
                result.overall.push_back(std::move(finding));
            }

            if (m->damageInWindow > 0) {
                const double ratio = static_cast<double>(u->damageInWindow) /
                                     m->damageInWindow;
                const int64_t delta = u->damageInWindow - m->damageInWindow;
                if (ratio < request.thresholds.cooldownWindowRatio &&
                    std::abs(delta) >= kMinDamageDeltaToReport) {
                    Finding finding;
                    finding.kind = FindingKind::CooldownWindowWeaker;
                    finding.weight = weighDamage(delta, totalShortfall);
                    finding.spellId = spellId;
                    finding.pullOffsetMs = u->pullOffsetMs;
                    finding.userValue = static_cast<double>(u->damageInWindow);
                    finding.mentorValue = static_cast<double>(m->damageInWindow);
                    result.overall.push_back(std::move(finding));
                }
            }
        }
    }

    // Filler throughput
    //
    // Most of a pull happens outside cooldowns, so this usually holds the
    // larger absolute loss even though each individual mistake costs less.
    if (mentorWindow.outsideCooldowns.dps() > 0.0) {
        const double ratio = userWindow.outsideCooldowns.dps() /
                             mentorWindow.outsideCooldowns.dps();
        if (ratio < request.thresholds.fillerDpsRatio &&
            std::abs(result.summary.fillerDamageDelta) >= kMinDamageDeltaToReport) {
            Finding finding;
            finding.kind = FindingKind::FillerDpsLower;
            finding.weight = weighDamage(result.summary.fillerDamageDelta, totalShortfall);
            finding.userValue = userWindow.outsideCooldowns.dps();
            finding.mentorValue = mentorWindow.outsideCooldowns.dps();
            result.overall.push_back(std::move(finding));
        }
    }

    const double idleDelta = userWindow.outsideCooldowns.idleShare() -
                             mentorWindow.outsideCooldowns.idleShare();
    if (idleDelta >= request.thresholds.fillerIdleShareDelta) {
        Finding finding;
        finding.kind = FindingKind::FillerIdleHigher;
        finding.weight = idleDelta >= 0.15 ? FindingWeight::Notable : FindingWeight::Info;
        finding.userValue = userWindow.outsideCooldowns.idleShare();
        finding.mentorValue = mentorWindow.outsideCooldowns.idleShare();
        result.overall.push_back(std::move(finding));
    }

    // Cast rates
    const SpellTotals userTotals = collectTotals(user, cutoff);
    const SpellTotals mentorTotals = collectTotals(mentor, cutoff);

    const double comparedMinutes = result.summary.comparedDurationMs / 60000.0;
    if (comparedMinutes > 0.0) {
        for (const auto& [spellId, mentorCasts] : mentorTotals.castsBySpell) {
            if (mentorCasts < kMinCastsToCompare) continue;

            double userCasts = 0.0;
            if (auto it = userTotals.castsBySpell.find(spellId);
                it != userTotals.castsBySpell.end()) {
                userCasts = it->second;
            }

            const double userRate = userCasts / comparedMinutes;
            const double mentorRate = mentorCasts / comparedMinutes;
            const double delta = mentorRate - userRate;

            if (delta >= request.thresholds.castRateDeltaPerMin) {
                // Cooldowns already have their own, more specific findings;
                // repeating them here as a rate would say the same thing twice
                // in less useful terms.
                const auto* cls = request.classification->findSpell(spellId);
                if (cls && (cls->role == CastRole::MajorCooldown ||
                            cls->role == CastRole::MinorCooldown ||
                            cls->role == CastRole::Trinket)) {
                    continue;
                }
                // Ignored spells never reach the profile at all now, so only
                // Utility needs excluding here - its use follows what the
                // fight demanded of each player, which the log cannot see.
                if (cls && cls->role == CastRole::Utility) {
                    continue;
                }
                // Auto-attack swings on its own. Reporting one player as
                // pressing it less than another would be reporting a haste
                // difference as a play difference.
                if (cls && cls->role == CastRole::Melee) {
                    continue;
                }

                Finding finding;
                finding.kind = FindingKind::CastRateLower;
                finding.weight = delta >= 5.0 ? FindingWeight::Notable : FindingWeight::Info;
                finding.spellId = spellId;
                finding.userValue = userRate;
                finding.mentorValue = mentorRate;
                result.overall.push_back(std::move(finding));
            }
        }
    }

    // Damage taken
    //
    // Reported as a difference, never as a mistake. The log cannot see who was
    // targeted, who was assigned where, or which soak the user was covering.
    for (const auto& [spellId, userTaken] : userTotals.takenBySpell) {
        int64_t mentorAmount = 0;
        if (auto it = mentorTotals.takenBySpell.find(spellId);
            it != mentorTotals.takenBySpell.end()) {
            mentorAmount = it->second.total;
        }

        if (userTaken.total < kMinDamageDeltaToReport) continue;

        const bool tookMore =
            mentorAmount == 0 ||
            static_cast<double>(userTaken.total) / mentorAmount >=
                request.thresholds.damageTakenRatio;

        if (tookMore) {
            Finding finding;
            finding.kind = FindingKind::DamageTakenHigher;
            finding.weight = userTaken.total >= 3000000 ? FindingWeight::Notable
                                                        : FindingWeight::Info;
            finding.spellId = spellId;
            finding.userValue = static_cast<double>(userTaken.total);
            finding.mentorValue = static_cast<double>(mentorAmount);
            result.overall.push_back(std::move(finding));
        }
    }

    // Target selection
    if (userTotals.totalDamageDone > 0 && mentorTotals.totalDamageDone > 0) {
        std::unordered_set<uint32_t> npcIds;
        for (const auto& [npcId, done] : userTotals.doneByNpc) npcIds.insert(npcId);
        for (const auto& [npcId, done] : mentorTotals.doneByNpc) npcIds.insert(npcId);

        for (uint32_t npcId : npcIds) {
            if (npcId == 0) continue;

            int64_t userAmount = 0;
            int64_t mentorAmount = 0;
            if (auto it = userTotals.doneByNpc.find(npcId);
                it != userTotals.doneByNpc.end()) {
                userAmount = it->second.total;
            }
            if (auto it = mentorTotals.doneByNpc.find(npcId);
                it != mentorTotals.doneByNpc.end()) {
                mentorAmount = it->second.total;
            }

            const double userShare =
                static_cast<double>(userAmount) / userTotals.totalDamageDone;
            const double mentorShare =
                static_cast<double>(mentorAmount) / mentorTotals.totalDamageDone;

            if (std::abs(userShare - mentorShare) >=
                request.thresholds.targetSelectionShareDelta) {
                Finding finding;
                finding.kind = FindingKind::TargetSelection;
                finding.weight = FindingWeight::Info;
                finding.npcId = npcId;
                finding.userValue = userShare;
                finding.mentorValue = mentorShare;
                result.overall.push_back(std::move(finding));
            }
        }
    }

    // Phase windows
    const size_t phaseCount = std::min(user.phases.size(), mentor.phases.size());
    for (size_t i = 0; i < phaseCount; ++i) {
        const auto& userPhase = user.phases[i];
        const auto& mentorPhase = mentor.phases[i];

        PhaseComparison comparison;
        comparison.label = mentorPhase.label.empty() ? userPhase.label : mentorPhase.label;
        comparison.userDurationMs = userPhase.durationMs;
        comparison.mentorDurationMs = mentorPhase.durationMs;
        comparison.userDps = userPhase.durationMs > 0
            ? static_cast<double>(userPhase.totalDamageDone) * 1000.0 / userPhase.durationMs
            : 0.0;
        comparison.mentorDps = mentorPhase.durationMs > 0
            ? static_cast<double>(mentorPhase.totalDamageDone) * 1000.0 / mentorPhase.durationMs
            : 0.0;

        // A boundary that never fired in one of the pulls leaves windows that
        // do not cover the same stretch of fight, so they are flagged rather
        // than diffed.
        const bool unresolvedHere =
            std::find(mentor.unresolvedPhaseLabels.begin(),
                      mentor.unresolvedPhaseLabels.end(),
                      comparison.label) != mentor.unresolvedPhaseLabels.end() ||
            std::find(user.unresolvedPhaseLabels.begin(),
                      user.unresolvedPhaseLabels.end(),
                      comparison.label) != user.unresolvedPhaseLabels.end();

        if (unresolvedHere) {
            comparison.comparable = false;
            comparison.uncomparableReason = "phase_not_reached";
        }

        result.phases.push_back(std::move(comparison));
    }

    // Heaviest first. A report read top-down should open on the thing most
    // likely to have changed the outcome.
    std::sort(result.overall.begin(), result.overall.end(),
              [](const Finding& a, const Finding& b) {
                  if (a.weight != b.weight) return a.weight > b.weight;
                  return a.pullOffsetMs < b.pullOffsetMs;
              });

    return result;
}

} // namespace benchmark
