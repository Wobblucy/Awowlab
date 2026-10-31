#pragma once

#include "AutoAnnotationTypes.h"
#include "AnnotationManager.h"
#include "TriggerEvaluators/TriggerEvaluatorCommon.h"
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <functional>

class SpellCastDatabase;
class CombatDatabase;
class AuraDatabase;
class EventDatabase;
class DeathDatabase;

// Auto-annotation triggers that fire during encounter playback.
// When triggers change, every matching event is scanned once and cached with its
// position; playback then creates annotations from that cache, so positions stay stable.
class AutoAnnotationTriggerManager {
public:
    AutoAnnotationTriggerManager();
    ~AutoAnnotationTriggerManager();

    uint64_t addTrigger(const AutoAnnotationTrigger& trigger);
    void removeTrigger(uint64_t triggerId);
    void updateTrigger(const AutoAnnotationTrigger& trigger);
    bool toggleTrigger(uint64_t triggerId);  // Returns new enabled state
    void clearTriggers();

    const std::vector<AutoAnnotationTrigger>& getTriggers() const { return triggers_; }
    const AutoAnnotationTrigger* getTrigger(uint64_t id) const;
    AutoAnnotationTrigger* getTriggerMutable(uint64_t id);
    size_t getTriggerCount() const { return triggers_.size(); }
    bool hasTriggers() const { return !triggers_.empty(); }

    // Rebuild when triggers change or a new encounter is loaded.
    void buildCache(
        const SpellCastDatabase* spellCasts,
        const CombatDatabase* combat,
        const AuraDatabase* auras,
        const EventDatabase* eventDb,
        const DeathDatabase* deathDb = nullptr  // needed for actor death times
    );

    void clearCache();

    bool isCacheValid() const { return cacheValid_; }
    void invalidateCache() { cacheValid_ = false; }

    // Creates and removes annotations as the timeline moves.
    void updateAnnotations(
        int32_t currentTimestamp_ms,
        AnnotationManager* annotationManager
    );

    void clearAutoAnnotations(AnnotationManager* annotationManager);

    size_t getCachedEventCount() const { return cachedEvents_.size(); }

    // Position of the first matching event, for the UI preview. Queries the databases
    // directly so it works before the trigger exists.
    bool getFirstEventPositionForSpell(
        uint32_t spellId,
        TriggerTiming timing,
        TriggerAnchor anchor,
        const SpellCastDatabase* spellCasts,
        const CombatDatabase* combat,
        const AuraDatabase* auras,
        const EventDatabase* eventDb,
        float& outX, float& outY) const;

    // Snapshots export the computed cache and restore it on import.
    const std::vector<CachedAnnotationEvent>& getCachedEvents() const { return cachedEvents_; }

    // Replaces the cache and marks it valid, skipping buildCache().
    void setCachedEvents(const std::vector<CachedAnnotationEvent>& events) {
        cachedEvents_ = events;
        cacheValid_ = true;
        activeAnnotations_.clear();
    }

    using ChangeCallback = std::function<void()>;
    void setChangeCallback(ChangeCallback callback) { onChangeCallback_ = std::move(callback); }

    // Triggers persist to triggers/{encounterName}.json, named from CurrentEncounter.
    void setSpellNameMap(const std::unordered_map<uint32_t, std::string>* spellNameMap);
    void loadFromFile();
    void saveToFile();
    bool isDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }

    // A non-empty encounterName is written into the JSON and turns on pretty-printing
    // (trigger files); empty gives compact output for snapshots.
    static std::string serializeTriggersToJson(
        const std::vector<AutoAnnotationTrigger>& triggers,
        const std::unordered_map<uint32_t, std::string>* spellNameMap = nullptr,
        const std::string& encounterName = "");

    // Replaces the current triggers. False on parse error.
    bool deserializeTriggersFromJson(const std::string& json);

private:
    std::vector<AutoAnnotationTrigger> triggers_;
    std::unordered_map<uint64_t, size_t> triggerIdToIndex_;
    uint64_t nextTriggerId_ = 1;

    // Sorted by timestamp.
    std::vector<CachedAnnotationEvent> cachedEvents_;
    bool cacheValid_ = false;

    // Chain ID (samples) or offset cache index (one-shot) -> annotation ID
    std::unordered_map<uint64_t, uint64_t> activeAnnotations_;

    ChangeCallback onChangeCallback_;

    const std::unordered_map<uint32_t, std::string>* spellNameMap_ = nullptr;
    bool dirty_ = false;

    // {encounter name}.json, inside the triggers folder
    static std::string getTriggerFileName();

    // Keeps triggerIdToIndex_ in step with triggers_.
    void appendTrigger(AutoAnnotationTrigger trigger);

    // After an edit: drop the cache, tell listeners and save.
    void commitTriggerChange();

    uint64_t createAnnotationFromCache(
        const CachedAnnotationEvent& event,
        const AutoAnnotationTrigger& trigger,
        AnnotationManager* annotationManager
    );

    // Chained samples reuse one annotation and update it in place.
    void updateAnnotationFromSample(
        Annotation* annotation,
        const CachedAnnotationEvent& event,
        const AutoAnnotationTrigger& trigger
    );

    void notifyChange();
    int findTriggerIndex(uint64_t id) const;
    void computeNegations();
};
