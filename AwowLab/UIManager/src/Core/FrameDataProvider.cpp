#include "Core/FrameDataProvider.h"
#include "Core/FrameProfiler.h"
#include "Color/ActorColorGenerator.h"
#include "Playback/PlaybackController.h"
#include "Animation/ActorPositionInterpolator.h"
#include "UI/UIActorSelector.h"
#include "../../Database/EventDatabase.h"
#include "../../Database/SpellCastDatabase.h"
#include "../../Database/ResourceDatabase.h"
#include "../../Database/DeathDatabase.h"
#include "../../Database/AuraDatabase.h"
#include "../../Database/AvoidanceDatabase.h"
#include "../../Core/Utils/ColorUtils.h"  // For awow::generateActorColor
#include <algorithm>

FrameDataProvider::FrameDataProvider()
    : eventDatabase_(nullptr)
    , spellCastDatabase_(nullptr)
    , resourceDatabase_(nullptr)
    , combatDatabase_(nullptr)
    , deathDatabase_(nullptr)
    , auraDatabase_(nullptr)
    , dispelDatabase_(nullptr)
    , avoidanceDatabase_(nullptr)
    , emoteDatabase_(nullptr)
    , spellNameMap_(nullptr)
    , guidToNameMap_(nullptr)
    , actorSelector_(nullptr)
    , cachedMinX_(0), cachedMaxX_(0)
    , cachedMinY_(0), cachedMaxY_(0)
    , boundsCached_(false)
    , currentMapId_(0)
    , lastCreatureEventTime_(0)
{
}

FrameDataProvider::~FrameDataProvider() = default;

void FrameDataProvider::setPlaybackController(std::shared_ptr<PlaybackController> playback) {
    playbackController_ = std::move(playback);
}

void FrameDataProvider::setPositionInterpolator(std::shared_ptr<ActorPositionInterpolator> interpolator) {
    positionInterpolator_ = std::move(interpolator);
}

void FrameDataProvider::setColorGenerator(std::shared_ptr<ActorColorGenerator> colorGen) {
    colorGenerator_ = std::move(colorGen);
}

void FrameDataProvider::setActorSelector(UIActorSelector* selector) {
    actorSelector_ = selector;
}

void FrameDataProvider::loadEventDatabase(const EventDatabase* db) {
    eventDatabase_ = db;
    boundsCached_ = false;
    currentMapId_ = 0;
    lastCreatureEventTime_ = 0;
    frameCache_.clear();

    if (colorGenerator_) {
        colorGenerator_->clear();
    }

    if (!db || db->empty()) {
        if (playbackController_) {
            playbackController_->loadTimeRange(0, 0);
        }
        return;
    }

    // Signed: pre-encounter events have negative timestamps
    int32_t minTimestamp = db->getMinTimestamp();
    int32_t maxTimestamp = db->getMaxTimestamp();
    if (playbackController_) {
        playbackController_->loadTimeRange(minTimestamp, maxTimestamp);
    }

    if (positionInterpolator_) {
        positionInterpolator_->reset();
    }

    // Cache actor info and compute bounds in one pass. Colors are not set here:
    // players get class colors from loadCombatantInfo(), everything else from
    // generateFallbackColors() afterwards.
    std::unordered_map<std::string, std::string> allPlayers;  // GUID -> Name
    bool firstEvent = true;
    uint16_t boundsMapId = 0;

    db->forEachEvent([&](const ActorEvent& event) {
        if (colorGenerator_ && !colorGenerator_->hasColor(event.source_guid)) {
            colorGenerator_->cacheActorInfo(event.source_guid, event.actor_type, event.flags);
            colorGenerator_->cacheActorName(event.source_guid, event.source_actor);

            // Hostile/neutral actors are listed by name in the Enemies tab
            if (event.actor_type == ActorType::Creature &&
                (event.flags.isHostile() || event.flags.isNeutral())) {
                colorGenerator_->cacheNonFriendlyActor(event.source_guid, event.source_actor, event.flags);
            }

            frameCache_.cacheActorInfo(event);
        }

        if (event.actor_type == ActorType::Player) {
            allPlayers[std::string(event.source_guid)] = std::string(event.source_actor);
        }

        // Bounds only use events on the first event's map, so coordinates from
        // different zones are never mixed
        if (firstEvent) {
            boundsMapId = event.map_id;
            cachedMinX_ = event.x_loc;
            cachedMaxX_ = event.x_loc;
            cachedMinY_ = event.y_loc;
            cachedMaxY_ = event.y_loc;
            firstEvent = false;
        } else if (event.map_id == boundsMapId) {
            cachedMinX_ = std::min(cachedMinX_, event.x_loc);
            cachedMaxX_ = std::max(cachedMaxX_, event.x_loc);
            cachedMinY_ = std::min(cachedMinY_, event.y_loc);
            cachedMaxY_ = std::max(cachedMaxY_, event.y_loc);
        }
    });

    frameCache_.markGuidMapsPopulated();

    if (actorSelector_ && colorGenerator_) {
        actorSelector_->updateKnownPlayers(colorGenerator_, allPlayers);
    }

    boundsCached_ = !firstEvent;
}

void FrameDataProvider::loadSpellCastDatabase(const SpellCastDatabase* db) {
    spellCastDatabase_ = db;
}

void FrameDataProvider::loadResourceDatabase(const ResourceDatabase* db) {
    resourceDatabase_ = db;
}

void FrameDataProvider::loadCombatDatabase(const CombatDatabase* db) {
    combatDatabase_ = db;
}

void FrameDataProvider::loadDeathDatabase(const DeathDatabase* db) {
    deathDatabase_ = db;
}

void FrameDataProvider::loadAuraDatabase(const AuraDatabase* db) {
    auraDatabase_ = db;
}

void FrameDataProvider::loadDispelDatabase(const DispelDatabase* db) {
    dispelDatabase_ = db;
}

void FrameDataProvider::loadAvoidanceDatabase(const AvoidanceDatabase* db) {
    avoidanceDatabase_ = db;
}

void FrameDataProvider::loadEmoteDatabase(const EmoteDatabase* db) {
    emoteDatabase_ = db;
}

void FrameDataProvider::loadSpellNameMap(const std::unordered_map<uint32_t, std::string>* spellNameMap) {
    spellNameMap_ = spellNameMap;
}

void FrameDataProvider::loadGuidToNameMap(const std::unordered_map<std::string, std::string>* guidToNameMap) {
    guidToNameMap_ = guidToNameMap;
}

bool FrameDataProvider::shouldHideActor(std::string_view guid) const {
    if (!deathDatabase_ || !playbackController_) {
        return false;
    }
    uint32_t currentTime = playbackController_->getCurrentTime();
    return deathDatabase_->shouldHide(guid, currentTime);
}

bool FrameDataProvider::hasEvents() const {
    return eventDatabase_ && !eventDatabase_->empty();
}

void FrameDataProvider::updateFrame() {
    FRAME_PROFILE_SCOPE("UpdateFrame");

    if (!eventDatabase_ || eventDatabase_->empty() || !playbackController_) {
        return;
    }

    uint32_t currentTime = playbackController_->getCurrentTime();
    uint32_t timeWindow = playbackController_->getTimeWindow();

    bool needsEventRefresh = !frameCache_.isValid(currentTime, timeWindow);

    if (needsEventRefresh) {
        FRAME_PROFILE_SCOPE("WindowEvents");
        {
            FRAME_PROFILE_SCOPE("WindowEvents::Query");
            windowEventsBuffer_.clear();
            eventDatabase_->forEachEventInWindow(currentTime, timeWindow, [&](const ActorEvent& event) {
                windowEventsBuffer_.push_back(event);
            });
        }
        {
            FRAME_PROFILE_SCOPE("WindowEvents::Cache");
            // Swap semantics: the cache takes this buffer and we get its old
            // one back, so neither side reallocates on the next refresh
            frameCache_.setWindowEvents(currentTime, timeWindow, std::move(windowEventsBuffer_));
        }
    }

    // Positions are interpolated every frame, even when the event window is reused
    if (positionInterpolator_ && !frameCache_.getWindowEvents().empty()) {
        FRAME_PROFILE_SCOPE("Interpolate");
        positionInterpolator_->interpolatePositionsInto(
            actorPositionsBuffer_, frameCache_.getWindowEvents(), currentTime);
        frameCache_.setActorPositions(actorPositionsBuffer_);
    }

    // Health/power only refresh with the event window; they don't need 60fps
    if (needsEventRefresh && resourceDatabase_ && !resourceDatabase_->empty() && frameCache_.hasPositions()) {
        FRAME_PROFILE_SCOPE("Resources");
        const auto& actorPositions = frameCache_.getActorPositions();

        visibleGuidsBuffer_.clear();
        visibleGuidsBuffer_.reserve(actorPositions.size());
        for (const auto& [guid, pos] : actorPositions) {
            visibleGuidsBuffer_.push_back(guid);
        }

        // ResourceDatabase takes std::string
        std::vector<std::string> guidsAsStrings;
        guidsAsStrings.reserve(visibleGuidsBuffer_.size());
        for (const auto& sv : visibleGuidsBuffer_) {
            guidsAsStrings.emplace_back(sv);
        }
        auto resourceEvents = resourceDatabase_->getResourcesAt(guidsAsStrings, currentTime);

        actorResourcesBuffer_.clear();

        for (const auto& [guidStr, resEvent] : resourceEvents) {
            InterpolatedResource ir{};
            ir.currentHealth = resEvent.current_health;
            ir.maxHealth = resEvent.max_health;
            ir.healthPercent = (resEvent.max_health > 0)
                ? static_cast<float>(resEvent.current_health) / static_cast<float>(resEvent.max_health)
                : 0.0f;
            ir.currentPower = resEvent.current_power;
            ir.maxPower = resEvent.max_power;
            ir.powerPercent = (resEvent.max_power > 0)
                ? static_cast<float>(resEvent.current_power) / static_cast<float>(resEvent.max_power)
                : 0.0f;
            ir.powerType = resEvent.power_type;
            ir.hasResource = true;
            // Reuse the interner-backed string_view key from actorPositions
            auto posIt = actorPositions.find(std::string_view(guidStr));
            if (posIt != actorPositions.end()) {
                actorResourcesBuffer_.emplace(posIt->first, ir);
            }
        }

        frameCache_.setActorResources(actorResourcesBuffer_);
    }
}

void FrameDataProvider::checkMapChange() {
    if (!hasEvents() || !frameCache_.hasPositions()) {
        return;
    }

    const auto& windowEvents = frameCache_.getWindowEvents();
    uint32_t currentTime = frameCache_.getCachedTime();

    if (!windowEvents.empty()) {
        uint16_t newMapId = windowEvents[0].map_id;
        bool mapChanged = (currentMapId_ != 0 && newMapId != currentMapId_);

        bool hasCreatureEvent = false;
        for (const auto& event : windowEvents) {
            if (event.actor_type == ActorType::Creature && static_cast<int32_t>(event.timestamp_ms) <= static_cast<int32_t>(currentTime)) {
                hasCreatureEvent = true;
                lastCreatureEventTime_ = event.timestamp_ms;
                break;
            }
        }

        bool inactivityTimeout = (lastCreatureEventTime_ > 0 &&
                                 currentTime > lastCreatureEventTime_ + INACTIVITY_RESET_MS);

        if (mapChanged || inactivityTimeout) {
            recalculateBounds(windowEvents);
            currentMapId_ = newMapId;
            if (inactivityTimeout) {
                lastCreatureEventTime_ = 0;
            }
        } else if (currentMapId_ == 0) {
            currentMapId_ = newMapId;
        }
    }
}

void FrameDataProvider::recalculateBounds() {
    if (!hasEvents()) {
        return;
    }
    recalculateBounds(frameCache_.getWindowEvents());
}

void FrameDataProvider::recalculateBounds(const std::vector<ActorEvent>& events) {
    if (events.empty()) {
        return;
    }

    // Minimap bounds come from players on the first event's map
    uint16_t targetMapId = events[0].map_id;
    bool firstMatch = true;

    for (const auto& event : events) {
        if (event.map_id != targetMapId) {
            continue;
        }
        if (event.actor_type != ActorType::Player) {
            continue;
        }

        if (firstMatch) {
            cachedMinX_ = event.x_loc;
            cachedMaxX_ = event.x_loc;
            cachedMinY_ = event.y_loc;
            cachedMaxY_ = event.y_loc;
            firstMatch = false;
        } else {
            cachedMinX_ = std::min(cachedMinX_, event.x_loc);
            cachedMaxX_ = std::max(cachedMaxX_, event.x_loc);
            cachedMinY_ = std::min(cachedMinY_, event.y_loc);
            cachedMaxY_ = std::max(cachedMaxY_, event.y_loc);
        }
    }

    // 15% padding, with a floor for tightly clustered players
    if (!firstMatch) {
        float paddingX = (cachedMaxX_ - cachedMinX_) * 0.15f;
        float paddingY = (cachedMaxY_ - cachedMinY_) * 0.15f;
        paddingX = std::max(paddingX, 10.0f);
        paddingY = std::max(paddingY, 10.0f);
        cachedMinX_ -= paddingX;
        cachedMaxX_ += paddingX;
        cachedMinY_ -= paddingY;
        cachedMaxY_ += paddingY;
    }

    boundsCached_ = !firstMatch;
}

void FrameDataProvider::generateFallbackColors() {
    if (!eventDatabase_ || !colorGenerator_) {
        return;
    }

    // Runs after loadCombatantInfo(), so only actors without a class color are left
    eventDatabase_->forEachEvent([this](const ActorEvent& event) {
        if (!colorGenerator_->hasColor(event.source_guid)) {
            ActorColor fallback = awow::generateActorColor(event.actor_type, event.flags, event.source_guid);
            colorGenerator_->cacheColor(event.source_guid, fallback);
        }
    });
}
