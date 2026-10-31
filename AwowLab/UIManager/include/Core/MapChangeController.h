#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "OutputWriter.h"  // For MapChangeEvent (needed for unordered_map)
class RenderingCoordinator;
class FrameDataProvider;
class PlaybackController;
class EncounterTextureManager;

// Handles map switching logic with anti-flicker protection
class MapChangeController {
public:
    MapChangeController();

    // Set dependencies
    void setRenderingCoordinator(RenderingCoordinator* coordinator) { renderingCoordinator_ = coordinator; }
    void setFrameDataProvider(FrameDataProvider* provider) { frameDataProvider_ = provider; }
    void setPlaybackController(PlaybackController* controller) { playbackController_ = controller; }
    void setEncounterTextureManager(EncounterTextureManager* manager) { encounterTextureManager_ = manager; }

    // Set current encounter ID for encounter-specific texture loading
    void setCurrentEncounterId(uint32_t encounterId) { currentEncounterId_ = encounterId; }

    // Load map change events from parsed log
    void loadMapChangeEvents(const std::vector<MapChangeEvent>& mapChanges);

    // Set initial map from first event (called when EventDatabase is loaded)
    // fallbackX/fallbackY: center for fallback grid when no map bounds available
    void setInitialMap(uint16_t mapId, float fallbackX = 0.0f, float fallbackY = 0.0f);

    // Update map based on actor positions (called every 0.5s)
    void updateMapFromActorPositions();

    // Preload upcoming map textures (10s lookahead, switch 3s ahead)
    void preloadUpcomingMapTextures();

    // Clear all map data (called when switching encounters)
    void clear();

    // Get current map ID
    uint16_t getCurrentMapId() const { return currentMapId_; }

    // Access map data by ID
    const MapChangeEvent* getMapData(uint16_t mapId) const;

private:
    // Anti-flicker: duration to ignore map changes after a switch
    static constexpr double MAP_STICKY_DURATION_S = 3.0;

    RenderingCoordinator* renderingCoordinator_ = nullptr;
    FrameDataProvider* frameDataProvider_ = nullptr;
    PlaybackController* playbackController_ = nullptr;
    EncounterTextureManager* encounterTextureManager_ = nullptr;

    std::vector<MapChangeEvent> mapChangeEvents_;
    std::unordered_map<uint16_t, MapChangeEvent> mapIdToData_;
    uint16_t currentMapId_ = 0;
    uint32_t currentEncounterId_ = 0;
    double lastMapChangeTime_ = 0.0;
    double lastCheckTime_ = 0.0;
};
