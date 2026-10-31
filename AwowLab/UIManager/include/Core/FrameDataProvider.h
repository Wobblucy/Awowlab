#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>
#include <memory>
#include <cstdint>
#include <functional>
#include "FrameCache.h"

class EventDatabase;
class SpellCastDatabase;
class ResourceDatabase;
class CombatDatabase;
class DeathDatabase;
class AuraDatabase;
class DispelDatabase;
class AvoidanceDatabase;
class EmoteDatabase;
class ActorColorGenerator;
class ActorPositionInterpolator;
class PlaybackController;
class UIActorSelector;

// Provides per-frame data to UI components
// Encapsulates event database access, caching, and position interpolation
class FrameDataProvider {
public:
    FrameDataProvider();
    ~FrameDataProvider();

    FrameDataProvider(const FrameDataProvider&) = delete;
    FrameDataProvider& operator=(const FrameDataProvider&) = delete;

    // Initialize with required components
    void setPlaybackController(std::shared_ptr<PlaybackController> playback);
    void setPositionInterpolator(std::shared_ptr<ActorPositionInterpolator> interpolator);
    void setColorGenerator(std::shared_ptr<ActorColorGenerator> colorGen);
    void setActorSelector(UIActorSelector* selector);

    // Load event databases
    void loadEventDatabase(const EventDatabase* db);
    void loadSpellCastDatabase(const SpellCastDatabase* db);
    void loadResourceDatabase(const ResourceDatabase* db);
    void loadCombatDatabase(const CombatDatabase* db);
    void loadDeathDatabase(const DeathDatabase* db);
    void loadAuraDatabase(const AuraDatabase* db);
    void loadDispelDatabase(const DispelDatabase* db);
    void loadAvoidanceDatabase(const AvoidanceDatabase* db);
    void loadEmoteDatabase(const EmoteDatabase* db);

    // Load spell name map (for UI display)
    void loadSpellNameMap(const std::unordered_map<uint32_t, std::string>* spellNameMap);

    // Load GUID to name map (for resolving pet/guardian names from combat events)
    void loadGuidToNameMap(const std::unordered_map<std::string, std::string>* guidToNameMap);

    // Generate fallback colors for actors without COMBATANT_INFO (NPCs, pets, etc.)
    // Call after loadCombatantInfo() so player class colors are already set
    void generateFallbackColors();

    // Per-frame update (call once per frame before accessing data)
    void updateFrame();

    // Data accessors (valid after updateFrame())
    const FrameCache& getFrameCache() const { return frameCache_; }
    const std::vector<ActorEvent>& getWindowEvents() const { return frameCache_.getWindowEvents(); }
    const std::unordered_map<std::string_view, std::string_view>& getGuidToName() const { return frameCache_.getGuidToName(); }
    const std::unordered_map<std::string_view, std::string_view>& getGuidToTruncatedName() const { return frameCache_.getGuidToTruncatedName(); }
    const std::unordered_map<std::string_view, ActorType>& getGuidToType() const { return frameCache_.getGuidToType(); }
    const std::unordered_map<std::string_view, InterpolatedPosition>& getActorPositions() const { return frameCache_.getActorPositions(); }
    const std::unordered_map<std::string_view, InterpolatedResource>& getActorResources() const { return frameCache_.getActorResources(); }
    bool hasPositions() const { return frameCache_.hasPositions(); }
    bool hasResources() const { return frameCache_.hasResources(); }
    uint32_t getCurrentTime() const { return frameCache_.getCachedTime(); }

    // Database accessors
    const EventDatabase* getEventDatabase() const { return eventDatabase_; }
    const SpellCastDatabase* getSpellCastDatabase() const { return spellCastDatabase_; }
    const ResourceDatabase* getResourceDatabase() const { return resourceDatabase_; }
    const CombatDatabase* getCombatDatabase() const { return combatDatabase_; }
    const DeathDatabase* getDeathDatabase() const { return deathDatabase_; }
    const AuraDatabase* getAuraDatabase() const { return auraDatabase_; }
    const DispelDatabase* getDispelDatabase() const { return dispelDatabase_; }
    const AvoidanceDatabase* getAvoidanceDatabase() const { return avoidanceDatabase_; }
    const EmoteDatabase* getEmoteDatabase() const { return emoteDatabase_; }
    const std::unordered_map<uint32_t, std::string>* getSpellNameMap() const { return spellNameMap_; }
    const std::unordered_map<std::string, std::string>* getGuidToNameMap() const { return guidToNameMap_; }
    bool hasEvents() const;

    // Death state query (checks if actor should be hidden due to death)
    bool shouldHideActor(std::string_view guid) const;

    // Bounds accessors (raw world coordinates)
    float getMinX() const { return cachedMinX_; }
    float getMaxX() const { return cachedMaxX_; }
    float getMinY() const { return cachedMinY_; }
    float getMaxY() const { return cachedMaxY_; }
    bool hasBounds() const { return boundsCached_; }

    // Map tracking for dynamic bounds reset
    uint16_t getCurrentMapId() const { return currentMapId_; }
    void checkMapChange();  // Updates map ID and recalculates bounds if needed

    // Manual bounds recalculation
    void recalculateBounds();
    void recalculateBounds(const std::vector<ActorEvent>& events);

private:
    // Database references
    const EventDatabase* eventDatabase_;
    const SpellCastDatabase* spellCastDatabase_;
    const ResourceDatabase* resourceDatabase_;
    const CombatDatabase* combatDatabase_;
    const DeathDatabase* deathDatabase_;
    const AuraDatabase* auraDatabase_;
    const DispelDatabase* dispelDatabase_;
    const AvoidanceDatabase* avoidanceDatabase_;
    const EmoteDatabase* emoteDatabase_;
    const std::unordered_map<uint32_t, std::string>* spellNameMap_;
    const std::unordered_map<std::string, std::string>* guidToNameMap_;

    // Component dependencies
    std::shared_ptr<PlaybackController> playbackController_;
    std::shared_ptr<ActorPositionInterpolator> positionInterpolator_;
    std::shared_ptr<ActorColorGenerator> colorGenerator_;
    UIActorSelector* actorSelector_;

    FrameCache frameCache_;

    // Reusable containers to avoid per-frame allocations
    std::vector<ActorEvent> windowEventsBuffer_;
    std::vector<std::string_view> visibleGuidsBuffer_;
    std::unordered_map<std::string_view, InterpolatedPosition> actorPositionsBuffer_;
    std::unordered_map<std::string_view, InterpolatedResource> actorResourcesBuffer_;

    float cachedMinX_, cachedMaxX_;
    float cachedMinY_, cachedMaxY_;
    bool boundsCached_;

    uint16_t currentMapId_;
    uint32_t lastCreatureEventTime_;
    static constexpr uint32_t INACTIVITY_RESET_MS = 5000;
};
