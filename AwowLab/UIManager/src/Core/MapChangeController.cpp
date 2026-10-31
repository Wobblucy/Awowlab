#include "Core/MapChangeController.h"
#include "Core/RenderingCoordinator.h"
#include "Core/FrameDataProvider.h"
#include "Playback/PlaybackController.h"
#include "UI/EncounterTextureManager.h"
#include "../../Database/OutputWriter.h"  // For MapChangeEvent
#include "Core/CurrentEncounter.h"
#include <imgui.h>

MapChangeController::MapChangeController() = default;

void MapChangeController::loadMapChangeEvents(const std::vector<MapChangeEvent>& mapChanges) {
    mapChangeEvents_ = mapChanges;
    mapIdToData_.clear();
    currentMapId_ = 0;

    // Build mapId -> full event data lookup (includes bounds for floor positioning)
    for (const auto& mapEvent : mapChanges) {
        if (mapIdToData_.find(static_cast<uint16_t>(mapEvent.map_id)) == mapIdToData_.end()) {
            mapIdToData_[static_cast<uint16_t>(mapEvent.map_id)] = mapEvent;
        }
    }
}

void MapChangeController::setInitialMap(uint16_t mapId, float fallbackX, float fallbackY) {
    if (mapId != 0 && mapId == currentMapId_) {
        return;
    }

    // An encounter-specific texture overrides mapId-based loading. It is keyed
    // by encounter id, so it works even when mapId == 0.
    if (currentEncounterId_ > 0 && encounterTextureManager_ && renderingCoordinator_) {
        MapWorldBounds encounterBounds = encounterTextureManager_->setCurrentEncounter(currentEncounterId_);
        if (encounterBounds.valid) {
            if (mapId != 0) {
                currentMapId_ = mapId;
                CurrentEncounter::instance().setMapId(mapId);
            }

            auto [texWidth, texHeight] = encounterTextureManager_->getCurrentTextureDimensions();
            renderingCoordinator_->setMapBoundsWithTexture(
                encounterBounds.minX, encounterBounds.minY,
                encounterBounds.maxX, encounterBounds.maxY,
                encounterTextureManager_->getCurrentImageView(),
                encounterTextureManager_->getCurrentSampler(),
                texWidth, texHeight);
            // Pass encounter texture directly to minimap (syncMinimapWorldMap only checks WorldMapTextureManager)
            renderingCoordinator_->setMinimapWorldMap(
                encounterBounds.minX, encounterBounds.maxX,
                encounterBounds.minY, encounterBounds.maxY,
                encounterTextureManager_->getCurrentImGuiTextureID());
            return;
        }
    }

    if (mapId == 0) {
        return;
    }

    currentMapId_ = mapId;

    // Snapshot export reads the resolved map ID from CurrentEncounter
    CurrentEncounter::instance().setMapId(mapId);

    auto it = mapIdToData_.find(mapId);
    if (it != mapIdToData_.end() && renderingCoordinator_) {
        const MapChangeEvent& mapData = it->second;
        bool hasValidBounds = (mapData.x1 != 0.0f || mapData.y1 != 0.0f ||
                               mapData.x2 != 0.0f || mapData.y2 != 0.0f);
        if (hasValidBounds) {
            renderingCoordinator_->setMapBounds(mapId, mapData.map_name,
                                                 mapData.x1, mapData.y1,
                                                 mapData.x2, mapData.y2);
        } else {
            // MAP_CHANGE bounds are zero - use first actor position as fallback
            float minX = fallbackX - 150.0f;
            float minY = fallbackY - 150.0f;
            float maxX = fallbackX + 150.0f;
            float maxY = fallbackY + 150.0f;
            renderingCoordinator_->setMapBounds(mapId, mapData.map_name, minX, minY, maxX, maxY);
        }
        renderingCoordinator_->syncMinimapWorldMap();
    } else if (renderingCoordinator_) {
        // No MAP_CHANGE data for this map - use actor position bounds if available
        float minX = 0, minY = 0, maxX = 0, maxY = 0;
        if (frameDataProvider_ && frameDataProvider_->hasBounds()) {
            minX = frameDataProvider_->getMinX();
            minY = frameDataProvider_->getMinY();
            maxX = frameDataProvider_->getMaxX();
            maxY = frameDataProvider_->getMaxY();
        } else if (fallbackX != 0.0f || fallbackY != 0.0f) {
            // Use first actor position as fallback center (300x300 grid)
            minX = fallbackX - 150.0f;
            minY = fallbackY - 150.0f;
            maxX = fallbackX + 150.0f;
            maxY = fallbackY + 150.0f;
        }
        // Called even with zero bounds; setMapBounds then builds a fallback grid floor
        renderingCoordinator_->setMapBounds(mapId, "", minX, minY, maxX, maxY);
        renderingCoordinator_->syncMinimapWorldMap();
    }
}

void MapChangeController::updateMapFromActorPositions() {
    double currentTime = ImGui::GetTime();

    if (currentTime - lastCheckTime_ < 0.5) {
        return;
    }
    lastCheckTime_ = currentTime;

    if (!frameDataProvider_) {
        return;
    }

    uint16_t actorMapId = frameDataProvider_->getCurrentMapId();
    if (actorMapId == 0 || actorMapId == currentMapId_) {
        return;
    }

    // Anti-flicker: don't switch if we just changed maps
    if (currentTime - lastMapChangeTime_ < MAP_STICKY_DURATION_S) {
        return;
    }

    currentMapId_ = actorMapId;
    lastMapChangeTime_ = currentTime;

    CurrentEncounter::instance().setMapId(actorMapId);

    if (renderingCoordinator_) {
        // Actor position bounds, used for the fallback grid floor
        float minX = 0, minY = 0, maxX = 0, maxY = 0;
        if (frameDataProvider_ && frameDataProvider_->hasBounds()) {
            minX = frameDataProvider_->getMinX();
            minY = frameDataProvider_->getMinY();
            maxX = frameDataProvider_->getMaxX();
            maxY = frameDataProvider_->getMaxY();
        }
        renderingCoordinator_->setMapBounds(actorMapId, "", minX, minY, maxX, maxY);
        renderingCoordinator_->syncMinimapWorldMap();
    }
}

void MapChangeController::preloadUpcomingMapTextures() {
    if (mapChangeEvents_.empty() || !playbackController_ || !renderingCoordinator_) {
        return;
    }

    int32_t currentTime = playbackController_->getCurrentTime();
    constexpr int32_t PRELOAD_WINDOW_MS = 10000;
    constexpr int32_t SWITCH_WINDOW_MS = 3000;

    for (const auto& mapEvent : mapChangeEvents_) {
        int32_t timeUntilEvent = mapEvent.timestamp_ms - currentTime;

        if (timeUntilEvent > 0 && timeUntilEvent <= PRELOAD_WINDOW_MS) {
            renderingCoordinator_->preloadMapTexture(mapEvent.map_id);
        }

        // Switch a little before the event fires
        if (timeUntilEvent > 0 && timeUntilEvent <= SWITCH_WINDOW_MS) {
            if (mapEvent.map_id != currentMapId_) {
                // Anti-flicker: don't switch if we just changed maps
                double currentRealTime = ImGui::GetTime();
                if (currentRealTime - lastMapChangeTime_ < MAP_STICKY_DURATION_S) {
                    break;
                }

                currentMapId_ = static_cast<uint16_t>(mapEvent.map_id);
                lastMapChangeTime_ = currentRealTime;

                CurrentEncounter::instance().setMapId(currentMapId_);

                renderingCoordinator_->setMapBounds(mapEvent.map_id, mapEvent.map_name,
                                                     mapEvent.x1, mapEvent.y1,
                                                     mapEvent.x2, mapEvent.y2);
                renderingCoordinator_->syncMinimapWorldMap();
            }
            break; // Only handle the first upcoming switch
        }
    }
}

void MapChangeController::clear() {
    mapChangeEvents_.clear();
    mapIdToData_.clear();
    currentMapId_ = 0;
    lastMapChangeTime_ = 0.0;
    lastCheckTime_ = 0.0;

    if (renderingCoordinator_) {
        renderingCoordinator_->clearMapBounds();
    }
}

const MapChangeEvent* MapChangeController::getMapData(uint16_t mapId) const {
    auto it = mapIdToData_.find(mapId);
    if (it != mapIdToData_.end()) {
        return &it->second;
    }
    return nullptr;
}
