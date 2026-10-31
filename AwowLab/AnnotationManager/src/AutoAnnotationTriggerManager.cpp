#include "AutoAnnotationTriggerManager.h"
#include "TriggerEvaluators/SpellCastTriggerEvaluator.h"
#include "TriggerEvaluators/DamageTriggerEvaluator.h"
#include "TriggerEvaluators/DamageTakenTriggerEvaluator.h"
#include "TriggerEvaluators/AuraTriggerEvaluator.h"
#include "TriggerEvaluators/ActorTriggerEvaluator.h"
#include "Database/SpellCastDatabase.h"
#include "Database/CombatDatabase.h"
#include "Database/AuraDatabase.h"
#include "Database/EventDatabase.h"
#include "Database/DeathDatabase.h"
#include "Core/StringInterner.h"
#include "Core/CurrentEncounter.h"
#include "Core/ErrorLogger.h"
#include "AssetResolver.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <filesystem>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>

namespace {

constexpr float kHalfPi = 1.5707963f;
constexpr float kFacingUnitsPerRadian = 10000.0f;

// Key offset for non-chained events, so they never collide with chain IDs
constexpr uint64_t kNonChainKeyOffset = 0x8000000000000000ULL;

// When a cached event stops showing, before death or negation cut it short.
int32_t naturalHideTime(const CachedAnnotationEvent& event, const AutoAnnotationTrigger& trigger) {
    if (event.isDuringDebuffSample) {
        // No overlap needed between samples; the persistent annotation covers the transition.
        return event.timestamp_ms + event.sampleDurationMs;
    }
    if (trigger.durationType == AnnotationDuration::Encounter) {
        return INT32_MAX;
    }
    return event.timestamp_ms + trigger.durationMs;
}

// Facing is stored as radians * 10000 and points along (cos, sin).
float facingRadians(const CachedAnnotationEvent& event, const AutoAnnotationTrigger& trigger) {
    return static_cast<float>(event.anchorFacing) / kFacingUnitsPerRadian +
           trigger.motion.facingOffset_deg * kAnnotationDegToRad;
}

// The model matrix maps local Z to (-sin(rotation), cos(rotation)), so
// rotation = facing - pi/2 lines the shape up with the facing.
float rotationForFacing(float facing) {
    return facing - kHalfPi;
}

// --- Trigger file field names -------------------------------------------

template <typename Enum>
struct EnumName {
    Enum value;
    const char* name;
};

constexpr EnumName<TriggerTiming> kTimingNames[] = {
    {TriggerTiming::OnCastSuccess, "OnCastSuccess"},
    {TriggerTiming::OnCastStart, "OnCastStart"},
    {TriggerTiming::DuringCast, "DuringCast"},
    {TriggerTiming::OnDamage, "OnDamage"},
    {TriggerTiming::OnDamageTaken, "OnDamageTaken"},
    {TriggerTiming::OnDebuffApplied, "OnDebuffApplied"},
    {TriggerTiming::DuringDebuff, "DuringDebuff"},
    {TriggerTiming::OnDebuffEnd, "OnDebuffEnd"},
    {TriggerTiming::Fixate, "Fixate"},
    {TriggerTiming::Tether, "Tether"},
    {TriggerTiming::OnBuffApplied, "OnBuffApplied"},
    {TriggerTiming::DuringBuff, "DuringBuff"},
    {TriggerTiming::OnBuffEnd, "OnBuffEnd"},
    {TriggerTiming::ActorFollow, "ActorFollow"},
    {TriggerTiming::ActorTrail, "ActorTrail"},
    {TriggerTiming::ActorSpawn, "ActorSpawn"},
    {TriggerTiming::ActorDeath, "ActorDeath"},
};

constexpr EnumName<TriggerAnchor> kAnchorNames[] = {
    {TriggerAnchor::Caster, "Caster"},
    {TriggerAnchor::Target, "Target"},
    {TriggerAnchor::CasterFacing, "CasterFacing"},
    {TriggerAnchor::TargetFacing, "TargetFacing"},
    {TriggerAnchor::SourceToTarget, "SourceToTarget"},
};

constexpr EnumName<ActorDeathBehavior> kDeathBehaviorNames[] = {
    {ActorDeathBehavior::DisappearImmediately, "DisappearImmediately"},
    {ActorDeathBehavior::PersistAtDeathSpot, "PersistAtDeathSpot"},
    {ActorDeathBehavior::FadeOverTime, "FadeOverTime"},
};

constexpr EnumName<SourceFilter> kSourceFilterNames[] = {
    {SourceFilter::Any, "Any"},
    {SourceFilter::SpecificCreature, "SpecificCreature"},
    {SourceFilter::EnvironmentOnly, "EnvironmentOnly"},
};

template <typename Enum, size_t N>
const char* enumName(const EnumName<Enum> (&names)[N], Enum value, const char* fallback) {
    for (const auto& entry : names) {
        if (entry.value == value) return entry.name;
    }
    return fallback;
}

// Leaves out untouched when the name is unknown.
template <typename Enum, size_t N>
bool enumFromName(const EnumName<Enum> (&names)[N], const char* name, Enum& out) {
    for (const auto& entry : names) {
        if (std::strcmp(entry.name, name) == 0) {
            out = entry.value;
            return true;
        }
    }
    return false;
}

// --- Reading -----------------------------------------------------------
// Trigger files are shared between players, so every field is optional and a
// field of the wrong type is ignored rather than trusted.

using JsonValue = rapidjson::Value;

const JsonValue* findMember(const JsonValue& obj, const char* key) {
    auto it = obj.FindMember(key);
    return it != obj.MemberEnd() ? &it->value : nullptr;
}

const char* readString(const JsonValue& obj, const char* key) {
    const JsonValue* value = findMember(obj, key);
    return (value && value->IsString()) ? value->GetString() : nullptr;
}

void readString(const JsonValue& obj, const char* key, std::string& out) {
    if (const char* value = readString(obj, key)) out = value;
}

void readBool(const JsonValue& obj, const char* key, bool& out) {
    const JsonValue* value = findMember(obj, key);
    if (value && value->IsBool()) out = value->GetBool();
}

void readFloat(const JsonValue& obj, const char* key, float& out) {
    const JsonValue* value = findMember(obj, key);
    if (value && value->IsNumber()) out = value->GetFloat();
}

void readInt(const JsonValue& obj, const char* key, int32_t& out) {
    const JsonValue* value = findMember(obj, key);
    if (value && value->IsInt()) out = value->GetInt();
}

void readUint8(const JsonValue& obj, const char* key, uint8_t& out) {
    const JsonValue* value = findMember(obj, key);
    if (value && value->IsUint()) out = static_cast<uint8_t>(value->GetUint());
}

// Non-numeric entries keep their current value.
void readColor(const JsonValue& obj, const char* key, std::array<float, 4>& out) {
    const JsonValue* array = findMember(obj, key);
    if (!array || !array->IsArray()) return;
    for (rapidjson::SizeType i = 0; i < array->Size() && i < out.size(); ++i) {
        if ((*array)[i].IsNumber()) out[i] = (*array)[i].GetFloat();
    }
}

template <typename Enum, size_t N>
void readEnum(const JsonValue& obj, const char* key, const EnumName<Enum> (&names)[N], Enum& out) {
    if (const char* name = readString(obj, key)) enumFromName(names, name, out);
}

void readStackColors(const JsonValue& obj, AutoAnnotationTrigger& trigger) {
    const JsonValue* countValue = findMember(obj, "stackColorCount");
    if (countValue && countValue->IsUint()) {
        // The count indexes a fixed four-entry array
        trigger.stackColorCount = static_cast<uint8_t>(std::min<unsigned>(
            countValue->GetUint(), static_cast<unsigned>(trigger.stackColors.size())));
    }

    const JsonValue* entries = findMember(obj, "stackColors");
    if (!entries || !entries->IsArray()) return;

    for (rapidjson::SizeType i = 0; i < entries->Size() && i < trigger.stackColors.size(); ++i) {
        const JsonValue& entry = (*entries)[i];
        if (!entry.IsObject()) continue;
        readUint8(entry, "stackCount", trigger.stackColors[i].stackCount);
        readColor(entry, "color", trigger.stackColors[i].color);
    }

    // getColorForStacks expects descending stackCount order.
    if (trigger.stackColorCount > 1) {
        std::sort(trigger.stackColors.begin(),
                  trigger.stackColors.begin() + trigger.stackColorCount,
                  [](const auto& a, const auto& b) {
                      return a.stackCount > b.stackCount;
                  });
    }
}

// Every field except the id, which the two callers treat differently.
void readTriggerFields(const JsonValue& obj, AutoAnnotationTrigger& trigger) {
    if (const char* category = readString(obj, "category")) {
        trigger.category = (std::strcmp(category, "Actor") == 0) ? TriggerCategory::Actor : TriggerCategory::Spell;
    }

    if (const JsonValue* spellId = findMember(obj, "spell_id"); spellId && spellId->IsUint()) {
        trigger.spell_id = spellId->GetUint();
    }
    readString(obj, "creature_guid", trigger.creature_guid);
    readString(obj, "creature_name", trigger.creature_name);
    readString(obj, "actorNameFilter", trigger.actorNameFilter);

    if (const JsonValue* interval = findMember(obj, "trailIntervalMs"); interval && interval->IsInt()) {
        trigger.trailIntervalMs = std::max(interval->GetInt(), kMinTrailIntervalMs);
    }
    readInt(obj, "castExtendMs", trigger.castExtendMs);

    readEnum(obj, "deathBehavior", kDeathBehaviorNames, trigger.deathBehavior);
    readEnum(obj, "sourceFilter", kSourceFilterNames, trigger.sourceFilter);

    if (const char* timing = readString(obj, "timing")) {
        // "FixateLine" is the old name for Fixate
        if (std::strcmp(timing, "FixateLine") == 0) {
            trigger.timing = TriggerTiming::Fixate;
        } else {
            enumFromName(kTimingNames, timing, trigger.timing);
        }
    }
    readEnum(obj, "anchor", kAnchorNames, trigger.anchor);

    if (const char* shape = readString(obj, "shape")) {
        trigger.shape = stringToAnnotationShape(shape);
    }
    readColor(obj, "color", trigger.color);
    readColor(obj, "dimensions", trigger.dimensions);
    readUint8(obj, "pointCount", trigger.pointCount);

    if (const char* style = readString(obj, "visualStyle")) {
        trigger.visualStyle = stringToAnnotationVisualStyle(style);
    }
    readFloat(obj, "stripeSpacing", trigger.stripeSpacing);
    readFloat(obj, "rippleSpeed", trigger.rippleSpeed);
    readFloat(obj, "rippleCount", trigger.rippleCount);

    readBool(obj, "enableEdgeFade", trigger.enableEdgeFade);
    readBool(obj, "enableGlow", trigger.enableGlow);
    readBool(obj, "enableOutline", trigger.enableOutline);
    readBool(obj, "enableGradient", trigger.enableGradient);
    readFloat(obj, "edgeFadeWidth", trigger.edgeFadeWidth);
    readFloat(obj, "outlineWidth", trigger.outlineWidth);
    readFloat(obj, "glowIntensity", trigger.glowIntensity);

    readBool(obj, "useSecondaryColor", trigger.useSecondaryColor);
    readBool(obj, "useInnerZoneColor", trigger.useInnerZoneColor);
    readColor(obj, "secondaryColor", trigger.secondaryColor);
    readColor(obj, "innerZoneColor", trigger.innerZoneColor);

    if (const char* durationType = readString(obj, "durationType")) {
        trigger.durationType = (std::strcmp(durationType, "encounter") == 0)
            ? AnnotationDuration::Encounter
            : AnnotationDuration::Timed;
    }
    readInt(obj, "durationMs", trigger.durationMs);
    readInt(obj, "leadTimeMs", trigger.leadTimeMs);
    readBool(obj, "ignoreEarlyPullCasts", trigger.ignoreEarlyPullCasts);

    readFloat(obj, "offsetX", trigger.offsetX);
    readFloat(obj, "offsetY", trigger.offsetY);
    readBool(obj, "enabled", trigger.enabled);

    AnnotationMotionConfig& motion = trigger.motion;
    if (const char* motionType = readString(obj, "motionType")) {
        motion.type = stringToAnnotationMotionType(motionType);
    }
    readFloat(obj, "rotationSpeed_degPerSec", motion.rotationSpeed_degPerSec);
    readFloat(obj, "motionEndOffsetX", motion.endOffsetX);
    readFloat(obj, "motionEndOffsetY", motion.endOffsetY);
    readInt(obj, "motionTravelTime_ms", motion.travelTime_ms);
    if (const char* endBehavior = readString(obj, "projectileEndBehavior")) {
        motion.endBehavior = stringToProjectileEndBehavior(endBehavior);
    }
    if (const char* endMode = readString(obj, "projectileEndMode")) {
        motion.endMode = stringToProjectileEndMode(endMode);
    }
    readFloat(obj, "projectileDistance", motion.projectileDistance);
    readFloat(obj, "facingOffset_deg", motion.facingOffset_deg);
    readBool(obj, "returnToOrigin", motion.returnToOrigin);
    readFloat(obj, "dropsPerSecond", motion.dropsPerSecond);
    readFloat(obj, "dropRadius", motion.dropRadius);
    readInt(obj, "dropLifetime_ms", motion.dropLifetime_ms);

    readUint8(obj, "minStacks", trigger.minStacks);
    readUint8(obj, "maxStacks", trigger.maxStacks);
    readStackColors(obj, trigger);

    readBool(obj, "isNegation", trigger.isNegation);
    if (const JsonValue* negates = findMember(obj, "negatesSpellIds"); negates && negates->IsArray()) {
        trigger.negatesSpellIds.clear();
        for (const auto& spellId : negates->GetArray()) {
            if (spellId.IsUint()) trigger.negatesSpellIds.push_back(spellId.GetUint());
        }
    }
}

const JsonValue* findTriggersArray(const rapidjson::Document& doc) {
    const JsonValue* triggers = findMember(doc, "triggers");
    return (triggers && triggers->IsArray()) ? triggers : nullptr;
}

bool hasIntVersion(const rapidjson::Document& doc) {
    const JsonValue* version = findMember(doc, "version");
    return version && version->IsInt();
}

// --- Writing -----------------------------------------------------------

using JsonAllocator = rapidjson::Document::AllocatorType;

void addString(JsonValue& obj, const char* key, const std::string& value, JsonAllocator& alloc) {
    JsonValue copy;
    copy.SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), alloc);
    obj.AddMember(rapidjson::StringRef(key), copy, alloc);
}

void addName(JsonValue& obj, const char* key, const char* value, JsonAllocator& alloc) {
    obj.AddMember(rapidjson::StringRef(key), JsonValue(value, alloc), alloc);
}

void addFloats(JsonValue& obj, const char* key, const std::array<float, 4>& values, JsonAllocator& alloc) {
    JsonValue array(rapidjson::kArrayType);
    for (float value : values) {
        array.PushBack(value, alloc);
    }
    obj.AddMember(rapidjson::StringRef(key), array, alloc);
}

JsonValue triggerToJson(const AutoAnnotationTrigger& trigger,
                        const std::unordered_map<uint32_t, std::string>* spellNameMap,
                        JsonAllocator& alloc) {
    JsonValue obj(rapidjson::kObjectType);

    obj.AddMember("id", trigger.id, alloc);
    addName(obj, "category", (trigger.category == TriggerCategory::Actor) ? "Actor" : "Spell", alloc);
    obj.AddMember("spell_id", trigger.spell_id, alloc);

    std::string spellName;
    if (spellNameMap && trigger.spell_id > 0) {
        auto it = spellNameMap->find(trigger.spell_id);
        if (it != spellNameMap->end()) {
            spellName = it->second;
        }
    }
    addString(obj, "spell_name", spellName, alloc);
    addString(obj, "creature_guid", trigger.creature_guid, alloc);
    addString(obj, "creature_name", trigger.creature_name, alloc);
    addString(obj, "actorNameFilter", trigger.actorNameFilter, alloc);
    obj.AddMember("trailIntervalMs", trigger.trailIntervalMs, alloc);
    obj.AddMember("castExtendMs", trigger.castExtendMs, alloc);

    addName(obj, "deathBehavior", enumName(kDeathBehaviorNames, trigger.deathBehavior, "DisappearImmediately"), alloc);
    addName(obj, "sourceFilter", enumName(kSourceFilterNames, trigger.sourceFilter, "Any"), alloc);
    addName(obj, "timing", enumName(kTimingNames, trigger.timing, "OnCastSuccess"), alloc);
    addName(obj, "anchor", enumName(kAnchorNames, trigger.anchor, "Target"), alloc);
    addName(obj, "shape", annotationShapeToString(trigger.shape), alloc);

    addFloats(obj, "color", trigger.color, alloc);
    addFloats(obj, "dimensions", trigger.dimensions, alloc);
    obj.AddMember("pointCount", trigger.pointCount, alloc);

    addName(obj, "visualStyle", annotationVisualStyleToString(trigger.visualStyle), alloc);
    obj.AddMember("stripeSpacing", trigger.stripeSpacing, alloc);
    obj.AddMember("rippleSpeed", trigger.rippleSpeed, alloc);
    obj.AddMember("rippleCount", trigger.rippleCount, alloc);

    obj.AddMember("enableEdgeFade", trigger.enableEdgeFade, alloc);
    obj.AddMember("enableGlow", trigger.enableGlow, alloc);
    obj.AddMember("enableOutline", trigger.enableOutline, alloc);
    obj.AddMember("enableGradient", trigger.enableGradient, alloc);
    obj.AddMember("edgeFadeWidth", trigger.edgeFadeWidth, alloc);
    obj.AddMember("outlineWidth", trigger.outlineWidth, alloc);
    obj.AddMember("glowIntensity", trigger.glowIntensity, alloc);

    obj.AddMember("useSecondaryColor", trigger.useSecondaryColor, alloc);
    obj.AddMember("useInnerZoneColor", trigger.useInnerZoneColor, alloc);
    addFloats(obj, "secondaryColor", trigger.secondaryColor, alloc);
    addFloats(obj, "innerZoneColor", trigger.innerZoneColor, alloc);

    addName(obj, "durationType", (trigger.durationType == AnnotationDuration::Encounter) ? "encounter" : "timed", alloc);
    obj.AddMember("durationMs", trigger.durationMs, alloc);
    obj.AddMember("leadTimeMs", trigger.leadTimeMs, alloc);
    obj.AddMember("ignoreEarlyPullCasts", trigger.ignoreEarlyPullCasts, alloc);

    obj.AddMember("offsetX", trigger.offsetX, alloc);
    obj.AddMember("offsetY", trigger.offsetY, alloc);
    obj.AddMember("enabled", trigger.enabled, alloc);

    const AnnotationMotionConfig& motion = trigger.motion;
    addName(obj, "motionType", annotationMotionTypeToString(motion.type), alloc);
    obj.AddMember("rotationSpeed_degPerSec", motion.rotationSpeed_degPerSec, alloc);
    obj.AddMember("motionEndOffsetX", motion.endOffsetX, alloc);
    obj.AddMember("motionEndOffsetY", motion.endOffsetY, alloc);
    obj.AddMember("motionTravelTime_ms", motion.travelTime_ms, alloc);
    addName(obj, "projectileEndBehavior", projectileEndBehaviorToString(motion.endBehavior), alloc);
    addName(obj, "projectileEndMode", projectileEndModeToString(motion.endMode), alloc);
    obj.AddMember("projectileDistance", motion.projectileDistance, alloc);
    obj.AddMember("facingOffset_deg", motion.facingOffset_deg, alloc);
    obj.AddMember("returnToOrigin", motion.returnToOrigin, alloc);
    obj.AddMember("dropsPerSecond", motion.dropsPerSecond, alloc);
    obj.AddMember("dropRadius", motion.dropRadius, alloc);
    obj.AddMember("dropLifetime_ms", motion.dropLifetime_ms, alloc);

    obj.AddMember("minStacks", trigger.minStacks, alloc);
    obj.AddMember("maxStacks", trigger.maxStacks, alloc);
    obj.AddMember("stackColorCount", trigger.stackColorCount, alloc);

    JsonValue stackColors(rapidjson::kArrayType);
    for (uint8_t i = 0; i < trigger.stackColorCount; ++i) {
        JsonValue entry(rapidjson::kObjectType);
        entry.AddMember("stackCount", trigger.stackColors[i].stackCount, alloc);
        addFloats(entry, "color", trigger.stackColors[i].color, alloc);
        stackColors.PushBack(entry, alloc);
    }
    obj.AddMember("stackColors", stackColors, alloc);

    obj.AddMember("isNegation", trigger.isNegation, alloc);
    JsonValue negates(rapidjson::kArrayType);
    for (uint32_t spellId : trigger.negatesSpellIds) {
        negates.PushBack(spellId, alloc);
    }
    obj.AddMember("negatesSpellIds", negates, alloc);

    return obj;
}

} // namespace

AutoAnnotationTriggerManager::AutoAnnotationTriggerManager() = default;
AutoAnnotationTriggerManager::~AutoAnnotationTriggerManager() = default;

uint64_t AutoAnnotationTriggerManager::addTrigger(const AutoAnnotationTrigger& trigger) {
    AutoAnnotationTrigger newTrigger = trigger;
    newTrigger.id = nextTriggerId_++;
    const uint64_t id = newTrigger.id;
    appendTrigger(std::move(newTrigger));
    commitTriggerChange();
    return id;
}

void AutoAnnotationTriggerManager::removeTrigger(uint64_t triggerId) {
    int idx = findTriggerIndex(triggerId);
    if (idx < 0) return;

    triggerIdToIndex_.erase(triggerId);
    triggers_.erase(triggers_.begin() + idx);
    for (size_t i = static_cast<size_t>(idx); i < triggers_.size(); ++i) {
        triggerIdToIndex_[triggers_[i].id] = i;
    }
    commitTriggerChange();
}

void AutoAnnotationTriggerManager::updateTrigger(const AutoAnnotationTrigger& trigger) {
    int idx = findTriggerIndex(trigger.id);
    if (idx < 0) return;

    triggers_[idx] = trigger;
    commitTriggerChange();
}

bool AutoAnnotationTriggerManager::toggleTrigger(uint64_t triggerId) {
    int idx = findTriggerIndex(triggerId);
    if (idx < 0) return false;

    triggers_[idx].enabled = !triggers_[idx].enabled;
    commitTriggerChange();
    return triggers_[idx].enabled;
}

void AutoAnnotationTriggerManager::clearTriggers() {
    triggers_.clear();
    triggerIdToIndex_.clear();
    clearCache();
    notifyChange();
}

const AutoAnnotationTrigger* AutoAnnotationTriggerManager::getTrigger(uint64_t id) const {
    int idx = findTriggerIndex(id);
    return idx >= 0 ? &triggers_[idx] : nullptr;
}

AutoAnnotationTrigger* AutoAnnotationTriggerManager::getTriggerMutable(uint64_t id) {
    int idx = findTriggerIndex(id);
    return idx >= 0 ? &triggers_[idx] : nullptr;
}

void AutoAnnotationTriggerManager::appendTrigger(AutoAnnotationTrigger trigger) {
    triggerIdToIndex_[trigger.id] = triggers_.size();
    triggers_.push_back(std::move(trigger));
}

void AutoAnnotationTriggerManager::commitTriggerChange() {
    invalidateCache();
    dirty_ = true;
    notifyChange();
    saveToFile();
}

void AutoAnnotationTriggerManager::buildCache(
    const SpellCastDatabase* spellCasts,
    const CombatDatabase* combat,
    const AuraDatabase* auras,
    const EventDatabase* eventDb,
    const DeathDatabase* deathDb
) {
    cachedEvents_.clear();

    if (triggers_.empty() || !eventDb) {
        cacheValid_ = true;
        return;
    }

    std::unordered_set<uint64_t> seenEvents;
    TriggerEvaluatorContext ctx{triggers_, cachedEvents_, seenEvents};

    trigger_eval::buildCastCache(ctx, spellCasts, eventDb);
    trigger_eval::buildDamageCache(ctx, combat, eventDb);
    trigger_eval::buildDamageTakenCache(ctx, combat, eventDb);
    trigger_eval::buildDebuffCache(ctx, auras, eventDb);
    trigger_eval::buildBuffCache(ctx, auras, eventDb);
    trigger_eval::buildActorCache(ctx, eventDb, deathDb);

    std::sort(cachedEvents_.begin(), cachedEvents_.end(),
        [](const CachedAnnotationEvent& a, const CachedAnnotationEvent& b) {
            return a.timestamp_ms < b.timestamp_ms;
        });

    computeNegations();

    cacheValid_ = true;
}

void AutoAnnotationTriggerManager::clearCache() {
    cachedEvents_.clear();
    activeAnnotations_.clear();
    cacheValid_ = false;
}

void AutoAnnotationTriggerManager::updateAnnotations(
    int32_t currentTimestamp_ms,
    AnnotationManager* annotationManager
) {
    if (!annotationManager || cachedEvents_.empty()) return;

    // key -> best cache index for that key
    std::unordered_map<uint64_t, size_t> activeKeys;

    // Find the active sample for each chain. At boundaries the newer sample wins.
    for (size_t i = 0; i < cachedEvents_.size(); ++i) {
        const auto& event = cachedEvents_[i];
        const auto* trigger = getTrigger(event.triggerId);
        if (!trigger || !trigger->enabled) continue;

        int32_t showTime = event.timestamp_ms;
        int32_t hideTime = naturalHideTime(event, *trigger);

        // Death only cuts short annotations that started before the death.
        if (event.isActorTrigger && event.actorDeathTime_ms != INT32_MAX &&
            showTime < event.actorDeathTime_ms &&
            event.deathBehavior == ActorDeathBehavior::DisappearImmediately) {
            hideTime = std::min(hideTime, event.actorDeathTime_ms);
        }

        trigger_eval::DeathFade fade = trigger_eval::deathFadeFor(event, hideTime);
        if (fade.fades()) {
            hideTime = fade.end;
        }

        if (event.negatedAt_ms != INT32_MAX && event.negatedAt_ms < hideTime) {
            hideTime = event.negatedAt_ms;
        }

        bool shouldBeVisible = (currentTimestamp_ms >= showTime && currentTimestamp_ms < hideTime);
        if (!shouldBeVisible) continue;

        uint64_t key = (event.sampleChainId != 0)
            ? event.sampleChainId
            : (kNonChainKeyOffset | static_cast<uint64_t>(i));

        auto it = activeKeys.find(key);
        if (it == activeKeys.end()) {
            activeKeys[key] = i;
        } else if (event.sampleChainId != 0 && i > it->second) {
            it->second = i;
        }
    }

    for (const auto& [key, cacheIdx] : activeKeys) {
        const auto& event = cachedEvents_[cacheIdx];
        const auto* trigger = getTrigger(event.triggerId);
        if (!trigger) continue;

        auto it = activeAnnotations_.find(key);
        if (it != activeAnnotations_.end()) {
            // Chained samples update the existing annotation rather than recreating it.
            // Non-chained events are single-shot and need no update.
            if (event.sampleChainId != 0) {
                if (Annotation* ann = annotationManager->getAnnotationMutable(it->second)) {
                    updateAnnotationFromSample(ann, event, *trigger);
                }
            }
        } else {
            uint64_t annotationId = createAnnotationFromCache(event, *trigger, annotationManager);
            it = activeAnnotations_.emplace(key, annotationId).first;
        }

        trigger_eval::DeathFade fade = trigger_eval::deathFadeFor(event, naturalHideTime(event, *trigger));
        if (fade.fades()) {
            if (Annotation* ann = annotationManager->getAnnotationMutable(it->second)) {
                ann->color = trigger->getColorForStacks(event.stackCount);
                ann->color[3] *= trigger_eval::deathFadeAlpha(fade, currentTimestamp_ms);
            }
        }
    }

    for (auto it = activeAnnotations_.begin(); it != activeAnnotations_.end(); ) {
        if (activeKeys.find(it->first) == activeKeys.end()) {
            annotationManager->deleteAnnotation(it->second);
            it = activeAnnotations_.erase(it);
        } else {
            ++it;
        }
    }
}

void AutoAnnotationTriggerManager::updateAnnotationFromSample(
    Annotation* annotation,
    const CachedAnnotationEvent& event,
    const AutoAnnotationTrigger& trigger
) {
    annotation->creationTimestamp_ms = event.timestamp_ms;
    annotation->durationMs = event.sampleDurationMs;
    annotation->worldX = event.worldX;
    annotation->worldY = event.worldY;

    if (event.hasNextPosition) {
        annotation->nextX = event.nextX;
        annotation->nextY = event.nextY;
        annotation->hasNextPosition = true;
    } else {
        annotation->hasNextPosition = false;
    }

    // Source position is used by Fixate/Tether line shapes.
    if (event.hasSourcePosition) {
        annotation->sourceX = event.sourceX;
        annotation->sourceY = event.sourceY;
        annotation->hasSourcePosition = true;
    }

    annotation->color = trigger.getColorForStacks(event.stackCount);

    if (event.hasFacing) {
        annotation->dimensions[3] = rotationForFacing(facingRadians(event, trigger));
    }
}

uint64_t AutoAnnotationTriggerManager::createAnnotationFromCache(
    const CachedAnnotationEvent& event,
    const AutoAnnotationTrigger& trigger,
    AnnotationManager* annotationManager
) {
    uint64_t annotationId = annotationManager->createAnnotation(
        trigger.shape, event.worldX, event.worldY, event.timestamp_ms);

    Annotation* annotation = annotationManager->getAnnotationMutable(annotationId);
    if (!annotation) return annotationId;

    annotation->isAutoGenerated = true;
    annotation->color = trigger.getColorForStacks(event.stackCount);
    annotation->dimensions = trigger.dimensions;
    annotation->pointCount = trigger.pointCount;

    annotation->visualStyle = trigger.visualStyle;
    annotation->stripeSpacing = trigger.stripeSpacing;
    annotation->rippleSpeed = trigger.rippleSpeed;
    annotation->rippleCount = trigger.rippleCount;

    annotation->enableEdgeFade = trigger.enableEdgeFade;
    annotation->enableGlow = trigger.enableGlow;
    annotation->enableOutline = trigger.enableOutline;
    annotation->enableGradient = trigger.enableGradient;
    annotation->edgeFadeWidth = trigger.edgeFadeWidth;
    annotation->outlineWidth = trigger.outlineWidth;
    annotation->glowIntensity = trigger.glowIntensity;

    annotation->useSecondaryColor = trigger.useSecondaryColor;
    annotation->useInnerZoneColor = trigger.useInnerZoneColor;
    annotation->secondaryColor = trigger.secondaryColor;
    annotation->innerZoneColor = trigger.innerZoneColor;

    annotation->motion = trigger.motion;

    // Dynamic projectile modes store an absolute end point; the annotation wants an offset.
    if (event.hasProjectileEnd && trigger.motion.endMode != ProjectileEndMode::Manual) {
        annotation->motion.endOffsetX = event.projectileEndX - event.worldX;
        annotation->motion.endOffsetY = event.projectileEndY - event.worldY;
    }

    if (event.isDuringDebuffSample) {
        // Encounter duration keeps isVisibleAtTime() true; updateAnnotations() deletes
        // the annotation when the chain ends. durationMs is still used for lerping.
        annotation->durationType = AnnotationDuration::Encounter;
        annotation->durationMs = event.sampleDurationMs;
        annotation->skipFade = true;
        if (event.hasNextPosition) {
            annotation->nextX = event.nextX;
            annotation->nextY = event.nextY;
            annotation->hasNextPosition = true;
        }
    } else {
        annotation->durationType = trigger.durationType;
        annotation->durationMs = trigger.durationMs;
    }

    // Facing-based anchors override any manual rotation (dimensions[3]).
    if (event.hasFacing) {
        float facing = facingRadians(event, trigger);
        annotation->dimensions[3] = rotationForFacing(facing);

        // Put the rectangle's back edge on the actor so it extends forward from them.
        if (trigger.shape == AnnotationShape::Rectangle) {
            float halfLength = annotation->dimensions[1] / 2.0f;
            float offsetX = halfLength * std::cos(facing);
            float offsetY = halfLength * std::sin(facing);

            annotation->worldX += offsetX;
            annotation->worldY += offsetY;
            annotation->currentX += offsetX;
            annotation->currentY += offsetY;
        }
    }

    // Only non-facing Line shapes carry a source position; facing-based lines
    // extend from the anchor along the facing instead.
    if (event.hasSourcePosition) {
        annotation->sourceX = event.sourceX;
        annotation->sourceY = event.sourceY;
        annotation->hasSourcePosition = true;
    }

    return annotationId;
}

void AutoAnnotationTriggerManager::clearAutoAnnotations(AnnotationManager* annotationManager) {
    if (!annotationManager) return;

    for (auto& [key, annotationId] : activeAnnotations_) {
        annotationManager->deleteAnnotation(annotationId);
    }
    activeAnnotations_.clear();
}

void AutoAnnotationTriggerManager::notifyChange() {
    if (onChangeCallback_) {
        onChangeCallback_();
    }
}

int AutoAnnotationTriggerManager::findTriggerIndex(uint64_t id) const {
    auto it = triggerIdToIndex_.find(id);
    if (it != triggerIdToIndex_.end()) {
        return static_cast<int>(it->second);
    }
    return -1;
}

void AutoAnnotationTriggerManager::computeNegations() {
    struct NegationInfo {
        size_t eventIndex;
        float x, y, radius;
        int32_t timestamp_ms;
        const std::vector<uint32_t>* targetSpellIds;
    };
    std::vector<NegationInfo> negationEvents;

    for (size_t i = 0; i < cachedEvents_.size(); ++i) {
        const auto& event = cachedEvents_[i];
        const auto* trigger = getTrigger(event.triggerId);
        if (!trigger || !trigger->isNegation || trigger->negatesSpellIds.empty()) continue;
        if (trigger->shape != AnnotationShape::Circle) continue;

        negationEvents.push_back({
            i, event.worldX, event.worldY, trigger->dimensions[0],
            event.timestamp_ms, &trigger->negatesSpellIds
        });
    }

    for (const auto& neg : negationEvents) {
        float radiusSq = neg.radius * neg.radius;

        for (size_t i = 0; i < cachedEvents_.size(); ++i) {
            if (i == neg.eventIndex) continue;
            auto& event = cachedEvents_[i];
            if (event.negatedAt_ms != INT32_MAX) continue;

            const auto* trigger = getTrigger(event.triggerId);
            if (!trigger) continue;

            const auto& targets = *neg.targetSpellIds;
            if (std::find(targets.begin(), targets.end(), trigger->spell_id) == targets.end()) continue;

            // Must be visible at the moment of negation.
            if (neg.timestamp_ms < event.timestamp_ms || neg.timestamp_ms >= naturalHideTime(event, *trigger)) {
                continue;
            }

            float dx = event.worldX - neg.x;
            float dy = event.worldY - neg.y;
            if (dx*dx + dy*dy <= radiusSq) {
                event.negatedAt_ms = neg.timestamp_ms;
            }
        }
    }
}

bool AutoAnnotationTriggerManager::getFirstEventPositionForSpell(
    uint32_t spellId,
    TriggerTiming timing,
    TriggerAnchor anchor,
    const SpellCastDatabase* spellCasts,
    const CombatDatabase* combat,
    const AuraDatabase* auras,
    const EventDatabase* eventDb,
    float& outX, float& outY
) const {
    if (!eventDb) return false;

    bool anchorIsCaster = anchorAtCaster(anchor);

    switch (timing) {
        case TriggerTiming::OnCastSuccess:
        case TriggerTiming::DuringCast: {
            if (!spellCasts) return false;

            for (const auto& [actorGuid, casts] : spellCasts->getAllActorCasts()) {
                for (const auto& cast : casts) {
                    if (cast.spell_id != spellId) continue;

                    std::string anchorGuid;
                    if (anchorIsCaster) {
                        anchorGuid = actorGuid;
                    } else {
                        bool hasValidTarget = !cast.target_guid.empty() &&
                                              cast.target_guid != actorGuid &&
                                              !cast.target_guid.starts_with("000000");
                        if (!hasValidTarget) continue;
                        anchorGuid = cast.target_guid;
                    }

                    if (trigger_eval::findPositionAtTime(eventDb, anchorGuid, cast.end_timestamp_ms, outX, outY)) {
                        return true;
                    }
                }
            }
            break;
        }

        case TriggerTiming::OnDamage: {
            if (!combat) return false;

            const auto* actorMap = combat->getActorMap();
            if (!actorMap) return false;

            for (const auto& [sourceGuid, table] : *actorMap) {
                for (const auto& record : table.damage_dealt_table) {
                    if (record.spell_id != spellId) continue;

                    std::string anchorGuid(anchorIsCaster
                        ? guidInterner().lookup(sourceGuid)
                        : guidInterner().lookup(record.target_guid_id));
                    if (anchorGuid.empty()) continue;

                    if (trigger_eval::findPositionAtTime(eventDb, anchorGuid, record.timestamp_ms, outX, outY)) {
                        return true;
                    }
                }
            }
            break;
        }

        case TriggerTiming::OnDamageTaken: {
            if (!combat) return false;

            const auto& damageTakenIndex = combat->getDamageTakenIndex();
            if (damageTakenIndex.empty()) return false;

            // Players only.
            for (const auto& [targetGuidId, recordPtrs] : damageTakenIndex) {
                std::string_view victimGuidSv = guidInterner().lookup(targetGuidId);

                if (!victimGuidSv.starts_with("Player-")) continue;

                std::string victimGuid(victimGuidSv);

                for (const CombatRecord* record : recordPtrs) {
                    if (record->spell_id != spellId) continue;

                    // Damage-taken triggers always anchor on the victim.
                    if (trigger_eval::findPositionAtTime(eventDb, victimGuid, record->timestamp_ms, outX, outY)) {
                        return true;
                    }
                }
            }
            break;
        }

        case TriggerTiming::OnDebuffApplied:
        case TriggerTiming::DuringDebuff:
        case TriggerTiming::OnDebuffEnd:
        case TriggerTiming::Fixate:
        case TriggerTiming::Tether: {
            if (!auras) return false;

            // Debuffs always anchor on the debuffed unit, never the caster.
            for (const auto& [actorGuid, auraList] : auras->getAllTargetAuras()) {
                for (const auto& aura : auraList) {
                    if (aura.spell_id != spellId) continue;
                    if (aura.aura_type != AuraType::DEBUFF) continue;

                    int32_t timestamp = (timing == TriggerTiming::OnDebuffEnd)
                                        ? aura.removed_at_ms
                                        : aura.applied_at_ms;

                    if (trigger_eval::findPositionAtTime(eventDb, actorGuid, timestamp, outX, outY)) {
                        return true;
                    }
                }
            }
            break;
        }

        case TriggerTiming::OnBuffApplied:
        case TriggerTiming::DuringBuff:
        case TriggerTiming::OnBuffEnd: {
            if (!auras) return false;

            for (const auto& [actorGuid, auraList] : auras->getAllTargetAuras()) {
                for (const auto& aura : auraList) {
                    if (aura.spell_id != spellId) continue;
                    if (aura.aura_type != AuraType::BUFF) continue;

                    int32_t timestamp = (timing == TriggerTiming::OnBuffEnd)
                                        ? aura.removed_at_ms
                                        : aura.applied_at_ms;

                    // actorGuid is the buffed unit; for self-buffs it equals the source.
                    const std::string& anchorGuid = anchorIsCaster ? aura.source_guid : actorGuid;

                    if (trigger_eval::findPositionAtTime(eventDb, anchorGuid, timestamp, outX, outY)) {
                        return true;
                    }
                }
            }
            break;
        }

        default:
            break;
    }

    return false;
}

void AutoAnnotationTriggerManager::setSpellNameMap(const std::unordered_map<uint32_t, std::string>* spellNameMap) {
    spellNameMap_ = spellNameMap;
}

std::string AutoAnnotationTriggerManager::getTriggerFileName() {
    return awow::AssetResolver::sanitizeFilename(CurrentEncounter::instance().getName()) + ".json";
}

void AutoAnnotationTriggerManager::saveToFile() {
    const auto& encounter = CurrentEncounter::instance();

    if (!dirty_ || !encounter.hasEncounter() || triggers_.empty()) {
        return;
    }

    // Passing the encounter name turns on pretty-printing.
    std::string json = serializeTriggersToJson(triggers_, spellNameMap_, encounter.getName());

    std::string filepath = awow::AssetResolver::getUserPath("triggers", getTriggerFileName());
    awow::AssetResolver::ensureDirectoryExists(filepath);
    std::ofstream file(filepath);
    if (!file.is_open()) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to save auto-annotation triggers to: " + filepath);
        return;
    }
    file << json;
    dirty_ = false;
}

void AutoAnnotationTriggerManager::loadFromFile() {
    if (!CurrentEncounter::instance().hasEncounter()) {
        return;
    }

    // User path first, then defaults/
    std::string filepath = awow::AssetResolver::resolve("triggers", getTriggerFileName());

    if (!std::filesystem::exists(filepath)) {
        return;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to open auto-annotation triggers file: " + filepath);
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    rapidjson::Document doc;
    if (doc.Parse(content.c_str()).HasParseError()) {
        AWOW_LOG_ERROR("ANNOTATION", "JSON parse error in auto-annotation triggers file: " + filepath);
        return;
    }

    if (!doc.IsObject() || !hasIntVersion(doc)) {
        AWOW_LOG_ERROR("ANNOTATION", "Missing or invalid version in auto-annotation triggers file: " + filepath);
        return;
    }

    const JsonValue* triggersArray = findTriggersArray(doc);
    if (!triggersArray) {
        return;
    }

    triggers_.clear();
    triggerIdToIndex_.clear();
    for (const auto& triggerObj : triggersArray->GetArray()) {
        if (!triggerObj.IsObject()) continue;

        AutoAnnotationTrigger trigger;
        trigger.id = nextTriggerId_++;  // file IDs are not trusted
        readTriggerFields(triggerObj, trigger);

        const JsonValue* spellId = findMember(triggerObj, "spell_id");
        bool hasSpellId = spellId && spellId->IsUint();
        if (trigger.category == TriggerCategory::Spell && !hasSpellId) {
            continue;
        }

        // Older files stored a showUncertainty bool instead of a style.
        const JsonValue* showUncertainty = findMember(triggerObj, "showUncertainty");
        if (!readString(triggerObj, "visualStyle") && showUncertainty && showUncertainty->IsBool()) {
            trigger.visualStyle = showUncertainty->GetBool()
                ? AnnotationVisualStyle::Hatched
                : AnnotationVisualStyle::Solid;
        }

        appendTrigger(std::move(trigger));
    }

    invalidateCache();
    dirty_ = false;
}

// Static so snapshot export can use it without a manager instance.
std::string AutoAnnotationTriggerManager::serializeTriggersToJson(
    const std::vector<AutoAnnotationTrigger>& triggers,
    const std::unordered_map<uint32_t, std::string>* spellNameMap,
    const std::string& encounterName) {

    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();

    doc.AddMember("version", 1, allocator);

    // Only the on-disk trigger files carry the encounter name.
    if (!encounterName.empty()) {
        addString(doc, "encounterName", encounterName, allocator);
    }

    JsonValue triggersArray(rapidjson::kArrayType);
    for (const auto& trigger : triggers) {
        triggersArray.PushBack(triggerToJson(trigger, spellNameMap, allocator), allocator);
    }
    doc.AddMember("triggers", triggersArray, allocator);

    // Pretty-print the on-disk files; snapshots stay compact.
    rapidjson::StringBuffer buffer;
    if (!encounterName.empty()) {
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
    } else {
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
    }
    return buffer.GetString();
}

bool AutoAnnotationTriggerManager::deserializeTriggersFromJson(const std::string& json) {
    if (json.empty()) {
        return false;
    }

    rapidjson::Document doc;
    if (doc.Parse(json.c_str()).HasParseError()) {
        AWOW_LOG_ERROR("ANNOTATION", "JSON parse error in triggers data");
        return false;
    }

    if (!doc.IsObject()) {
        AWOW_LOG_ERROR("ANNOTATION", "Invalid triggers JSON: expected object");
        return false;
    }

    if (!hasIntVersion(doc)) {
        AWOW_LOG_WARNING("ANNOTATION", "Triggers JSON missing version field");
    }

    const JsonValue* triggersArray = findTriggersArray(doc);
    if (!triggersArray) {
        AWOW_LOG_ERROR("ANNOTATION", "Invalid triggers JSON: missing triggers array");
        return false;
    }

    triggers_.clear();
    triggerIdToIndex_.clear();
    nextTriggerId_ = 1;
    for (const auto& triggerObj : triggersArray->GetArray()) {
        if (!triggerObj.IsObject()) continue;

        // Snapshot ids are kept, since the cached events refer to them
        AutoAnnotationTrigger trigger;
        const JsonValue* id = findMember(triggerObj, "id");
        if (id && id->IsUint64()) {
            trigger.id = id->GetUint64();
            if (trigger.id >= nextTriggerId_) {
                nextTriggerId_ = trigger.id + 1;
            }
        } else {
            trigger.id = nextTriggerId_++;
        }

        readTriggerFields(triggerObj, trigger);
        appendTrigger(std::move(trigger));
    }

    invalidateCache();
    dirty_ = false;
    activeAnnotations_.clear();
    return true;
}
