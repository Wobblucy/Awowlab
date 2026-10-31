#include "Core/Benchmark/ProfileExtractor.h"
#include "Core/UnifiedSettings.h"
#include "Core/Utils/ExecutablePath.h"
#include "Database/SpellDataTable.h"

#include "Core/MobWeightSettings.h"
#include "Core/PhaseResolver.h"
#include "Core/StringInterner.h"

#include <algorithm>
#include <charconv>
#include <memory>
#include <unordered_set>

namespace benchmark {

namespace {

constexpr int32_t kBucketMs = 5000;

// A cooldown with no aura marking its window and no known duration still needs
// one to measure output in. Twenty seconds covers the common case without
// swallowing the filler around it.
constexpr int32_t kDefaultCooldownWindowMs = 20000;

// Beyond this gap between casts the player is moving, waiting on a mechanic or
// dead, so gaps are only charged as dropped globals up to this much.
constexpr int32_t kMaxChargeableGapMs = 5000;

// Below this, the gap is a global cooldown doing its job rather than idle time.
constexpr int32_t kMinIdleGapMs = 1600;

// Base global cooldown for a spell, from the shipped spell data. Whether a
// spell is on the global at all cannot be inferred from timings. The benchmark
// paths stop before EncounterDataLoader builds its databases, so the table is
// loaded here once and reused.
//
// Falls back to the standard global when the table is unavailable, which is
// closer than assuming casts cost nothing.
constexpr int32_t kStandardGcdMs = 1500;
// Haste cannot push a global below this
constexpr int32_t kMinGcdMs = 750;

int32_t baseGcdFor(uint32_t spellId) {
    static const std::unique_ptr<awow::SpellDataTable> table = [] {
        auto loaded = std::make_unique<awow::SpellDataTable>();
        const std::string version =
            SettingsCache::instance().get().spellDataVersion;
        const std::string path = awow::getExecutableDirectory() +
                                 "data\\spell_data_" + version + ".bin";
        if (!loaded->loadFromBinary(path)) loaded.reset();
        return loaded;
    }();

    if (!table) return kStandardGcdMs;
    return table->getBaseGCD(spellId);
}

// Damage credited for one hit after the user's weighting for that target (a
// weight of 0.2 counts a fifth of it). Every damage figure goes through here so
// the report's tabs agree with each other.
int64_t weightedAmount(int64_t amount, uint32_t npcId) {
    const float weight = MobWeightSettings::instance().weightFor(npcId);
    if (weight >= 0.999f) return amount;
    return static_cast<int64_t>(amount * weight);
}

// Resolve a target GUID to a creature id. WarcraftLogs replays carry synthetic
// GUIDs built from the report's own actor index, so the real creature id has
// to come from the side map; real combat logs hold it in the GUID itself.
uint32_t resolveNpcId(std::string_view guid,
                      const std::unordered_map<int32_t, uint32_t>& actorIdToNpcId) {
    if (guid.empty() || guid.starts_with("Player-")) return 0;

    if (!actorIdToNpcId.empty()) {
        // Synthetic layout: Creature-0-0-0-0-<actorId>-<spawn>
        size_t pos = 0;
        for (int field = 0; field < 5; ++field) {
            pos = guid.find('-', pos);
            if (pos == std::string_view::npos) break;
            ++pos;
        }
        if (pos != std::string_view::npos && pos < guid.size()) {
            size_t end = guid.find('-', pos);
            if (end == std::string_view::npos) end = guid.size();
            int32_t actorId = 0;
            std::from_chars(guid.data() + pos, guid.data() + end, actorId);
            if (auto it = actorIdToNpcId.find(actorId); it != actorIdToNpcId.end()) {
                return it->second;
            }
        }
    }

    return MobWeightSettings::npcIdFromGuid(guid);
}

// Windows during which a major cooldown was active. Overlapping windows are
// merged so shared time is not counted twice.
struct ActiveWindow {
    int32_t startMs = 0;
    int32_t endMs = 0;
};

std::vector<ActiveWindow> mergeWindows(std::vector<ActiveWindow> windows) {
    if (windows.empty()) return windows;

    std::sort(windows.begin(), windows.end(),
              [](const ActiveWindow& a, const ActiveWindow& b) {
                  return a.startMs < b.startMs;
              });

    std::vector<ActiveWindow> merged;
    merged.push_back(windows.front());
    for (size_t i = 1; i < windows.size(); ++i) {
        if (windows[i].startMs <= merged.back().endMs) {
            merged.back().endMs = std::max(merged.back().endMs, windows[i].endMs);
        } else {
            merged.push_back(windows[i]);
        }
    }
    return merged;
}

bool insideAnyWindow(const std::vector<ActiveWindow>& windows, int32_t timeMs) {
    for (const auto& w : windows) {
        if (timeMs >= w.startMs && timeMs < w.endMs) return true;
    }
    return false;
}

// How much of [start, end) falls inside the merged windows.
int32_t overlapWithWindows(const std::vector<ActiveWindow>& windows,
                           int32_t start, int32_t end) {
    int32_t total = 0;
    for (const auto& w : windows) {
        const int32_t lo = std::max(start, w.startMs);
        const int32_t hi = std::min(end, w.endMs);
        if (hi > lo) total += hi - lo;
    }
    return total;
}

} // namespace

int32_t findDeathTime(const ExtractedData& data, const std::string& actorGuid) {
    int32_t earliest = -1;
    for (const auto& death : data.deathEvents) {
        if (death.actor_guid != actorGuid) continue;
        // The first death ends the comparable window, even if the player is
        // battle-ressed later.
        if (earliest < 0 || death.timestamp_ms < earliest) {
            earliest = death.timestamp_ms;
        }
    }
    return earliest;
}

SuggesterInput buildSuggesterInput(const ExtractedData& data,
                                   const std::string& actorGuid,
                                   uint16_t specId,
                                   const std::string& specName,
                                   int32_t pullDurationMs,
                                   const std::unordered_set<uint32_t>& ignoredSpellIds,
                                   const std::unordered_set<uint32_t>& ignoredAuraIds) {
    SuggesterInput input;
    input.specId = specId;
    input.specName = specName;
    input.pullDurationMs = pullDurationMs;

    for (const auto& cast : data.spellCastEvents) {
        if (cast.actor_guid != actorGuid) continue;
        if (cast.cancelled) continue;   // Started but never went out
        // Already-ignored spells are left out so they neither return to the
        // review queue nor skew the cadence bands for everything else.
        if (ignoredSpellIds.count(cast.spell_id)) continue;
        input.casts.push_back({cast.spell_id, cast.end_timestamp_ms});

        // The log's own cooldown value, where known. Cadence alone cannot tell
        // a long cooldown from a spell that was only cast twice.
        if (cast.cooldown_ms > 0) {
            input.cooldownMs[cast.spell_id] = cast.cooldown_ms;
        }
    }

    // Pair aura applications with their removals so each becomes an interval.
    // Whether the player applied it themselves is recorded; buffs from other
    // players are outside their control.
    struct OpenAura {
        int32_t appliedMs = 0;
        bool selfApplied = false;
    };
    std::unordered_map<uint32_t, OpenAura> open;

    for (const auto& aura : data.auraEvents) {
        if (aura.target_guid != actorGuid) continue;
        if (ignoredAuraIds.count(aura.spell_id)) continue;

        const bool self = (aura.source_guid == actorGuid);
        switch (aura.event_type) {
            case AuraEventType::Applied:
            case AuraEventType::Refresh:
                if (!open.count(aura.spell_id)) {
                    open[aura.spell_id] = {aura.timestamp_ms, self};
                }
                break;
            case AuraEventType::Removed:
            case AuraEventType::Broken:
            case AuraEventType::BrokenSpell: {
                auto it = open.find(aura.spell_id);
                if (it != open.end()) {
                    input.auras.push_back({aura.spell_id, it->second.appliedMs,
                                           aura.timestamp_ms, it->second.selfApplied});
                    open.erase(it);
                }
                break;
            }
            default:
                break;
        }
    }
    // Auras still up when the pull ended count up to the end of the pull.
    for (const auto& [spellId, aura] : open) {
        input.auras.push_back({spellId, aura.appliedMs, pullDurationMs, aura.selfApplied});
    }

    input.spellNames = data.spellNameMap;
    return input;
}

uint32_t resolveNpcIdForGuid(std::string_view guid,
                             const std::unordered_map<int32_t, uint32_t>& actorIdToNpcId) {
    return resolveNpcId(guid, actorIdToNpcId);
}

MentorProfile extractProfile(const ExtractedData& data,
                             const ExtractionRequest& request) {
    MentorProfile profile;
    profile.sourceLabel = request.sourceLabel;
    profile.playerName = request.playerName;
    profile.actorGuid = request.actorGuid;
    profile.encounterId = request.encounterId;
    profile.specId = request.specId;
    profile.difficulty = request.difficulty;
    profile.kill = request.kill;
    profile.phaseRules = request.phaseRules;
    profile.majorCooldownSpellIds = request.majorCooldownSpellIds;
    profile.ignoredSpellIds = request.ignoredSpellIds;
    profile.offGlobalSpellIds = request.offGlobalSpellIds;
    profile.ignoredAuraIds = request.ignoredAuraIds;
    profile.mobWeightFingerprint = MobWeightSettings::instance().fingerprint();
    profile.burstWindowSpellIds = request.burstWindowSpellIds;
    profile.buffOnlyCooldownIds = request.buffOnlyCooldownIds;
    profile.bucketMs = kBucketMs;

    if (request.actorGuid.empty()) return profile;

    // A guid that was never interned appears in no event. Carrying on would
    // match INVALID (0) against owner_guid_id, which is 0 on every event that
    // has no owner, and credit this actor with the whole log's damage.
    if (guidInterner().find(request.actorGuid) == StringInterner::INVALID) return profile;

    // Pull length is taken from the events themselves so the profile matches
    // the data it was built from.
    int32_t pullEnd = 0;
    for (const auto& cast : data.spellCastEvents) {
        pullEnd = std::max(pullEnd, cast.end_timestamp_ms);
    }
    for (const auto& combat : data.combatEvents) {
        pullEnd = std::max(pullEnd, combat.timestamp_ms);
    }
    profile.pullDurationMs = pullEnd;
    if (pullEnd <= 0) return profile;

    // This actor's casts, ordered. Spells the user ruled Ignored are dropped
    // here so the ruling reaches every figure built from casts.
    const std::unordered_set<uint32_t> ignoredSpells(request.ignoredSpellIds.begin(),
                                                     request.ignoredSpellIds.end());
    const std::unordered_set<uint32_t> ignoredAuras(request.ignoredAuraIds.begin(),
                                                    request.ignoredAuraIds.end());

    std::vector<const SpellCastEvent*> casts;
    for (const auto& cast : data.spellCastEvents) {
        if (cast.actor_guid != request.actorGuid) continue;
        if (cast.cancelled) continue;
        if (ignoredSpells.count(cast.spell_id)) continue;
        casts.push_back(&cast);
    }
    std::sort(casts.begin(), casts.end(),
              [](const SpellCastEvent* a, const SpellCastEvent* b) {
                  return a->end_timestamp_ms < b->end_timestamp_ms;
              });

    // The subset that occupied a global. The full list is kept too: off-global
    // casts still deal damage and belong to their cooldown window, but they do
    // not count as globals and cannot close a gap in the rotation.
    const std::unordered_set<uint32_t> offGlobalSpells(request.offGlobalSpellIds.begin(),
                                                       request.offGlobalSpellIds.end());
    std::vector<const SpellCastEvent*> globalCasts;
    globalCasts.reserve(casts.size());
    for (const SpellCastEvent* cast : casts) {
        if (offGlobalSpells.count(cast->spell_id)) continue;
        globalCasts.push_back(cast);
    }

    // Burst windows. Burst is whatever the user confirmed as a major cooldown;
    // with nothing confirmed the whole pull counts as filler rather than
    // guessing. Falls back to the major cooldowns when no wider list was
    // supplied.
    const auto& windowSource = request.burstWindowSpellIds.empty()
        ? request.majorCooldownSpellIds
        : request.burstWindowSpellIds;
    const std::unordered_set<uint32_t> majorIds(windowSource.begin(),
                                                windowSource.end());

    std::vector<ActiveWindow> burstWindows;
    std::vector<ProfileCooldownUse> uses;
    std::unordered_map<uint32_t, int32_t> useCounter;

    // Cooldowns whose window comes from a buff are handled below from the buff
    // itself. Opening a window here as well would count them twice, and would
    // open one window per proc for anything logged as a proc.
    const std::unordered_set<uint32_t> buffDriven(request.buffOnlyCooldownIds.begin(),
                                                  request.buffOnlyCooldownIds.end());

    for (const SpellCastEvent* cast : casts) {
        if (!majorIds.count(cast->spell_id)) continue;
        if (buffDriven.count(cast->spell_id)) continue;

        // Skip casts from before the pull timer started rather than clamping
        // them to zero, which would make their windows open on pull.
        if (cast->end_timestamp_ms < 0) continue;
        const int32_t start = cast->end_timestamp_ms;
        int32_t windowMs = kDefaultCooldownWindowMs;

        // Prefer the real buff duration, so a cooldown extended by a proc or
        // cut short by a death measures what actually happened.
        uint32_t windowAura = 0;
        if (auto it = request.cooldownWindowAuras.find(cast->spell_id);
            it != request.cooldownWindowAuras.end()) {
            windowAura = it->second;
        }
        bool resolvedFromAura = false;
        if (windowAura != 0) {
            for (const auto& aura : data.auraEvents) {
                if (aura.spell_id != windowAura) continue;
                if (aura.target_guid != request.actorGuid) continue;
                if (aura.event_type != AuraEventType::Removed) continue;
                if (aura.timestamp_ms <= start) continue;
                windowMs = aura.timestamp_ms - start;
                resolvedFromAura = true;
                break;
            }
        }
        if (!resolvedFromAura) {
            if (auto it = request.cooldownWindowMs.find(cast->spell_id);
                it != request.cooldownWindowMs.end() && it->second > 0) {
                windowMs = it->second;
            }
        }

        ProfileCooldownUse use;
        use.spellId = cast->spell_id;
        use.useIndex = ++useCounter[cast->spell_id];
        use.pullOffsetMs = start;
        use.windowMs = windowMs;
        uses.push_back(use);

        burstWindows.push_back({start, start + windowMs});
    }

    // Windows taken from the cooldown buffs themselves. The buff says directly
    // when the cooldown was up; deriving it from casts would mean guessing a
    // duration, and a short proc firing twenty times would open twenty
    // windows.
    //
    // Overlapping cooldown buffs form one window (a cooldown, then a trinket,
    // then a potion is one burst), which ends when the last of them drops.
    if (!request.buffOnlyCooldownIds.empty()) {
        const std::unordered_set<uint32_t> windowBuffs(
            request.buffOnlyCooldownIds.begin(), request.buffOnlyCooldownIds.end());

        struct BuffEdge {
            int32_t timeMs = 0;
            bool opening = false;
            uint32_t spellId = 0;
        };
        std::vector<BuffEdge> edges;
        std::unordered_map<uint32_t, int32_t> openAt;

        for (const auto& aura : data.auraEvents) {
            if (!windowBuffs.count(aura.spell_id)) continue;
            if (aura.target_guid != request.actorGuid) continue;

            // Only cooldowns the player pressed themselves open a window, not
            // raid buffs or externals cast on them. An empty source is kept
            // because some logs omit it for self-buffs.
            if (!aura.source_guid.empty() && aura.source_guid != request.actorGuid) {
                continue;
            }

            switch (aura.event_type) {
                case AuraEventType::Applied:
                case AuraEventType::Refresh: {
                    // A refresh on an open window does not start a second
                    // one. A refresh with no open window means the buff was
                    // already up at pull, so it opens one (the interval pass
                    // below does the same). Pre-pull timestamps clamp to the
                    // pull start.
                    if (!openAt.count(aura.spell_id)) {
                        const int32_t start = std::max(aura.timestamp_ms, 0);
                        openAt[aura.spell_id] = start;
                        edges.push_back({start, true, aura.spell_id});
                    }
                    break;
                }
                case AuraEventType::Removed:
                case AuraEventType::Broken:
                case AuraEventType::BrokenSpell:
                    if (openAt.erase(aura.spell_id) > 0) {
                        edges.push_back({aura.timestamp_ms, false, aura.spell_id});
                    }
                    break;
                default:
                    break;
            }
        }

        // Anything still up when the pull ended stayed up to the end.
        for (const auto& [spellId, appliedAt] : openAt) {
            edges.push_back({pullEnd, false, spellId});
        }

        std::sort(edges.begin(), edges.end(),
                  [](const BuffEdge& a, const BuffEdge& b) {
                      if (a.timeMs != b.timeMs) return a.timeMs < b.timeMs;
                      // Openings first at the same instant, so a cooldown
                      // chained straight into another stays one window.
                      return a.opening && !b.opening;
                  });

        // Sweep the edges, keeping a window open while any cooldown buff is
        // active. Counting active buffs merges overlaps.
        int active = 0;
        int32_t windowStart = 0;
        std::vector<uint32_t> windowSpells;

        for (const auto& edge : edges) {
            if (edge.opening) {
                if (active == 0) {
                    windowStart = edge.timeMs;
                    windowSpells.clear();
                }
                ++active;
                if (std::find(windowSpells.begin(), windowSpells.end(),
                              edge.spellId) == windowSpells.end()) {
                    windowSpells.push_back(edge.spellId);
                }
                continue;
            }

            if (active == 0) continue;
            --active;
            if (active > 0) continue;

            const int32_t windowMs = edge.timeMs - windowStart;
            if (windowMs <= 0) continue;

            // A window covering most of the pull is a passive or stacking
            // aura that was ruled a cooldown, not a burst. Counting it would
            // make the whole pull read as burst.
            if (pullEnd > 0 && windowMs > (pullEnd * 3) / 4) continue;

            // Named after the cooldown that opened it; the rest are recorded
            // in stackedSpellIds.
            ProfileCooldownUse use;
            use.spellId = windowSpells.empty() ? edge.spellId : windowSpells.front();
            use.useIndex = ++useCounter[use.spellId];
            use.pullOffsetMs = windowStart;
            use.windowMs = windowMs;
            use.stackedSpellIds = windowSpells;
            uses.push_back(use);
            burstWindows.push_back({windowStart, windowStart + windowMs});
        }

        std::sort(uses.begin(), uses.end(),
                  [](const ProfileCooldownUse& a, const ProfileCooldownUse& b) {
                      return a.pullOffsetMs < b.pullOffsetMs;
                  });
    }

    burstWindows = mergeWindows(std::move(burstWindows));

    // Damage, split by direction and target
    auto& interner = guidInterner();
    const auto actorId = interner.find(request.actorGuid);

    std::vector<ProfileTimelineBucket> buckets;
    const size_t bucketCount = static_cast<size_t>(pullEnd / kBucketMs) + 1;
    buckets.resize(bucketCount);
    for (size_t i = 0; i < bucketCount; ++i) {
        buckets[i].startMs = static_cast<int32_t>(i) * kBucketMs;
    }

    std::unordered_map<uint32_t, ProfileDamageDone> damageByNpc;
    std::unordered_map<uint32_t, ProfileDamageTaken> damageTakenBySpell;

    for (const auto& event : data.combatEvents) {
        if (event.event_type != CombatEventType::DamageDealt) continue;
        if (event.timestamp_ms < 0 || event.timestamp_ms > pullEnd) continue;

        const size_t bucket = static_cast<size_t>(event.timestamp_ms / kBucketMs);
        if (bucket >= buckets.size()) continue;

        // Damage this actor dealt, including their pets.
        const bool isSource = (event.source_guid_id == actorId) ||
                              (event.owner_guid_id == actorId);
        if (isSource) {
            const uint32_t npcId = resolveNpcId(interner.lookup(event.target_guid_id),
                                                data.actorIdToNpcId);

            // Weighted once here and used by every figure built from this
            // event, so the headline, timeline and per-target split agree.
            const int64_t weighted = weightedAmount(event.effective_amount, npcId);

            buckets[bucket].damageDone += weighted;

            auto& done = damageByNpc[npcId];
            done.npcId = npcId;
            done.total += weighted;
            done.casts += 1;

            // The raw total is kept so it can be matched against other tools.
            profile.rawDamageDone += event.effective_amount;
            profile.weightedDamageDone += weighted;
            continue;
        }

        if (event.target_guid_id == actorId) {
            buckets[bucket].damageTaken += event.effective_amount;

            auto& taken = damageTakenBySpell[event.spell_id];
            taken.spellId = event.spell_id;
            taken.hits += 1;
            taken.total += event.effective_amount;
        }
    }

    // Cast counts per bucket
    for (const SpellCastEvent* cast : globalCasts) {
        const int32_t t = cast->end_timestamp_ms;
        if (t < 0 || t > pullEnd) continue;
        const size_t bucket = static_cast<size_t>(t / kBucketMs);
        if (bucket < buckets.size()) {
            buckets[bucket].casts += 1;
        }
    }

    // Buffs on this player, as intervals. Externals are kept here: a cooldown
    // window that caught someone else's buff is a common reason two uses of
    // the same spell differ.
    struct BuffInterval {
        uint32_t spellId = 0;
        int32_t startMs = 0;
        int32_t endMs = 0;
    };
    std::vector<BuffInterval> buffIntervals;
    {
        std::unordered_map<uint32_t, int32_t> openBuffs;
        for (const auto& aura : data.auraEvents) {
            if (aura.target_guid != request.actorGuid) continue;
            if (ignoredAuras.count(aura.spell_id)) continue;

            switch (aura.event_type) {
                case AuraEventType::Applied:
                case AuraEventType::Refresh:
                    if (!openBuffs.count(aura.spell_id)) {
                        openBuffs[aura.spell_id] = aura.timestamp_ms;
                    }
                    break;
                case AuraEventType::Removed:
                case AuraEventType::Broken:
                case AuraEventType::BrokenSpell: {
                    auto it = openBuffs.find(aura.spell_id);
                    if (it != openBuffs.end()) {
                        buffIntervals.push_back({aura.spell_id, it->second,
                                                 aura.timestamp_ms});
                        openBuffs.erase(it);
                    }
                    break;
                }
                default:
                    break;
            }
        }
        // Buffs still up at the end of the pull run to the end.
        for (const auto& [spellId, startMs] : openBuffs) {
            buffIntervals.push_back({spellId, startMs, pullEnd});
        }
    }

    for (auto& use : uses) {
        const int32_t end = use.pullOffsetMs + use.windowMs;

        // One sample per second of the window, so windows of different
        // lengths can be drawn together without rescaling.
        const size_t sampleCount =
            static_cast<size_t>((use.windowMs + 999) / 1000) + 1;
        use.damageCurve.assign(sampleCount, 0);

        for (const auto& event : data.combatEvents) {
            if (event.event_type != CombatEventType::DamageDealt) continue;
            if (event.timestamp_ms < use.pullOffsetMs || event.timestamp_ms >= end) continue;
            if (event.source_guid_id != actorId && event.owner_guid_id != actorId) continue;

            const uint32_t npcId = resolveNpcId(interner.lookup(event.target_guid_id),
                                                data.actorIdToNpcId);
            const int64_t weighted = weightedAmount(event.effective_amount, npcId);
            use.damageInWindow += weighted;

            const size_t bucket =
                static_cast<size_t>((event.timestamp_ms - use.pullOffsetMs) / 1000);
            if (bucket < use.damageCurve.size()) {
                use.damageCurve[bucket] += weighted;
            }
        }

        // Stored as a running total, so two curves show how far ahead one
        // window was at every point.
        for (size_t i = 1; i < use.damageCurve.size(); ++i) {
            use.damageCurve[i] += use.damageCurve[i - 1];
        }
        for (const SpellCastEvent* cast : globalCasts) {
            if (cast->end_timestamp_ms >= use.pullOffsetMs && cast->end_timestamp_ms < end) {
                use.castsInWindow += 1;
            }
        }

        // Where the burst went, by target.
        {
            std::unordered_map<uint32_t, ProfileDamageDone> byTarget;
            for (const auto& event : data.combatEvents) {
                if (event.event_type != CombatEventType::DamageDealt) continue;
                if (event.timestamp_ms < use.pullOffsetMs ||
                    event.timestamp_ms >= end) continue;
                if (event.source_guid_id != actorId &&
                    event.owner_guid_id != actorId) continue;

                const uint32_t npcId = resolveNpcId(
                    interner.lookup(event.target_guid_id), data.actorIdToNpcId);
                auto& row = byTarget[npcId];
                row.npcId = npcId;
                row.total += weightedAmount(event.effective_amount, npcId);
                row.casts += 1;
            }
            for (auto& entry : byTarget) {
                use.damageByTarget.push_back(entry.second);
            }
            std::sort(use.damageByTarget.begin(), use.damageByTarget.end(),
                      [](const ProfileDamageDone& a, const ProfileDamageDone& b) {
                          return a.total > b.total;
                      });
        }

        // Off-global casts are left out of the sequence. It is meant to show
        // what the player pressed, and automatic or free casts (resource
        // ticks, swings) can outnumber the real presses. Which spells are
        // off-global comes from the user's rulings, since it differs per spec.
        for (const SpellCastEvent* cast : casts) {
            if (offGlobalSpells.count(cast->spell_id)) continue;
            if (cast->end_timestamp_ms >= use.pullOffsetMs && cast->end_timestamp_ms < end) {
                use.castSequence.push_back(cast->spell_id);
            }
        }

        for (const auto& interval : buffIntervals) {
            const int32_t overlap = std::min(interval.endMs, end) -
                                    std::max(interval.startMs, use.pullOffsetMs);
            if (overlap <= 0) continue;
            const int32_t offset = std::max(interval.startMs, use.pullOffsetMs) -
                                   use.pullOffsetMs;
            use.buffs.push_back({interval.spellId, overlap, offset});
        }
        // Longest overlap first.
        std::sort(use.buffs.begin(), use.buffs.end(),
                  [](const ProfileCooldownUse::WindowBuff& a,
                     const ProfileCooldownUse::WindowBuff& b) {
                      if (a.overlapMs != b.overlapMs) return a.overlapMs > b.overlapMs;
                      return a.spellId < b.spellId;
                  });
    }

    // Defensives, with the damage taken while each was up.
    if (!request.defensiveSpellIds.empty()) {
        const std::unordered_set<uint32_t> defensives(
            request.defensiveSpellIds.begin(), request.defensiveSpellIds.end());

        for (const SpellCastEvent* cast : casts) {
            if (!defensives.count(cast->spell_id)) continue;
            if (cast->end_timestamp_ms < 0) continue;

            ProfileDefensiveUse use;
            use.spellId = cast->spell_id;
            use.pullOffsetMs = cast->end_timestamp_ms;

            // The buff of the same id gives the real duration where one
            // exists; otherwise assume 8s, which most defensives are close to.
            constexpr int32_t kAssumedDefensiveMs = 8000;
            use.windowMs = kAssumedDefensiveMs;
            for (const auto& aura : data.auraEvents) {
                if (aura.spell_id != cast->spell_id) continue;
                if (aura.target_guid != request.actorGuid) continue;
                if (aura.event_type != AuraEventType::Removed) continue;
                if (aura.timestamp_ms <= use.pullOffsetMs) continue;
                use.windowMs = aura.timestamp_ms - use.pullOffsetMs;
                break;
            }

            const int32_t end = use.pullOffsetMs + use.windowMs;
            for (const auto& event : data.combatEvents) {
                if (event.event_type != CombatEventType::DamageDealt) continue;
                if (event.target_guid_id != actorId) continue;
                if (event.timestamp_ms < use.pullOffsetMs ||
                    event.timestamp_ms >= end) continue;
                use.damageTakenInWindow += event.effective_amount;
            }

            profile.defensiveUses.push_back(use);
        }

        std::sort(profile.defensiveUses.begin(), profile.defensiveUses.end(),
                  [](const ProfileDefensiveUse& a, const ProfileDefensiveUse& b) {
                      return a.pullOffsetMs < b.pullOffsetMs;
                  });
    }

    // Position samples
    {
        constexpr int32_t kPositionSampleMs = 1000;

        // Treat whichever actor took the most damage from the player as the
        // boss, which avoids needing an id per encounter.
        uint32_t bossActorId = 0;
        {
            std::unordered_map<uint32_t, int64_t> hostileDamage;
            for (const auto& event : data.combatEvents) {
                if (event.event_type != CombatEventType::DamageDealt) continue;
                if (event.source_guid_id != actorId &&
                    event.owner_guid_id != actorId) continue;
                hostileDamage[event.target_guid_id] += event.effective_amount;
            }
            int64_t best = 0;
            for (const auto& [id, total] : hostileDamage) {
                if (total > best) { best = total; bossActorId = id; }
            }
        }

        // Boss position per sample, so the path can be drawn relative to it.
        // Position events carry the GUID as text, so the interned boss id is
        // looked up first.
        const std::string bossGuid = bossActorId != 0
            ? std::string(interner.lookup(bossActorId)) : std::string();

        std::unordered_map<int32_t, std::pair<float, float>> bossAt;
        if (!bossGuid.empty()) {
            for (const auto& actor : data.actorEvents) {
                if (actor.source_guid != bossGuid) continue;
                if (actor.timestamp_ms < 0) continue;
                bossAt[actor.timestamp_ms / kPositionSampleMs] = {actor.x_loc, actor.y_loc};
            }
        }

        int32_t lastSample = -1;
        for (const auto& actor : data.actorEvents) {
            if (actor.source_guid != request.actorGuid) continue;
            if (actor.timestamp_ms < 0 || actor.timestamp_ms > pullEnd) continue;

            const int32_t sample = actor.timestamp_ms / kPositionSampleMs;
            if (sample == lastSample) continue;
            lastSample = sample;

            ProfilePosition pos;
            pos.timeMs = actor.timestamp_ms;
            pos.x = actor.x_loc;
            pos.y = actor.y_loc;
            if (auto it = bossAt.find(sample); it != bossAt.end()) {
                pos.bossX = it->second.first;
                pos.bossY = it->second.second;
                pos.hasBoss = true;
            }
            profile.positions.push_back(pos);
        }
    }

    profile.timeline = std::move(buckets);
    profile.cooldownUses = std::move(uses);

    // Idle time: gaps between globals, each charged only up to
    // kMaxChargeableGapMs.
    //
    // Imported reports carry no haste, so every cast would get the 1.5s base
    // global, which on a hasted pull adds up to more than the pull itself. One
    // inferred global for all spells is not right either, since different
    // spells chain at different speeds. Instead each spell's global is taken
    // from the short end of the gaps that follow it.
    std::unordered_map<uint32_t, int32_t> observedGcd;
    if (!globalCasts.empty() && globalCasts.front()->haste_permille == 0) {
        std::unordered_map<uint32_t, std::vector<int32_t>> gapsBySpell;

        // Gap to the next cast of any spell, keyed by the earlier cast: how
        // long this press blocked the next one.
        for (size_t i = 1; i < globalCasts.size(); ++i) {
            const int32_t gap = globalCasts[i]->end_timestamp_ms -
                                globalCasts[i - 1]->end_timestamp_ms;
            if (gap >= 400 && gap <= 2200) {
                gapsBySpell[globalCasts[i - 1]->spell_id].push_back(gap);
            }
        }

        for (auto& [spellId, gaps] : gapsBySpell) {
            if (gaps.size() < 4) continue;
            std::sort(gaps.begin(), gaps.end());
            // The 20th percentile: the fast end is the spell chained straight
            // into the next, i.e. its global. Higher percentiles include
            // hesitation.
            observedGcd[spellId] = std::clamp(gaps[gaps.size() / 5],
                                              kMinGcdMs, kStandardGcdMs);
        }
    }

    // Time spent channelling, from the auras the user ruled Channel. The idle
    // loop subtracts it from gaps.
    std::vector<ActiveWindow> channelWindows;
    if (!request.channelAuraIds.empty()) {
        const std::unordered_set<uint32_t> channelAuras(
            request.channelAuraIds.begin(), request.channelAuraIds.end());

        std::unordered_map<uint32_t, int32_t> openAt;
        for (const auto& aura : data.auraEvents) {
            if (!channelAuras.count(aura.spell_id)) continue;
            if (aura.target_guid != request.actorGuid) continue;

            switch (aura.event_type) {
                case AuraEventType::Applied:
                    openAt[aura.spell_id] = aura.timestamp_ms;
                    break;
                case AuraEventType::Removed:
                case AuraEventType::Broken:
                case AuraEventType::BrokenSpell: {
                    auto it = openAt.find(aura.spell_id);
                    if (it == openAt.end()) break;
                    if (aura.timestamp_ms > it->second) {
                        channelWindows.push_back({it->second, aura.timestamp_ms});
                    }
                    openAt.erase(it);
                    break;
                }
                default:
                    break;
            }
        }
        channelWindows = mergeWindows(std::move(channelWindows));
    }

    // How much of a span was spent channelling.
    const auto channelledMs = [&](int32_t startMs, int32_t endMs) {
        return overlapWithWindows(channelWindows, startMs, endMs);
    };

    int32_t burstIdle = 0;
    int32_t fillerIdle = 0;
    for (size_t i = 1; i < globalCasts.size(); ++i) {
        const SpellCastEvent* prevCast = globalCasts[i - 1];
        const SpellCastEvent* nextCast = globalCasts[i];

        // Idle starts once the previous global has run out. The inferred
        // global replaces the logged one where there is one, but a spell the
        // game data lists as off-global (base 0) stays off-global.
        const int32_t baseGcd = baseGcdFor(prevCast->spell_id);
        int32_t gcdMs = castOccupancyMs(prevCast, baseGcd);
        if (baseGcd > 0) {
            if (auto it = observedGcd.find(prevCast->spell_id);
                it != observedGcd.end()) {
                gcdMs = std::max(it->second, prevCast->cast_duration_ms());
            }
        }
        const int32_t busyUntil = std::max(prevCast->end_timestamp_ms + gcdMs,
                                           prevCast->end_timestamp_ms);

        // Idle ends when the next cast begins, not when it completes.
        const int32_t nextStart = nextCast->start_timestamp_ms > 0
                                      ? nextCast->start_timestamp_ms
                                      : nextCast->end_timestamp_ms;

        int32_t gap = nextStart - busyUntil;
        if (gap <= 0) continue;

        // A channel running through the gap is not idle time.
        gap -= channelledMs(busyUntil, nextStart);
        if (gap <= 0) continue;

        const int32_t charged = std::min(gap, kMaxChargeableGapMs);
        if (insideAnyWindow(burstWindows, prevCast->end_timestamp_ms)) {
            burstIdle += charged;
        } else {
            fillerIdle += charged;
        }

        // Gaps under a second are latency and rounding; only longer ones are
        // listed individually.
        constexpr int32_t kReportableGapMs = 1000;
        if (gap < kReportableGapMs) continue;

        ProfileDowntimeGap entry;
        entry.startMs = busyUntil;
        entry.durationMs = gap;
        entry.inCooldown = insideAnyWindow(burstWindows, busyUntil);
        entry.beforeSpellId = prevCast->spell_id;
        entry.afterSpellId = nextCast->spell_id;
        entry.expectedGcdMs = gcdMs;

        // The casts in the ten seconds before the gap, collected backwards and
        // then reversed into play order.
        constexpr int32_t kLeadInMs = 10000;
        for (size_t back = i; back-- > 0;) {
            const SpellCastEvent* cast = globalCasts[back];
            if (cast->end_timestamp_ms < busyUntil - kLeadInMs) break;

            ProfileDowntimeGap::PriorCast prior;
            prior.spellId = cast->spell_id;
            prior.timeMs = cast->end_timestamp_ms;
            prior.occupancyMs = castOccupancyMs(cast, baseGcdFor(cast->spell_id));
            entry.leadIn.push_back(prior);
            if (entry.leadIn.size() >= 12) break;
        }
        std::reverse(entry.leadIn.begin(), entry.leadIn.end());

        // A cast that was started and abandoned.
        if (prevCast->cancelled) {
            entry.cancelledCast = true;
            entry.cancelledSpellId = prevCast->spell_id;
        }

        // Debuffs covering the whole gap. One that landed partway through did
        // not cause the start of it.
        for (const auto& aura : data.auraEvents) {
            if (aura.target_guid != request.actorGuid) continue;
            if (aura.event_type != AuraEventType::Applied) continue;
            if (aura.timestamp_ms > entry.startMs) continue;
            if (ignoredAuras.count(aura.spell_id)) continue;

            bool stillUp = true;
            for (const auto& off : data.auraEvents) {
                if (off.spell_id != aura.spell_id) continue;
                if (off.target_guid != request.actorGuid) continue;
                if (off.event_type != AuraEventType::Removed) continue;
                if (off.timestamp_ms > aura.timestamp_ms &&
                    off.timestamp_ms < entry.startMs + entry.durationMs) {
                    stillUp = false;
                    break;
                }
            }
            if (stillUp) entry.debuffSpellIds.push_back(aura.spell_id);
        }

        profile.downtimeGaps.push_back(std::move(entry));
    }

    // Longest first
    std::sort(profile.downtimeGaps.begin(), profile.downtimeGaps.end(),
              [](const ProfileDowntimeGap& a, const ProfileDowntimeGap& b) {
                  return a.durationMs > b.durationMs;
              });

    // Phases
    phase::RuleInputs inputs;
    inputs.pullStart_ms = 0;
    inputs.pullEnd_ms = pullEnd;
    inputs.firstCastTime = [&data](uint32_t spellId) -> std::optional<int32_t> {
        std::optional<int32_t> earliest;
        for (const auto& cast : data.spellCastEvents) {
            if (cast.spell_id != spellId) continue;
            if (!earliest || cast.end_timestamp_ms < *earliest) {
                earliest = cast.end_timestamp_ms;
            }
        }
        return earliest;
    };
    inputs.firstEmoteTime = [&data](std::string_view text) -> std::optional<int32_t> {
        std::optional<int32_t> earliest;
        for (const auto& emote : data.emoteEvents) {
            if (emote.text != text) continue;
            if (!earliest || emote.timestamp_ms < *earliest) {
                earliest = emote.timestamp_ms;
            }
        }
        return earliest;
    };

    auto resolved = phase::resolvePhases(request.phaseRules, inputs);
    if (resolved.empty()) {
        // No rules, or none fired: use one window covering the whole pull.
        resolved.push_back({"", 0, pullEnd});
    }

    // Rules that never fired, so the comparison can report those windows as
    // not comparable.
    for (const auto& rule : request.phaseRules) {
        if (!phase::resolveRuleTime(rule, inputs).has_value() && !rule.label.empty()) {
            profile.unresolvedPhaseLabels.push_back(rule.label);
        }
    }

    for (const auto& window : resolved) {
        ProfilePhase phaseOut;
        phaseOut.label = window.label;
        phaseOut.durationMs = window.end_ms - window.start_ms;
        if (phaseOut.durationMs <= 0) continue;

        std::unordered_map<uint32_t, ProfileSpellUsage> spellUsage;
        int32_t phaseCasts = 0;
        int32_t burstCasts = 0;

        for (const SpellCastEvent* cast : casts) {
            const int32_t t = cast->end_timestamp_ms;
            if (t < window.start_ms || t >= window.end_ms) continue;

            auto& usage = spellUsage[cast->spell_id];
            usage.spellId = cast->spell_id;
            usage.casts += 1;
            usage.isCooldown = cast->cooldown_ms > 0;
            usage.castTimeMs += cast->cast_duration_ms();
            if (usage.firstCastOffsetMs < 0) {
                usage.firstCastOffsetMs = t - window.start_ms;
            }
            // Per-spell usage counts every cast; the two counters below are
            // globals only.
            if (offGlobalSpells.count(cast->spell_id)) continue;
            ++phaseCasts;
            if (insideAnyWindow(burstWindows, t)) ++burstCasts;
        }

        const double phaseMinutes = phaseOut.durationMs / 60000.0;
        for (auto& [spellId, usage] : spellUsage) {
            usage.castsPerMinute = phaseMinutes > 0.0
                                       ? static_cast<float>(usage.casts / phaseMinutes)
                                       : 0.0f;
            phaseOut.spells.push_back(usage);
        }
        std::sort(phaseOut.spells.begin(), phaseOut.spells.end(),
                  [](const ProfileSpellUsage& a, const ProfileSpellUsage& b) {
                      return a.casts > b.casts;
                  });

        int64_t phaseDone = 0;
        int64_t phaseTaken = 0;
        int64_t phaseBurstDamage = 0;
        std::unordered_map<uint32_t, ProfileDamageBySpell> damageBySpell;
        for (const auto& event : data.combatEvents) {
            if (event.event_type != CombatEventType::DamageDealt) continue;
            if (event.timestamp_ms < window.start_ms || event.timestamp_ms >= window.end_ms) {
                continue;
            }
            if (event.source_guid_id == actorId || event.owner_guid_id == actorId) {
                const uint32_t targetNpc = resolveNpcId(
                    interner.lookup(event.target_guid_id), data.actorIdToNpcId);
                const int64_t weighted =
                    weightedAmount(event.effective_amount, targetNpc);

                phaseDone += weighted;
                if (insideAnyWindow(burstWindows, event.timestamp_ms)) {
                    phaseBurstDamage += weighted;
                }
                // Recorded under the spell id that dealt the damage, which is
                // often not the id that was cast (a projectile, proc or dot).
                auto& bySpell = damageBySpell[event.spell_id];
                bySpell.spellId = event.spell_id;
                bySpell.total += weighted;
                bySpell.hits += 1;

                // Also credited to the cast where the ids match, for damage
                // per cast.
                if (auto it = spellUsage.find(event.spell_id);
                    it != spellUsage.end()) {
                    it->second.damage += weighted;
                }
            } else if (event.target_guid_id == actorId) {
                phaseTaken += event.effective_amount;
            }
        }

        phaseOut.totalDamageDone = phaseDone;
        phaseOut.totalDamageTaken = phaseTaken;

        for (auto& entry : damageBySpell) {
            phaseOut.damageBySpell.push_back(entry.second);
        }
        std::sort(phaseOut.damageBySpell.begin(), phaseOut.damageBySpell.end(),
                  [](const ProfileDamageBySpell& a, const ProfileDamageBySpell& b) {
                      return a.total > b.total;
                  });

        const int32_t burstMs = overlapWithWindows(burstWindows, window.start_ms, window.end_ms);
        phaseOut.inCooldowns.durationMs = burstMs;
        phaseOut.inCooldowns.damage = phaseBurstDamage;
        phaseOut.inCooldowns.casts = burstCasts;
        phaseOut.outsideCooldowns.durationMs = phaseOut.durationMs - burstMs;
        phaseOut.outsideCooldowns.damage = phaseDone - phaseBurstDamage;
        phaseOut.outsideCooldowns.casts = phaseCasts - burstCasts;

        // Idle is measured over the whole pull and shared out to each phase
        // by how much of the region falls inside it. It is only ever read as
        // a share, so exact per-phase idle is not worth a second pass.
        const double burstShare = burstIdle > 0 && !burstWindows.empty()
            ? static_cast<double>(burstMs) / std::max(1, overlapWithWindows(
                  burstWindows, 0, pullEnd))
            : 0.0;
        const double fillerShare = phaseOut.outsideCooldowns.durationMs > 0
            ? static_cast<double>(phaseOut.outsideCooldowns.durationMs) /
                  std::max(1, pullEnd - overlapWithWindows(burstWindows, 0, pullEnd))
            : 0.0;
        phaseOut.inCooldowns.idleMs = static_cast<int32_t>(burstIdle * burstShare);
        phaseOut.outsideCooldowns.idleMs = static_cast<int32_t>(fillerIdle * fillerShare);

        for (const auto& death : data.deathEvents) {
            if (death.actor_guid != request.actorGuid) continue;
            if (death.timestamp_ms >= window.start_ms && death.timestamp_ms < window.end_ms) {
                phaseOut.deaths += 1;
            }
        }

        // The target and damage-taken breakdowns are whole-pull totals, so
        // they are only attached when there is a single phase. Nothing reads
        // them per phase yet.
        if (resolved.size() == 1) {
            for (auto& [npcId, done] : damageByNpc) {
                phaseOut.damageDone.push_back(done);
            }
            std::sort(phaseOut.damageDone.begin(), phaseOut.damageDone.end(),
                      [](const ProfileDamageDone& a, const ProfileDamageDone& b) {
                          return a.total > b.total;
                      });
            for (auto& [spellId, taken] : damageTakenBySpell) {
                phaseOut.damageTaken.push_back(taken);
            }
            std::sort(phaseOut.damageTaken.begin(), phaseOut.damageTaken.end(),
                      [](const ProfileDamageTaken& a, const ProfileDamageTaken& b) {
                          return a.total > b.total;
                      });
        }

        profile.phases.push_back(std::move(phaseOut));
    }

    return profile;
}

} // namespace benchmark
