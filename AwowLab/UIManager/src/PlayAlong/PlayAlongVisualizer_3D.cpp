#include "PlayAlong/PlayAlongVisualizer.h"
#include "Camera/Camera3D.h"
#include "Rendering/SceneRenderer3D.h"
#include "Color/ActorColorGenerator.h"
#include "Database/EventDatabase.h"
#include "Database/DeathDatabase.h"
#include "AnnotationManager.h"
#include "AnnotationRenderer.h"
#include "Core/StringInterner.h"
#include "Core/ErrorLogger.h"
#include <vulkan/vulkan.h>
#include <unordered_set>

namespace PlayAlong {

void PlayAlongVisualizer::initializeClonePosition() {
    if (!eventDb_ || targetPlayerGuid_.empty()) {
        return;
    }

    // First position event for the target player, searching the whole log
    float x = 0.0f, y = 0.0f, facing = 0.0f;
    bool found = false;

    eventDb_->forEachEvent([&](const ActorEvent& event) {
        if (event.source_guid == targetPlayerGuid_) {
            x = event.x_loc;
            y = event.y_loc;
            found = true;
        }
        return !found;
    });

    if (!found) {
        AWOW_LOG_WARNING("PlayAlong", "No position events found for " + targetPlayerGuid_);
        return;
    }

    clone_.setPosition(x, y);
    clone_.setFacing(facing);

    camera3D_->setTarget(x, y, 0.0f);
}

bool PlayAlongVisualizer::getTargetPlayerPosition(float& x, float& y, float& facing) const {
    if (!eventDb_ || targetPlayerGuid_.empty()) {
        return false;
    }

    std::vector<ActorEvent> targetEvents;
    eventDb_->forEachEventInWindow(minTimeMs_, currentTimeMs_, [&](const ActorEvent& event) {
        if (event.source_guid == targetPlayerGuid_) {
            targetEvents.push_back(event);
        }
        return true;
    });

    if (targetEvents.empty()) {
        return false;
    }

    ActorPositionInterpolator interpolator;
    auto interpolatedPositions = interpolator.interpolatePositions(
        targetEvents, static_cast<int32_t>(currentTimeMs_), 5000);

    auto it = interpolatedPositions.find(targetPlayerGuid_);
    if (it != interpolatedPositions.end() && it->second.hasPosition) {
        x = it->second.x;
        y = it->second.y;
        facing = it->second.facing;
        return true;
    }

    return false;
}

void PlayAlongVisualizer::render3D(VkCommandBuffer commandBuffer, uint32_t width, uint32_t height) {
    if (!sceneRenderer_ || !sceneRenderer_->isInitialized() || !eventDb_) {
        return;
    }

    buildActorPositions();

    // The user's clone and the target's ghost are added as extra actors with made-up GUIDs
    static const std::string cloneGuid = "PlayAlongClone-User";
    InterpolatedPosition clonePos;
    clonePos.x = clone_.getX();
    clonePos.y = clone_.getY();
    clonePos.facing = clone_.getFacing();
    clonePos.hasPosition = true;
    clonePos.hasFacing = true;
    clonePos.lastEventTimestamp = static_cast<int32_t>(currentTimeMs_);
    clonePos.scaleOverride = 1.3f;  // 30% larger for visibility
    clonePos.orientedShape = true;  // Cone points where the clone is facing
    actorPositions_[cloneGuid] = clonePos;
    guidToName_[cloneGuid] = "You";

    ActorColor cloneColor{0.2f, 1.0f, 0.4f};  // green
    colorGenerator_->cacheColor(cloneGuid, cloneColor);
    UnitFlags cloneFlags;
    cloneFlags.affiliation = UnitAffiliation::Mine;
    cloneFlags.reaction = UnitReaction::Friendly;
    cloneFlags.controller = UnitController::Player;
    cloneFlags.unitType = UnitType::Player;
    colorGenerator_->cacheActorInfo(cloneGuid, ActorType::Player, cloneFlags);

    float ghostX, ghostY, ghostFacing;
    if (getTargetPlayerPosition(ghostX, ghostY, ghostFacing)) {
        static const std::string ghostGuid = "PlayAlongGhost-Target";
        InterpolatedPosition ghostPos;
        ghostPos.x = ghostX;
        ghostPos.y = ghostY;
        ghostPos.facing = ghostFacing;
        ghostPos.hasPosition = true;
        ghostPos.hasFacing = true;
        ghostPos.lastEventTimestamp = static_cast<int32_t>(currentTimeMs_);
        ghostPos.scaleOverride = 1.3f;  // 30% larger for visibility
        ghostPos.orientedShape = true;  // Cone points where the ghost is facing
        actorPositions_[ghostGuid] = ghostPos;
        guidToName_[ghostGuid] = targetPlayerName_;

        ActorColor ghostColor{0.4f, 0.6f, 1.0f};  // light blue
        colorGenerator_->cacheColor(ghostGuid, ghostColor);
        colorGenerator_->cacheActorInfo(ghostGuid, ActorType::Player, cloneFlags);
    }

    // The clone and ghost are friendly players, so friendly players must be drawn here.
    // buildActorPositions already decided which database actors are included.
    ActorVisibilitySettings renderSettings = visibilitySettings_;
    renderSettings.renderFriendlyPlayers = true;

    // SceneRenderer3D takes string_view maps; ours own their keys as std::string
    std::unordered_map<std::string_view, InterpolatedPosition> viewPositions;
    std::unordered_map<std::string_view, InterpolatedResource> viewResources;
    std::unordered_map<std::string_view, std::string_view> viewGuidToName;

    for (const auto& [k, v] : actorPositions_) {
        viewPositions[k] = v;
    }
    for (const auto& [k, v] : actorResources_) {
        viewResources[k] = v;
    }
    for (const auto& [k, v] : guidToName_) {
        viewGuidToName[k] = v;
    }

    VkExtent2D extent{width, height};
    sceneRenderer_->render(
        device_,
        commandBuffer,
        extent,
        viewPositions,
        viewResources,
        viewGuidToName,
        colorGenerator_,
        camera3D_,
        renderSettings,
        false,  // No nameplates for now
        ""      // No selected player
    );

    if (annotationsEnabled_ && annotationRenderer_ && annotationManager_ &&
        annotationRenderer_->isInitialized()) {

        annotationManager_->updatePositions(viewPositions, static_cast<int32_t>(currentTimeMs_));

        annotationRenderer_->render(
            device_,
            commandBuffer,
            extent,
            annotationManager_.get(),
            camera3D_.get(),
            static_cast<int32_t>(currentTimeMs_)
        );
    }
}

void PlayAlongVisualizer::buildActorPositions() {
    actorPositions_.clear();
    actorResources_.clear();
    guidToName_.clear();

    if (!eventDb_) {
        return;
    }

    // Collect events for actors we want to show:
    // 1. Hostile/neutral creatures and vehicles (enemies)
    // 2. Friendly players (other raid members, NOT the target player)
    std::vector<ActorEvent> windowEvents;

    // Friendly players are tracked by interner id to avoid string copies
    std::unordered_set<StringInterner::Id> friendlyPlayerIds;
    auto& interner = guidInterner();

    eventDb_->forEachEventInWindow(minTimeMs_, currentTimeMs_, [&](const ActorEvent& event) {
        if (deathDb_ && deathDb_->shouldHide(event.source_guid, static_cast<int32_t>(currentTimeMs_))) {
            return true;
        }

        bool shouldInclude = false;
        bool isFriendlyPlayer = false;

        // Hostile/neutral creatures and vehicles (enemies)
        if (event.actor_type == ActorType::Creature || event.actor_type == ActorType::Vehicle) {
            if (event.flags.reaction == UnitReaction::Hostile ||
                event.flags.reaction == UnitReaction::Neutral) {
                shouldInclude = true;
            }
        }

        // Friendly players (other raid members) - NOT the target player (ghost is separate)
        if (event.actor_type == ActorType::Player &&
            event.flags.reaction == UnitReaction::Friendly &&
            event.source_guid != targetPlayerGuid_) {
            shouldInclude = true;
            isFriendlyPlayer = true;
        }

        if (shouldInclude) {
            windowEvents.push_back(event);

            if (isFriendlyPlayer) {
                StringInterner::Id id = interner.intern(event.source_guid);
                friendlyPlayerIds.insert(id);
            }

            guidToName_[std::string(event.source_guid)] = std::string(event.source_actor);
        }

        return true;
    });

    ActorPositionInterpolator interpolator;
    auto interpolatedPositions = interpolator.interpolatePositions(
        windowEvents, static_cast<int32_t>(currentTimeMs_), 5000);

    // Other friendly players are drawn at 30% opacity
    for (const auto& [guidView, pos] : interpolatedPositions) {
        std::string guid(guidView);
        InterpolatedPosition finalPos = pos;

        StringInterner::Id id = interner.intern(guidView);
        if (friendlyPlayerIds.count(id) > 0) {
            finalPos.opacity = 0.3f;
        }

        actorPositions_[guid] = finalPos;
    }

    updateMapFromActorPositions();
}

void PlayAlongVisualizer::updateMapFromActorPositions() {
    if (!eventDb_ || !sceneRenderer_ || mapIdToData_.empty()) {
        return;
    }

    // Latest non-zero map_id in the window
    uint16_t detectedMapId = 0;
    eventDb_->forEachEventInWindow(minTimeMs_, currentTimeMs_, [&](const ActorEvent& event) {
        if (event.map_id != 0) {
            detectedMapId = event.map_id;
        }
        return true;
    });

    if (detectedMapId == 0 || detectedMapId == currentMapId_) {
        return;
    }

    auto it = mapIdToData_.find(detectedMapId);
    if (it != mapIdToData_.end()) {
        const MapChangeEvent& mapData = it->second;
        currentMapId_ = detectedMapId;
        sceneRenderer_->setMapBounds(
            mapData.map_id,
            mapData.map_name,
            mapData.x1, mapData.y1,
            mapData.x2, mapData.y2
        );
    } else {
        // No bounds from the log; SceneRenderer3D falls back to WorldMapTextureManager
        currentMapId_ = detectedMapId;
        sceneRenderer_->setMapBounds(detectedMapId, "", 0, 0, 0, 0);
    }
}

} // namespace PlayAlong
