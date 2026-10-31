#include "PlayAlong/PlayAlongVisualizer.h"
#include "Camera/Camera3D.h"
#include "Rendering/SceneRenderer3D.h"
#include "UI/EncounterTextureManager.h"
#include "Color/ActorColorGenerator.h"
#include "Database/EventDatabase.h"
#include "Database/SpellCastDatabase.h"
#include "Database/DeathDatabase.h"
#include "Database/OutputWriter.h"  // For CombatantInfoEvent, MapChangeEvent
#include "AutoAnnotationTriggerManager.h"
#include "Core/CurrentEncounter.h"

namespace PlayAlong {

void PlayAlongVisualizer::loadEventDatabase(const EventDatabase* db) {
    eventDb_ = db;
    if (db) {
        minTimeMs_ = db->getMinTimestamp();
        maxTimeMs_ = db->getMaxTimestamp();
        currentTimeMs_ = minTimeMs_;

        db->forEachEvent([this](const ActorEvent& event) {
            colorGenerator_->cacheActorInfo(event.source_guid, event.actor_type, event.flags);
            colorGenerator_->cacheActorName(event.source_guid, event.source_actor);
            return true;
        });
    }
}

void PlayAlongVisualizer::loadSpellCastDatabase(const SpellCastDatabase* db) {
    spellCastDb_ = db;
}

void PlayAlongVisualizer::setSpellIconLoader(SpellIconLoader* loader) {
    spellIconLoader_ = loader;
}

void PlayAlongVisualizer::loadMapChangeEvents(const std::vector<MapChangeEvent>& mapChanges) {
    mapChangeEvents_ = mapChanges;
    mapIdToData_.clear();
    currentMapId_ = 0;

    if (!sceneRenderer_) {
        return;
    }

    for (const auto& mapEvent : mapChanges) {
        uint16_t mapId = static_cast<uint16_t>(mapEvent.map_id);
        if (mapIdToData_.find(mapId) == mapIdToData_.end()) {
            mapIdToData_[mapId] = mapEvent;
        }
    }

    // An encounter-specific texture wins over the zone map
    if (encounterId_ > 0 && encounterTextureManager_ && encounterTextureManager_->isInitialized()) {
        MapWorldBounds encounterBounds = encounterTextureManager_->setCurrentEncounter(encounterId_);
        if (encounterBounds.valid) {
            auto [texWidth, texHeight] = encounterTextureManager_->getCurrentTextureDimensions();
            sceneRenderer_->setMapBoundsWithTexture(
                encounterBounds.minX, encounterBounds.minY,
                encounterBounds.maxX, encounterBounds.maxY,
                encounterTextureManager_->getCurrentImageView(),
                encounterTextureManager_->getCurrentSampler(),
                texWidth, texHeight);

            if (!mapChanges.empty()) {
                currentMapId_ = static_cast<uint16_t>(mapChanges.front().map_id);
            }
            return;
        }
    }

    // Otherwise the zone map from the first MAP_CHANGE event
    if (mapChanges.empty()) {
        return;
    }

    const auto& firstMap = mapChanges.front();
    currentMapId_ = static_cast<uint16_t>(firstMap.map_id);
    sceneRenderer_->setMapBounds(
        firstMap.map_id,
        firstMap.map_name,
        firstMap.x1, firstMap.y1,
        firstMap.x2, firstMap.y2
    );
}

void PlayAlongVisualizer::loadDeathEvents(const std::vector<DeathEvent>& deaths,
                                           const std::vector<ResurrectEvent>& resurrects,
                                           const EventDatabase* eventDb) {
    deathDb_ = std::make_unique<DeathDatabase>();
    deathDb_->loadEvents(deaths, resurrects, eventDb);
}

void PlayAlongVisualizer::loadAnnotationDatabases(const CombatDatabase* combatDb,
                                                   const AuraDatabase* auraDb,
                                                   const std::unordered_map<uint32_t, std::string>* spellNameMap) {
    combatDb_ = combatDb;
    auraDb_ = auraDb;

    if (autoAnnotationTriggerManager_ && spellNameMap) {
        autoAnnotationTriggerManager_->setSpellNameMap(spellNameMap);
    }

    annotationCacheBuilt_ = false;
}

void PlayAlongVisualizer::setEncounterInfo(uint32_t encounterId, const std::string& encounterName) {
    encounterId_ = encounterId;
    encounterName_ = encounterName;

    // The annotation loader reads the encounter from this singleton
    CurrentEncounter::instance().set(encounterName, encounterId);

    if (autoAnnotationTriggerManager_) {
        autoAnnotationTriggerManager_->loadFromFile();
        autoAnnotationTriggerManager_->invalidateCache();
    }

    annotationCacheBuilt_ = false;
}

void PlayAlongVisualizer::setTargetPlayer(const std::string& guid, const std::string& name) {
    targetPlayerGuid_ = guid;
    targetPlayerName_ = name;

    snapHintShown_ = false;
    snapHintTimeRemaining_ = 0.0f;

    initializeClonePosition();
}

void PlayAlongVisualizer::setCombatantInfo(const std::vector<CombatantInfoEvent>& combatants,
                                            const EventDatabase* eventDb) {
    // Only players with at least one position event are offered; the rest never took part
    std::vector<PlayerInfo> players;
    players.reserve(combatants.size());

    for (const auto& combatant : combatants) {
        PlayerInfo info;
        info.guid = combatant.player_guid;
        info.specId = combatant.spec_id;
        info.name = combatant.player_guid;  // fallback if no event carries a name
        bool found = false;

        if (eventDb) {
            eventDb->forEachEvent([&](const ActorEvent& event) {
                if (event.source_guid == combatant.player_guid) {
                    info.name = event.source_actor;
                    found = true;
                    return false;
                }
                return true;
            });
        }

        if (found) {
            players.push_back(std::move(info));
        }

        // Spec is cached for every combatant, present or not
        if (!combatant.player_guid.empty() && combatant.spec_id > 0) {
            colorGenerator_->cacheSpecId(combatant.player_guid, combatant.spec_id);
        }
    }

    playerSelector_.setPlayers(players);
}

void PlayAlongVisualizer::resetNavigationFlags() {
    returnToLandingPage_ = false;
    returnToEncounterSelector_ = false;
}

} // namespace PlayAlong
