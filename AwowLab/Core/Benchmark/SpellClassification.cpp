#include "Core/Benchmark/SpellClassification.h"
#include "Core/Benchmark/BenchmarkStore.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <charconv>

namespace benchmark {

namespace {

const char* castRoleName(CastRole role) {
    switch (role) {
        case CastRole::Filler:          return "filler";
        case CastRole::ProcConsumer:    return "proc";
        case CastRole::ResourceSpender: return "spender";
        case CastRole::MajorCooldown:   return "major";
        case CastRole::MinorCooldown:   return "minor";
        case CastRole::Defensive:       return "defensive";
        case CastRole::Utility:         return "utility";
        case CastRole::Ignored:         return "ignored";
        case CastRole::Trinket:         return "trinket";
        case CastRole::Cantrip:         return "cantrip";
        case CastRole::Melee:           return "melee";
        case CastRole::Unclassified:
        default:                        return "none";
    }
}

CastRole castRoleFromName(std::string_view name) {
    if (name == "filler")    return CastRole::Filler;
    if (name == "proc")      return CastRole::ProcConsumer;
    if (name == "spender")   return CastRole::ResourceSpender;
    if (name == "major")     return CastRole::MajorCooldown;
    if (name == "minor")     return CastRole::MinorCooldown;
    if (name == "defensive") return CastRole::Defensive;
    if (name == "utility")   return CastRole::Utility;
    if (name == "ignored")   return CastRole::Ignored;
    if (name == "trinket")   return CastRole::Trinket;
    if (name == "cantrip")   return CastRole::Cantrip;
    if (name == "melee")     return CastRole::Melee;
    return CastRole::Unclassified;
}

const char* auraRoleName(AuraRole role) {
    switch (role) {
        case AuraRole::Proc:         return "proc";
        case AuraRole::Maintenance:  return "maintenance";
        case AuraRole::CooldownBuff: return "cdbuff";
        case AuraRole::Unimportant:  return "unimportant";
        case AuraRole::MajorCooldown: return "auramajor";
        case AuraRole::Channel:       return "channel";
        case AuraRole::Unclassified:
        default:                     return "none";
    }
}

AuraRole auraRoleFromName(std::string_view name) {
    if (name == "proc")        return AuraRole::Proc;
    if (name == "maintenance") return AuraRole::Maintenance;
    if (name == "cdbuff")      return AuraRole::CooldownBuff;
    if (name == "auramajor")   return AuraRole::MajorCooldown;
    if (name == "channel")     return AuraRole::Channel;
    if (name == "unimportant") return AuraRole::Unimportant;
    return AuraRole::Unclassified;
}

const char* basisName(SuggestionBasis basis) {
    switch (basis) {
        case SuggestionBasis::CadenceMajor:  return "major";
        case SuggestionBasis::CadenceMinor:  return "minor";
        case SuggestionBasis::CadenceFiller: return "filler";
        case SuggestionBasis::Irregular:     return "irregular";
        case SuggestionBasis::SingleUse:     return "single";
        case SuggestionBasis::AuraSelfBuff:  return "selfbuff";
        case SuggestionBasis::None:
        default:                             return "none";
    }
}

SuggestionBasis basisFromName(std::string_view name) {
    if (name == "major")     return SuggestionBasis::CadenceMajor;
    if (name == "minor")     return SuggestionBasis::CadenceMinor;
    if (name == "filler")    return SuggestionBasis::CadenceFiller;
    if (name == "irregular") return SuggestionBasis::Irregular;
    if (name == "single")    return SuggestionBasis::SingleUse;
    if (name == "selfbuff")  return SuggestionBasis::AuraSelfBuff;
    return SuggestionBasis::None;
}

std::string readString(const rapidjson::Value& obj, const char* name) {
    if (!obj.HasMember(name)) return {};
    const auto& v = obj[name];
    return v.IsString() ? std::string(v.GetString(), v.GetStringLength()) : std::string{};
}

int32_t readInt(const rapidjson::Value& obj, const char* name, int32_t fallback = 0) {
    if (!obj.HasMember(name)) return fallback;
    const auto& v = obj[name];
    return v.IsInt() ? v.GetInt() : fallback;
}

uint32_t readUint(const rapidjson::Value& obj, const char* name, uint32_t fallback = 0) {
    if (!obj.HasMember(name)) return fallback;
    const auto& v = obj[name];
    return v.IsUint() ? v.GetUint() : fallback;
}

float readFloat(const rapidjson::Value& obj, const char* name, float fallback = 0.0f) {
    if (!obj.HasMember(name)) return fallback;
    const auto& v = obj[name];
    return v.IsNumber() ? static_cast<float>(v.GetDouble()) : fallback;
}

bool readBool(const rapidjson::Value& obj, const char* name) {
    return obj.HasMember(name) && obj[name].IsBool() && obj[name].GetBool();
}

} // namespace

std::vector<uint32_t> SpecClassification::majorCooldownSpellIds() const {
    std::vector<uint32_t> ids;
    for (const auto& [id, spell] : spells) {
        // Only confirmed entries count. A suggestion that happens to say
        // "major cooldown" has not been agreed to yet, and letting it define
        // the burst window would shape the whole comparison off a guess.
        if (spell.confirmed && spell.role == CastRole::MajorCooldown) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<uint32_t> SpecClassification::burstWindowSpellIds() const {
    std::vector<uint32_t> ids;
    for (const auto& [id, spell] : spells) {
        if (!spell.confirmed) continue;
        if (spell.role == CastRole::MajorCooldown ||
            spell.role == CastRole::Trinket) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<uint32_t> SpecClassification::buffOnlyCooldownIds() const {
    std::vector<uint32_t> ids;
    for (const auto& [id, aura] : auras) {
        if (!aura.confirmed || aura.role != AuraRole::MajorCooldown) continue;
        // A cast of the same id already produces windows; taking the buff as
        // well would count the same window twice.
        if (spells.count(id)) continue;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<uint32_t> SpecClassification::channelAuraIds() const {
    std::vector<uint32_t> ids;
    for (const auto& [id, aura] : auras) {
        if (aura.confirmed && aura.role == AuraRole::Channel) ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<uint32_t> SpecClassification::defensiveSpellIds() const {
    std::vector<uint32_t> ids;
    for (const auto& [id, spell] : spells) {
        if (spell.confirmed && spell.role == CastRole::Defensive) ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<uint32_t> SpecClassification::ignoredSpellIds() const {
    std::vector<uint32_t> ids;
    for (const auto& [id, spell] : spells) {
        // Only confirmed rulings count. A suggestion of "ignored" is still a
        // guess, and silently dropping a spell on a guess would remove it from
        // every number without the user ever agreeing to that.
        if (spell.confirmed && spell.role == CastRole::Ignored) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

uint32_t SpecClassification::windowAuraFor(uint32_t spellId) const {
    const auto* spell = findSpell(spellId);
    if (!spell) return 0;
    if (spell->windowAuraSpellId != 0) return spell->windowAuraSpellId;
    return auras.count(spellId) ? spellId : 0;
}

std::vector<uint32_t> SpecClassification::offGlobalSpellIds() const {
    std::vector<uint32_t> ids;
    for (const auto& [id, spell] : spells) {
        // Confirmed only, on the same reasoning as the lists above: dropping a
        // spell out of the globals count on a guess would change a number the
        // user never agreed to change.
        if (spell.confirmed &&
            (spell.role == CastRole::Cantrip || spell.role == CastRole::Melee)) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<uint32_t> SpecClassification::ignoredAuraIds() const {
    std::vector<uint32_t> ids;
    for (const auto& [id, aura] : auras) {
        if (aura.confirmed && aura.role == AuraRole::Unimportant) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

ClassificationStore& ClassificationStore::instance() {
    static ClassificationStore store;
    return store;
}

SpecClassification* ClassificationStore::find(uint16_t specId) {
    auto it = specs_.find(specId);
    return it != specs_.end() ? &it->second : nullptr;
}

const SpecClassification* ClassificationStore::find(uint16_t specId) const {
    auto it = specs_.find(specId);
    return it != specs_.end() ? &it->second : nullptr;
}

SpecClassification& ClassificationStore::getOrCreate(uint16_t specId, const std::string& specName) {
    auto it = specs_.find(specId);
    if (it != specs_.end()) {
        if (it->second.specName.empty() && !specName.empty()) {
            it->second.specName = specName;
        }
        return it->second;
    }
    SpecClassification spec;
    spec.specId = specId;
    spec.specName = specName;
    return specs_.emplace(specId, std::move(spec)).first->second;
}

size_t ClassificationStore::mergeSuggestions(const SpecClassification& suggested) {
    SpecClassification& target = getOrCreate(suggested.specId, suggested.specName);
    size_t newEntries = 0;

    // Track the version the classifications were last seen under. Roles drift
    // across patches - an ability reworked into a cooldown keeps its id - and
    // without this there is nothing to tell the user their rulings are stale.
    if (!suggested.gameVersion.empty()) {
        target.gameVersion = suggested.gameVersion;
    }

    for (const auto& [id, incoming] : suggested.spells) {
        auto existing = target.spells.find(id);
        if (existing != target.spells.end()) {
            // The user's ruling outranks any later heuristic. Refresh only the
            // evidence, so the review UI shows what this log actually did
            // without reopening a decision that has already been made.
            if (existing->second.confirmed) {
                existing->second.castsPerMinute = incoming.castsPerMinute;
                existing->second.observedCasts  = incoming.observedCasts;
                existing->second.medianGapMs    = incoming.medianGapMs;
                if (existing->second.name.empty()) existing->second.name = incoming.name;
                continue;
            }
            // Still unconfirmed: take the fresher suggestion, since it was
            // built from the log the user is looking at right now.
            existing->second = incoming;
            continue;
        }
        target.spells[id] = incoming;
        ++newEntries;
    }

    for (const auto& [id, incoming] : suggested.auras) {
        auto existing = target.auras.find(id);
        if (existing != target.auras.end()) {
            if (existing->second.confirmed) {
                existing->second.applications = incoming.applications;
                existing->second.totalUptimeMs = incoming.totalUptimeMs;
                existing->second.uptimeShare = incoming.uptimeShare;
                if (existing->second.name.empty()) existing->second.name = incoming.name;
                continue;
            }
            existing->second = incoming;
            continue;
        }
        target.auras[id] = incoming;
        ++newEntries;
    }

    rebuildPending(target);
    return newEntries;
}

void ClassificationStore::rebuildPending(SpecClassification& spec) {
    spec.pendingSpellIds.clear();
    spec.pendingAuraIds.clear();

    for (const auto& [id, spell] : spec.spells) {
        if (!spell.confirmed) spec.pendingSpellIds.push_back(id);
    }
    for (const auto& [id, aura] : spec.auras) {
        if (!aura.confirmed) spec.pendingAuraIds.push_back(id);
    }

    // Weakest suggestions first: those are the ones most likely to be wrong,
    // and the user's attention is worth more at the top of a list than the
    // bottom.
    std::sort(spec.pendingSpellIds.begin(), spec.pendingSpellIds.end(),
              [&spec](uint32_t a, uint32_t b) {
                  return spec.spells.at(a).suggestionConfidence <
                         spec.spells.at(b).suggestionConfidence;
              });
    std::sort(spec.pendingAuraIds.begin(), spec.pendingAuraIds.end(),
              [&spec](uint32_t a, uint32_t b) {
                  return spec.auras.at(a).suggestionConfidence <
                         spec.auras.at(b).suggestionConfidence;
              });
}

void ClassificationStore::confirmSpell(uint16_t specId, uint32_t spellId, CastRole role) {
    SpecClassification* spec = find(specId);
    if (!spec) return;
    auto it = spec->spells.find(spellId);
    if (it == spec->spells.end()) return;
    it->second.role = role;
    it->second.confirmed = true;
    rebuildPending(*spec);
    save();
}

void ClassificationStore::save() const {
    if (batchDepth_ > 0) {
        savePending_ = true;
        return;
    }
    savePending_ = false;
    store::writeFile(store::classificationsPath(), toJson());
}

ClassificationStore::BatchGuard::BatchGuard(ClassificationStore& store)
    : store_(store) {
    ++store_.batchDepth_;
}

ClassificationStore::BatchGuard::~BatchGuard() {
    if (--store_.batchDepth_ > 0) return;
    // Only writes if something actually changed, so opening a batch and
    // ruling on nothing does not rewrite the file.
    if (store_.savePending_) store_.save();
}

void ClassificationStore::confirmAura(uint16_t specId, uint32_t auraId, AuraRole role) {
    SpecClassification* spec = find(specId);
    if (!spec) return;
    auto it = spec->auras.find(auraId);
    if (it == spec->auras.end()) return;
    it->second.role = role;
    it->second.confirmed = true;
    rebuildPending(*spec);
    save();
}

void ClassificationStore::ruleUnlistedAura(uint16_t specId, uint32_t auraId,
                                          AuraRole role) {
    if (auraId == 0) return;

    SpecClassification* spec = find(specId);
    if (!spec) return;

    // Created on demand. An external the suggester skipped has no entry to
    // update, so the ruling itself is what brings it into existence - and it
    // arrives confirmed, because the user just made the decision by hand.
    auto& aura = spec->auras[auraId];
    aura.spellId = auraId;
    aura.role = role;
    aura.confirmed = true;
    rebuildPending(*spec);
    save();
}

void ClassificationStore::confirmAllSuggested(uint16_t specId) {
    SpecClassification* spec = find(specId);
    if (!spec) return;
    for (auto& [id, spell] : spec->spells) spell.confirmed = true;
    for (auto& [id, aura] : spec->auras)   aura.confirmed = true;
    rebuildPending(*spec);
    save();
}

std::string ClassificationStore::toJson() const {
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    for (const auto& [specId, spec] : specs_) {
        rapidjson::Value specObj(rapidjson::kObjectType);
        specObj.AddMember("name", rapidjson::Value(spec.specName.c_str(), alloc), alloc);
        specObj.AddMember("gameVersion", rapidjson::Value(spec.gameVersion.c_str(), alloc), alloc);

        rapidjson::Value spellArray(rapidjson::kArrayType);
        for (const auto& [id, spell] : spec.spells) {
            rapidjson::Value obj(rapidjson::kObjectType);
            obj.AddMember("id", id, alloc);
            obj.AddMember("name", rapidjson::Value(spell.name.c_str(), alloc), alloc);
            obj.AddMember("role", rapidjson::Value(castRoleName(spell.role), alloc), alloc);
            obj.AddMember("confirmed", spell.confirmed, alloc);
            obj.AddMember("basis", rapidjson::Value(basisName(spell.basis), alloc), alloc);
            obj.AddMember("confidence", spell.suggestionConfidence, alloc);
            obj.AddMember("cpm", spell.castsPerMinute, alloc);
            obj.AddMember("casts", spell.observedCasts, alloc);
            obj.AddMember("gap", spell.medianGapMs, alloc);
            obj.AddMember("windowAura", spell.windowAuraSpellId, alloc);
            obj.AddMember("windowMs", spell.assumedWindowMs, alloc);
            spellArray.PushBack(obj, alloc);
        }
        specObj.AddMember("spells", spellArray, alloc);

        rapidjson::Value auraArray(rapidjson::kArrayType);
        for (const auto& [id, aura] : spec.auras) {
            rapidjson::Value obj(rapidjson::kObjectType);
            obj.AddMember("id", id, alloc);
            obj.AddMember("name", rapidjson::Value(aura.name.c_str(), alloc), alloc);
            obj.AddMember("role", rapidjson::Value(auraRoleName(aura.role), alloc), alloc);
            obj.AddMember("confirmed", aura.confirmed, alloc);
            obj.AddMember("basis", rapidjson::Value(basisName(aura.basis), alloc), alloc);
            obj.AddMember("confidence", aura.suggestionConfidence, alloc);
            obj.AddMember("applications", aura.applications, alloc);
            obj.AddMember("uptimeMs", aura.totalUptimeMs, alloc);
            obj.AddMember("uptimeShare", aura.uptimeShare, alloc);
            obj.AddMember("consumedBy", aura.consumedBySpellId, alloc);
            auraArray.PushBack(obj, alloc);
        }
        specObj.AddMember("auras", auraArray, alloc);

        std::string key = std::to_string(specId);
        doc.AddMember(rapidjson::Value(key.c_str(), alloc), specObj, alloc);
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    return buffer.GetString();
}

void ClassificationStore::fromJson(const std::string& json) {
    specs_.clear();
    if (json.empty()) return;

    rapidjson::Document doc;
    if (doc.Parse(json.c_str()).HasParseError() || !doc.IsObject()) {
        return;
    }

    for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
        if (!it->value.IsObject()) continue;

        uint32_t specId = 0;
        std::string_view key(it->name.GetString(), it->name.GetStringLength());
        std::from_chars(key.data(), key.data() + key.size(), specId);
        if (specId == 0) continue;

        SpecClassification spec;
        spec.specId      = static_cast<uint16_t>(specId);
        spec.specName    = readString(it->value, "name");
        spec.gameVersion = readString(it->value, "gameVersion");

        if (it->value.HasMember("spells") && it->value["spells"].IsArray()) {
            for (const auto& v : it->value["spells"].GetArray()) {
                if (!v.IsObject()) continue;
                SpellClass spell;
                spell.spellId              = readUint(v, "id");
                if (spell.spellId == 0) continue;
                spell.name                 = readString(v, "name");
                spell.role                 = castRoleFromName(readString(v, "role"));
                spell.confirmed            = readBool(v, "confirmed");
                spell.basis                = basisFromName(readString(v, "basis"));
                spell.suggestionConfidence = readFloat(v, "confidence");
                spell.castsPerMinute       = readFloat(v, "cpm");
                spell.observedCasts        = readInt(v, "casts");
                spell.medianGapMs          = readInt(v, "gap");
                spell.windowAuraSpellId    = readUint(v, "windowAura");
                spell.assumedWindowMs      = readInt(v, "windowMs");
                spec.spells[spell.spellId] = std::move(spell);
            }
        }

        if (it->value.HasMember("auras") && it->value["auras"].IsArray()) {
            for (const auto& v : it->value["auras"].GetArray()) {
                if (!v.IsObject()) continue;
                AuraClass aura;
                aura.spellId              = readUint(v, "id");
                if (aura.spellId == 0) continue;
                aura.name                 = readString(v, "name");
                aura.role                 = auraRoleFromName(readString(v, "role"));
                aura.confirmed            = readBool(v, "confirmed");
                aura.basis                = basisFromName(readString(v, "basis"));
                aura.suggestionConfidence = readFloat(v, "confidence");
                aura.applications         = readInt(v, "applications");
                aura.totalUptimeMs        = readInt(v, "uptimeMs");
                aura.uptimeShare          = readFloat(v, "uptimeShare");
                aura.consumedBySpellId    = readUint(v, "consumedBy");
                spec.auras[aura.spellId]  = std::move(aura);
            }
        }

        rebuildPending(spec);
        specs_[spec.specId] = std::move(spec);
    }
}

} // namespace benchmark
