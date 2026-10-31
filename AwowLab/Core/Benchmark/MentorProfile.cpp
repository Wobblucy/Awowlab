#include "Core/Benchmark/MentorProfile.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>

namespace benchmark {

namespace {

// Profiles outlive the build that wrote them - they are saved to disk and
// shared between users - so a version stamp is the difference between
// "reject this cleanly" and "read garbage into a comparison".
// Version 2: window buffs became (id, overlap, offset) triples. A version 1
// profile's buff array is (id, overlap) pairs, and reading it with the triple
// stride would fill offsets with the next buff's spell id.
constexpr int kProfileVersion = 2;

const char* ruleKindName(PhaseSettings::RuleKind kind) {
    switch (kind) {
        case PhaseSettings::RuleKind::Elapsed: return "time";
        case PhaseSettings::RuleKind::Emote:   return "emote";
        case PhaseSettings::RuleKind::SpellCast:
        default:                               return "cast";
    }
}

PhaseSettings::RuleKind ruleKindFromName(std::string_view name) {
    if (name == "time")  return PhaseSettings::RuleKind::Elapsed;
    if (name == "emote") return PhaseSettings::RuleKind::Emote;
    return PhaseSettings::RuleKind::SpellCast;
}

// Fraction of a bucket that falls before the cutoff. Buckets are coarse
// (5s), so a cutoff landing mid-bucket is the common case, not the edge
// case; counting the whole bucket would hand the mentor up to 5 seconds of
// damage the user never had time to match.
double bucketOverlap(const ProfileTimelineBucket& bucket, int32_t bucketMs, int32_t cutoffMs) {
    const int32_t end = bucket.startMs + bucketMs;
    if (cutoffMs >= end) return 1.0;
    if (cutoffMs <= bucket.startMs) return 0.0;
    return static_cast<double>(cutoffMs - bucket.startMs) / static_cast<double>(bucketMs);
}

void addJsonInt(rapidjson::Value& obj, const char* name, int64_t v,
                rapidjson::Document::AllocatorType& alloc) {
    obj.AddMember(rapidjson::StringRef(name), v, alloc);
}

int32_t readInt(const rapidjson::Value& obj, const char* name, int32_t fallback = 0) {
    if (!obj.HasMember(name)) return fallback;
    const auto& v = obj[name];
    return v.IsInt() ? v.GetInt() : fallback;
}

int64_t readInt64(const rapidjson::Value& obj, const char* name, int64_t fallback = 0) {
    if (!obj.HasMember(name)) return fallback;
    const auto& v = obj[name];
    if (v.IsInt64()) return v.GetInt64();
    if (v.IsInt())   return v.GetInt();
    return fallback;
}

uint32_t readUint(const rapidjson::Value& obj, const char* name, uint32_t fallback = 0) {
    if (!obj.HasMember(name)) return fallback;
    const auto& v = obj[name];
    return v.IsUint() ? v.GetUint() : fallback;
}

std::string readString(const rapidjson::Value& obj, const char* name) {
    if (!obj.HasMember(name)) return {};
    const auto& v = obj[name];
    return v.IsString() ? std::string(v.GetString(), v.GetStringLength()) : std::string{};
}

rapidjson::Value splitToJson(const ProfileThroughputSplit& split,
                             rapidjson::Document::AllocatorType& alloc) {
    rapidjson::Value obj(rapidjson::kObjectType);
    addJsonInt(obj, "ms", split.durationMs, alloc);
    addJsonInt(obj, "dmg", split.damage, alloc);
    addJsonInt(obj, "casts", split.casts, alloc);
    addJsonInt(obj, "idle", split.idleMs, alloc);
    return obj;
}

ProfileThroughputSplit splitFromJson(const rapidjson::Value& obj) {
    ProfileThroughputSplit split;
    if (!obj.IsObject()) return split;
    split.durationMs = readInt(obj, "ms");
    split.damage     = readInt64(obj, "dmg");
    split.casts      = readInt(obj, "casts");
    split.idleMs     = readInt(obj, "idle");
    return split;
}

} // namespace

MentorProfile::Window MentorProfile::windowUpTo(int32_t cutoffMs) const {
    Window window;
    if (cutoffMs <= 0 || timeline.empty()) {
        return window;
    }

    // Never credit the mentor with time beyond their own pull: comparing a
    // user who survived longer than the reference should show the mentor's
    // real pull length, not a padded one.
    window.durationMs = std::min(cutoffMs, pullDurationMs > 0 ? pullDurationMs : cutoffMs);

    for (const auto& bucket : timeline) {
        const double share = bucketOverlap(bucket, bucketMs, cutoffMs);
        if (share <= 0.0) continue;

        window.damageDone  += static_cast<int64_t>(bucket.damageDone * share);
        window.damageTaken += static_cast<int64_t>(bucket.damageTaken * share);
        window.casts       += static_cast<int32_t>(bucket.casts * share);
    }

    // The burst/filler split is rebuilt from the phase aggregates rather than
    // the timeline, because the timeline does not record which region each
    // bucket belonged to. Phases are scaled by how much of them the cutoff
    // covers, which keeps the split consistent with the totals above.
    int32_t phaseStart = 0;
    for (const auto& phase : phases) {
        if (phaseStart >= cutoffMs) break;

        const int32_t phaseEnd = phaseStart + phase.durationMs;
        const int32_t covered = std::min(phaseEnd, cutoffMs) - phaseStart;
        if (covered <= 0) {
            phaseStart = phaseEnd;
            continue;
        }

        const double share = phase.durationMs > 0
                                 ? static_cast<double>(covered) / phase.durationMs
                                 : 0.0;

        const auto scale = [share](const ProfileThroughputSplit& src,
                                   ProfileThroughputSplit& dst) {
            dst.durationMs += static_cast<int32_t>(src.durationMs * share);
            dst.damage     += static_cast<int64_t>(src.damage * share);
            dst.casts      += static_cast<int32_t>(src.casts * share);
            dst.idleMs     += static_cast<int32_t>(src.idleMs * share);
        };
        scale(phase.inCooldowns, window.inCooldowns);
        scale(phase.outsideCooldowns, window.outsideCooldowns);

        phaseStart = phaseEnd;
    }

    return window;
}

std::vector<ProfileCooldownUse> MentorProfile::cooldownsUpTo(int32_t cutoffMs) const {
    std::vector<ProfileCooldownUse> used;
    if (cutoffMs <= 0) return used;

    for (const auto& use : cooldownUses) {
        // Keyed on when the cooldown went out, not when its window closed. A
        // cooldown popped just before the user died still shows whether it was
        // held too long, which is the thing worth reporting.
        if (use.pullOffsetMs < cutoffMs) {
            used.push_back(use);
        }
    }
    return used;
}

std::string MentorProfile::toJson() const {
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    doc.AddMember("version", kProfileVersion, alloc);
    doc.AddMember("source", rapidjson::Value(sourceLabel.c_str(), alloc), alloc);
    doc.AddMember("player", rapidjson::Value(playerName.c_str(), alloc), alloc);
    doc.AddMember("actor", rapidjson::Value(actorGuid.c_str(), alloc), alloc);
    doc.AddMember("encounter", encounterId, alloc);
    doc.AddMember("spec", specId, alloc);
    doc.AddMember("difficulty", difficulty, alloc);
    doc.AddMember("durationMs", pullDurationMs, alloc);
    doc.AddMember("kill", kill, alloc);
    doc.AddMember("bucketMs", bucketMs, alloc);

    // The rules the profile was cut with. Comparing against a player whose
    // rules have since changed would silently misalign the phase windows, so
    // they travel with the profile and are checked before comparing.
    rapidjson::Value ruleArray(rapidjson::kArrayType);
    for (const auto& rule : phaseRules) {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("kind", rapidjson::Value(ruleKindName(rule.kind), alloc), alloc);
        obj.AddMember("spell", rule.spellId, alloc);
        obj.AddMember("ms", rule.elapsedMs, alloc);
        if (!rule.emoteText.empty()) {
            obj.AddMember("text", rapidjson::Value(rule.emoteText.c_str(), alloc), alloc);
        }
        if (!rule.label.empty()) {
            obj.AddMember("label", rapidjson::Value(rule.label.c_str(), alloc), alloc);
        }
        ruleArray.PushBack(obj, alloc);
    }
    doc.AddMember("phaseRules", ruleArray, alloc);

    rapidjson::Value cdIds(rapidjson::kArrayType);
    for (uint32_t id : majorCooldownSpellIds) cdIds.PushBack(id, alloc);
    doc.AddMember("majorCooldowns", cdIds, alloc);

    rapidjson::Value ignoredIds(rapidjson::kArrayType);
    for (uint32_t id : ignoredSpellIds) ignoredIds.PushBack(id, alloc);
    doc.AddMember("ignoredSpells", ignoredIds, alloc);

    rapidjson::Value offGlobalIds(rapidjson::kArrayType);
    for (uint32_t id : offGlobalSpellIds) offGlobalIds.PushBack(id, alloc);
    doc.AddMember("offGlobalSpells", offGlobalIds, alloc);

    rapidjson::Value ignoredAuras(rapidjson::kArrayType);
    for (uint32_t id : ignoredAuraIds) ignoredAuras.PushBack(id, alloc);
    doc.AddMember("ignoredAuras", ignoredAuras, alloc);
    doc.AddMember("mobWeights", mobWeightFingerprint, alloc);

    rapidjson::Value burstIds(rapidjson::kArrayType);
    for (uint32_t id : burstWindowSpellIds) burstIds.PushBack(id, alloc);
    doc.AddMember("burstWindowSpells", burstIds, alloc);

    rapidjson::Value buffOnlyIds(rapidjson::kArrayType);
    for (uint32_t id : buffOnlyCooldownIds) buffOnlyIds.PushBack(id, alloc);
    doc.AddMember("buffOnlyCooldowns", buffOnlyIds, alloc);

    rapidjson::Value unresolved(rapidjson::kArrayType);
    for (const auto& label : unresolvedPhaseLabels) {
        unresolved.PushBack(rapidjson::Value(label.c_str(), alloc), alloc);
    }
    doc.AddMember("unresolvedPhases", unresolved, alloc);

    rapidjson::Value phaseArray(rapidjson::kArrayType);
    for (const auto& phase : phases) {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("label", rapidjson::Value(phase.label.c_str(), alloc), alloc);
        addJsonInt(obj, "ms", phase.durationMs, alloc);
        addJsonInt(obj, "dmgDone", phase.totalDamageDone, alloc);
        addJsonInt(obj, "dmgTaken", phase.totalDamageTaken, alloc);
        addJsonInt(obj, "deaths", phase.deaths, alloc);
        obj.AddMember("burst", splitToJson(phase.inCooldowns, alloc), alloc);
        obj.AddMember("filler", splitToJson(phase.outsideCooldowns, alloc), alloc);

        rapidjson::Value spellArray(rapidjson::kArrayType);
        for (const auto& spell : phase.spells) {
            rapidjson::Value s(rapidjson::kObjectType);
            s.AddMember("id", spell.spellId, alloc);
            addJsonInt(s, "casts", spell.casts, alloc);
            s.AddMember("cpm", spell.castsPerMinute, alloc);
            addJsonInt(s, "first", spell.firstCastOffsetMs, alloc);
            s.AddMember("cd", spell.isCooldown, alloc);
            addJsonInt(s, "dmg", spell.damage, alloc);
            addJsonInt(s, "ct", spell.castTimeMs, alloc);
            spellArray.PushBack(s, alloc);
        }
        obj.AddMember("spells", spellArray, alloc);

        rapidjson::Value bySpellArray(rapidjson::kArrayType);
        for (const auto& src : phase.damageBySpell) {
            rapidjson::Value d(rapidjson::kObjectType);
            d.AddMember("id", src.spellId, alloc);
            addJsonInt(d, "dmg", src.total, alloc);
            addJsonInt(d, "hits", src.hits, alloc);
            bySpellArray.PushBack(d, alloc);
        }
        obj.AddMember("damageBySpell", bySpellArray, alloc);

        rapidjson::Value takenArray(rapidjson::kArrayType);
        for (const auto& taken : phase.damageTaken) {
            rapidjson::Value t(rapidjson::kObjectType);
            t.AddMember("id", taken.spellId, alloc);
            addJsonInt(t, "hits", taken.hits, alloc);
            addJsonInt(t, "total", taken.total, alloc);
            takenArray.PushBack(t, alloc);
        }
        obj.AddMember("taken", takenArray, alloc);

        rapidjson::Value doneArray(rapidjson::kArrayType);
        for (const auto& done : phase.damageDone) {
            rapidjson::Value d(rapidjson::kObjectType);
            d.AddMember("npc", done.npcId, alloc);
            addJsonInt(d, "total", done.total, alloc);
            addJsonInt(d, "casts", done.casts, alloc);
            doneArray.PushBack(d, alloc);
        }
        obj.AddMember("done", doneArray, alloc);

        phaseArray.PushBack(obj, alloc);
    }
    doc.AddMember("phases", phaseArray, alloc);

    rapidjson::Value bucketArray(rapidjson::kArrayType);
    for (const auto& bucket : timeline) {
        rapidjson::Value b(rapidjson::kObjectType);
        addJsonInt(b, "t", bucket.startMs, alloc);
        addJsonInt(b, "dd", bucket.damageDone, alloc);
        addJsonInt(b, "dt", bucket.damageTaken, alloc);
        addJsonInt(b, "c", bucket.casts, alloc);
        bucketArray.PushBack(b, alloc);
    }
    doc.AddMember("timeline", bucketArray, alloc);

    rapidjson::Value useArray(rapidjson::kArrayType);
    for (const auto& use : cooldownUses) {
        rapidjson::Value u(rapidjson::kObjectType);
        u.AddMember("id", use.spellId, alloc);
        addJsonInt(u, "n", use.useIndex, alloc);
        addJsonInt(u, "t", use.pullOffsetMs, alloc);
        addJsonInt(u, "win", use.windowMs, alloc);
        addJsonInt(u, "dmg", use.damageInWindow, alloc);
        addJsonInt(u, "casts", use.castsInWindow, alloc);

        rapidjson::Value seq(rapidjson::kArrayType);
        for (uint32_t id : use.castSequence) seq.PushBack(id, alloc);
        u.AddMember("seq", seq, alloc);

        // Flat (id, overlap, offset) triples rather than objects: this array
        // runs to a few hundred entries across a pull and the profile is
        // written on every save, so the terser form is worth the small loss
        // in readability. The stride is versioned - see kProfileVersion.
        rapidjson::Value buffs(rapidjson::kArrayType);
        for (const auto& buff : use.buffs) {
            buffs.PushBack(buff.spellId, alloc);
            buffs.PushBack(buff.overlapMs, alloc);
            buffs.PushBack(buff.offsetMs, alloc);
        }
        u.AddMember("buffs", buffs, alloc);

        rapidjson::Value curve(rapidjson::kArrayType);
        for (int64_t v : use.damageCurve) curve.PushBack(v, alloc);
        u.AddMember("curve", curve, alloc);

        rapidjson::Value stacked(rapidjson::kArrayType);
        for (uint32_t id : use.stackedSpellIds) stacked.PushBack(id, alloc);
        u.AddMember("stacked", stacked, alloc);

        // Flat triples: npcId, total, hits. The report card shows where each
        // window's damage went for both sides, so a restored reference has to
        // carry it.
        rapidjson::Value targets(rapidjson::kArrayType);
        for (const auto& target : use.damageByTarget) {
            targets.PushBack(target.npcId, alloc);
            targets.PushBack(target.total, alloc);
            targets.PushBack(target.casts, alloc);
        }
        u.AddMember("targets", targets, alloc);

        useArray.PushBack(u, alloc);
    }
    doc.AddMember("cooldownUses", useArray, alloc);

    rapidjson::Value defArray(rapidjson::kArrayType);
    for (const auto& use : defensiveUses) {
        rapidjson::Value d(rapidjson::kObjectType);
        d.AddMember("id", use.spellId, alloc);
        addJsonInt(d, "t", use.pullOffsetMs, alloc);
        addJsonInt(d, "win", use.windowMs, alloc);
        addJsonInt(d, "dt", use.damageTakenInWindow, alloc);
        defArray.PushBack(d, alloc);
    }
    doc.AddMember("defensiveUses", defArray, alloc);

    addJsonInt(doc, "weightedDamage", weightedDamageDone, alloc);
    addJsonInt(doc, "rawDamage", rawDamageDone, alloc);

    // Flat quintuples rather than objects: a three minute pull is ~180
    // samples and the terser form keeps the file readable by hand.
    rapidjson::Value posArray(rapidjson::kArrayType);
    for (const auto& pos : positions) {
        posArray.PushBack(pos.timeMs, alloc);
        posArray.PushBack(pos.x, alloc);
        posArray.PushBack(pos.y, alloc);
        posArray.PushBack(pos.hasBoss ? pos.bossX : 0.0f, alloc);
        posArray.PushBack(pos.hasBoss ? pos.bossY : 0.0f, alloc);
    }
    doc.AddMember("positions", posArray, alloc);

    rapidjson::Value gapArray(rapidjson::kArrayType);
    for (const auto& gap : downtimeGaps) {
        rapidjson::Value g(rapidjson::kObjectType);
        addJsonInt(g, "t", gap.startMs, alloc);
        addJsonInt(g, "ms", gap.durationMs, alloc);
        g.AddMember("before", gap.beforeSpellId, alloc);
        g.AddMember("after", gap.afterSpellId, alloc);
        addJsonInt(g, "gcd", gap.expectedGcdMs, alloc);
        g.AddMember("cd", gap.inCooldown, alloc);
        g.AddMember("cancelled", gap.cancelledCast, alloc);

        // Flat triples: id, time, occupancy.
        rapidjson::Value lead(rapidjson::kArrayType);
        for (const auto& prior : gap.leadIn) {
            lead.PushBack(prior.spellId, alloc);
            lead.PushBack(prior.timeMs, alloc);
            lead.PushBack(prior.occupancyMs, alloc);
        }
        g.AddMember("lead", lead, alloc);
        gapArray.PushBack(g, alloc);
    }
    doc.AddMember("downtimeGaps", gapArray, alloc);

    rapidjson::Value originObj(rapidjson::kObjectType);
    originObj.AddMember("report", rapidjson::Value(origin.reportCode.c_str(), alloc), alloc);
    originObj.AddMember("log", rapidjson::Value(origin.logPath.c_str(), alloc), alloc);
    addJsonInt(originObj, "fight", origin.fightId, alloc);
    originObj.AddMember("actor", rapidjson::Value(origin.actorGuid.c_str(), alloc), alloc);
    doc.AddMember("origin", originObj, alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    return buffer.GetString();
}

std::optional<MentorProfile> MentorProfile::fromJson(const std::string& json) {
    if (json.empty()) return std::nullopt;

    rapidjson::Document doc;
    if (doc.Parse(json.c_str()).HasParseError() || !doc.IsObject()) {
        return std::nullopt;
    }

    // A profile written by a newer build may have changed what its fields
    // mean, so it is rejected. Older versions are read, with the fields whose
    // layout changed handled per version below.
    const int version = readInt(doc, "version", 0);
    if (version > kProfileVersion) {
        return std::nullopt;
    }

    MentorProfile profile;
    profile.sourceLabel    = readString(doc, "source");
    profile.playerName     = readString(doc, "player");
    profile.actorGuid      = readString(doc, "actor");
    profile.encounterId    = readUint(doc, "encounter");
    profile.specId         = static_cast<uint16_t>(readUint(doc, "spec"));
    profile.difficulty     = readUint(doc, "difficulty");
    profile.pullDurationMs = readInt(doc, "durationMs");
    profile.kill           = doc.HasMember("kill") && doc["kill"].IsBool() && doc["kill"].GetBool();
    profile.bucketMs       = readInt(doc, "bucketMs", 5000);
    if (profile.bucketMs <= 0) profile.bucketMs = 5000;

    if (doc.HasMember("phaseRules") && doc["phaseRules"].IsArray()) {
        for (const auto& v : doc["phaseRules"].GetArray()) {
            if (!v.IsObject()) continue;
            PhaseSettings::PhaseRule rule;
            rule.kind      = ruleKindFromName(readString(v, "kind"));
            rule.spellId   = readUint(v, "spell");
            rule.elapsedMs = readInt(v, "ms");
            rule.emoteText = readString(v, "text");
            rule.label     = readString(v, "label");
            profile.phaseRules.push_back(std::move(rule));
        }
    }

    if (doc.HasMember("majorCooldowns") && doc["majorCooldowns"].IsArray()) {
        for (const auto& v : doc["majorCooldowns"].GetArray()) {
            if (v.IsUint()) profile.majorCooldownSpellIds.push_back(v.GetUint());
        }
    }

    if (doc.HasMember("ignoredSpells") && doc["ignoredSpells"].IsArray()) {
        for (const auto& v : doc["ignoredSpells"].GetArray()) {
            if (v.IsUint()) profile.ignoredSpellIds.push_back(v.GetUint());
        }
    }

    if (doc.HasMember("downtimeGaps") && doc["downtimeGaps"].IsArray()) {
        for (const auto& g : doc["downtimeGaps"].GetArray()) {
            if (!g.IsObject()) continue;
            ProfileDowntimeGap gap;
            gap.startMs = readInt(g, "t");
            gap.durationMs = readInt(g, "ms");
            gap.beforeSpellId = readUint(g, "before");
            gap.afterSpellId = readUint(g, "after");
            gap.expectedGcdMs = readInt(g, "gcd");
            gap.inCooldown = g.HasMember("cd") && g["cd"].IsBool() && g["cd"].GetBool();
            gap.cancelledCast = g.HasMember("cancelled") && g["cancelled"].IsBool() &&
                                g["cancelled"].GetBool();

            if (g.HasMember("lead") && g["lead"].IsArray()) {
                const auto& arr = g["lead"].GetArray();
                for (rapidjson::SizeType i = 0; i + 2 < arr.Size(); i += 3) {
                    ProfileDowntimeGap::PriorCast prior;
                    prior.spellId = arr[i].IsUint() ? arr[i].GetUint() : 0;
                    prior.timeMs = arr[i + 1].IsInt() ? arr[i + 1].GetInt() : 0;
                    prior.occupancyMs = arr[i + 2].IsInt() ? arr[i + 2].GetInt() : 0;
                    if (prior.spellId != 0) gap.leadIn.push_back(prior);
                }
            }
            if (gap.durationMs > 0) profile.downtimeGaps.push_back(gap);
        }
    }

    if (doc.HasMember("positions") && doc["positions"].IsArray()) {
        const auto& arr = doc["positions"].GetArray();
        for (rapidjson::SizeType i = 0; i + 4 < arr.Size(); i += 5) {
            ProfilePosition pos;
            pos.timeMs = arr[i].IsInt() ? arr[i].GetInt() : 0;
            pos.x = arr[i + 1].IsNumber() ? arr[i + 1].GetFloat() : 0.0f;
            pos.y = arr[i + 2].IsNumber() ? arr[i + 2].GetFloat() : 0.0f;
            pos.bossX = arr[i + 3].IsNumber() ? arr[i + 3].GetFloat() : 0.0f;
            pos.bossY = arr[i + 4].IsNumber() ? arr[i + 4].GetFloat() : 0.0f;
            pos.hasBoss = (pos.bossX != 0.0f || pos.bossY != 0.0f);
            profile.positions.push_back(pos);
        }
    }

    if (doc.HasMember("mobWeights") && doc["mobWeights"].IsUint64()) {
        profile.mobWeightFingerprint = doc["mobWeights"].GetUint64();
    }

    if (doc.HasMember("ignoredAuras") && doc["ignoredAuras"].IsArray()) {
        for (const auto& v : doc["ignoredAuras"].GetArray()) {
            if (v.IsUint()) profile.ignoredAuraIds.push_back(v.GetUint());
        }
    }

    if (doc.HasMember("burstWindowSpells") && doc["burstWindowSpells"].IsArray()) {
        for (const auto& v : doc["burstWindowSpells"].GetArray()) {
            if (v.IsUint()) profile.burstWindowSpellIds.push_back(v.GetUint());
        }
    }

    if (doc.HasMember("buffOnlyCooldowns") && doc["buffOnlyCooldowns"].IsArray()) {
        for (const auto& v : doc["buffOnlyCooldowns"].GetArray()) {
            if (v.IsUint()) profile.buffOnlyCooldownIds.push_back(v.GetUint());
        }
    }

    if (doc.HasMember("origin") && doc["origin"].IsObject()) {
        const auto& o = doc["origin"];
        profile.origin.reportCode = readString(o, "report");
        profile.origin.logPath    = readString(o, "log");
        profile.origin.fightId    = readInt(o, "fight");
        profile.origin.actorGuid  = readString(o, "actor");
    }

    if (doc.HasMember("offGlobalSpells") && doc["offGlobalSpells"].IsArray()) {
        for (const auto& v : doc["offGlobalSpells"].GetArray()) {
            if (v.IsUint()) profile.offGlobalSpellIds.push_back(v.GetUint());
        }
    }

    if (doc.HasMember("unresolvedPhases") && doc["unresolvedPhases"].IsArray()) {
        for (const auto& v : doc["unresolvedPhases"].GetArray()) {
            if (v.IsString()) profile.unresolvedPhaseLabels.emplace_back(v.GetString());
        }
    }

    if (doc.HasMember("phases") && doc["phases"].IsArray()) {
        for (const auto& v : doc["phases"].GetArray()) {
            if (!v.IsObject()) continue;
            ProfilePhase phase;
            phase.label            = readString(v, "label");
            phase.durationMs       = readInt(v, "ms");
            phase.totalDamageDone  = readInt64(v, "dmgDone");
            phase.totalDamageTaken = readInt64(v, "dmgTaken");
            phase.deaths           = readInt(v, "deaths");
            if (v.HasMember("burst"))  phase.inCooldowns      = splitFromJson(v["burst"]);
            if (v.HasMember("filler")) phase.outsideCooldowns = splitFromJson(v["filler"]);

            if (v.HasMember("spells") && v["spells"].IsArray()) {
                for (const auto& s : v["spells"].GetArray()) {
                    if (!s.IsObject()) continue;
                    ProfileSpellUsage spell;
                    spell.spellId           = readUint(s, "id");
                    spell.casts             = readInt(s, "casts");
                    spell.castsPerMinute    = s.HasMember("cpm") && s["cpm"].IsNumber()
                                                  ? static_cast<float>(s["cpm"].GetDouble())
                                                  : 0.0f;
                    spell.firstCastOffsetMs = readInt(s, "first", -1);
                    spell.isCooldown        = s.HasMember("cd") && s["cd"].IsBool() && s["cd"].GetBool();
                    spell.damage            = readInt64(s, "dmg");
                    spell.castTimeMs        = readInt(s, "ct");
                    if (spell.spellId != 0) phase.spells.push_back(spell);
                }
            }

            if (v.HasMember("damageBySpell") && v["damageBySpell"].IsArray()) {
                for (const auto& d : v["damageBySpell"].GetArray()) {
                    if (!d.IsObject()) continue;
                    ProfileDamageBySpell src;
                    src.spellId = readUint(d, "id");
                    src.total = readInt64(d, "dmg");
                    src.hits = readInt(d, "hits");
                    if (src.spellId != 0) phase.damageBySpell.push_back(src);
                }
            }

            if (v.HasMember("taken") && v["taken"].IsArray()) {
                for (const auto& t : v["taken"].GetArray()) {
                    if (!t.IsObject()) continue;
                    ProfileDamageTaken taken;
                    taken.spellId = readUint(t, "id");
                    taken.hits    = readInt(t, "hits");
                    taken.total   = readInt64(t, "total");
                    phase.damageTaken.push_back(taken);
                }
            }

            if (v.HasMember("done") && v["done"].IsArray()) {
                for (const auto& d : v["done"].GetArray()) {
                    if (!d.IsObject()) continue;
                    ProfileDamageDone done;
                    done.npcId = readUint(d, "npc");
                    done.total = readInt64(d, "total");
                    done.casts = readInt(d, "casts");
                    phase.damageDone.push_back(done);
                }
            }

            profile.phases.push_back(std::move(phase));
        }
    }

    if (doc.HasMember("timeline") && doc["timeline"].IsArray()) {
        for (const auto& v : doc["timeline"].GetArray()) {
            if (!v.IsObject()) continue;
            ProfileTimelineBucket bucket;
            bucket.startMs     = readInt(v, "t");
            bucket.damageDone  = readInt64(v, "dd");
            bucket.damageTaken = readInt64(v, "dt");
            bucket.casts       = readInt(v, "c");
            profile.timeline.push_back(bucket);
        }
    }

    if (doc.HasMember("cooldownUses") && doc["cooldownUses"].IsArray()) {
        for (const auto& v : doc["cooldownUses"].GetArray()) {
            if (!v.IsObject()) continue;
            ProfileCooldownUse use;
            use.spellId        = readUint(v, "id");
            use.useIndex       = readInt(v, "n");
            use.pullOffsetMs   = readInt(v, "t");
            use.windowMs       = readInt(v, "win");
            use.damageInWindow = readInt64(v, "dmg");
            use.castsInWindow  = readInt(v, "casts");

            if (v.HasMember("seq") && v["seq"].IsArray()) {
                for (const auto& id : v["seq"].GetArray()) {
                    if (id.IsUint()) use.castSequence.push_back(id.GetUint());
                }
            }

            if (v.HasMember("buffs") && v["buffs"].IsArray()) {
                const auto& arr = v["buffs"].GetArray();
                // Version 1 wrote (id, overlap) pairs; version 2 writes
                // (id, overlap, offset) triples. The stride has to follow the
                // version, or a v1 array reads the next buff's spell id as an
                // offset. A short tail means a truncated write - stopping one
                // entry early beats misreading it.
                const rapidjson::SizeType stride = version >= 2 ? 3 : 2;
                for (rapidjson::SizeType i = 0;
                     i + stride <= arr.Size(); i += stride) {
                    if (!arr[i].IsUint() || !arr[i + 1].IsInt()) continue;
                    ProfileCooldownUse::WindowBuff buff;
                    buff.spellId = arr[i].GetUint();
                    buff.overlapMs = arr[i + 1].GetInt();
                    if (stride == 3) {
                        if (!arr[i + 2].IsInt()) continue;
                        buff.offsetMs = arr[i + 2].GetInt();
                    }
                    use.buffs.push_back(buff);
                }
            }

            if (v.HasMember("curve") && v["curve"].IsArray()) {
                for (const auto& c : v["curve"].GetArray()) {
                    if (c.IsInt64()) use.damageCurve.push_back(c.GetInt64());
                }
            }

            if (v.HasMember("stacked") && v["stacked"].IsArray()) {
                for (const auto& id : v["stacked"].GetArray()) {
                    if (id.IsUint()) use.stackedSpellIds.push_back(id.GetUint());
                }
            }

            if (v.HasMember("targets") && v["targets"].IsArray()) {
                const auto& arr = v["targets"].GetArray();
                // Triples: npcId, total, hits.
                for (rapidjson::SizeType i = 0; i + 3 <= arr.Size(); i += 3) {
                    if (!arr[i].IsUint() || !arr[i + 1].IsInt64() ||
                        !arr[i + 2].IsInt()) {
                        continue;
                    }
                    ProfileDamageDone target;
                    target.npcId = arr[i].GetUint();
                    target.total = arr[i + 1].GetInt64();
                    target.casts = arr[i + 2].GetInt();
                    use.damageByTarget.push_back(target);
                }
            }

            if (use.spellId != 0) profile.cooldownUses.push_back(use);
        }
    }

    if (doc.HasMember("defensiveUses") && doc["defensiveUses"].IsArray()) {
        for (const auto& v : doc["defensiveUses"].GetArray()) {
            if (!v.IsObject()) continue;
            ProfileDefensiveUse use;
            use.spellId             = readUint(v, "id");
            use.pullOffsetMs        = readInt(v, "t");
            use.windowMs            = readInt(v, "win");
            use.damageTakenInWindow = readInt64(v, "dt");
            if (use.spellId != 0) profile.defensiveUses.push_back(use);
        }
    }

    profile.weightedDamageDone = readInt64(doc, "weightedDamage");
    profile.rawDamageDone      = readInt64(doc, "rawDamage");

    // Both are relied on downstream to be ordered; a hand-edited or
    // re-serialized profile is not guaranteed to be.
    std::sort(profile.timeline.begin(), profile.timeline.end(),
              [](const ProfileTimelineBucket& a, const ProfileTimelineBucket& b) {
                  return a.startMs < b.startMs;
              });
    std::sort(profile.cooldownUses.begin(), profile.cooldownUses.end(),
              [](const ProfileCooldownUse& a, const ProfileCooldownUse& b) {
                  return a.pullOffsetMs < b.pullOffsetMs;
              });

    if (!profile.isValid()) return std::nullopt;
    return profile;
}

} // namespace benchmark
