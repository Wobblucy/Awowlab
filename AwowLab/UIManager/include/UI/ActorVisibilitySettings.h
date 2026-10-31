#pragma once
#include "structures.h"  // For UnitReaction
#include "../../Database/OutputWriter.h"  // For ActorType

struct ActorVisibilitySettings {
    // Players (from GUID prefix "Player-")
    bool renderFriendlyPlayers = true;
    bool renderHostilePlayers = true;
    bool renderNeutralPlayers = true;

    // Creatures (from GUID prefix "Creature-")
    bool renderFriendlyCreatures = false;  // Off by default
    bool renderHostileCreatures = true;
    bool renderNeutralCreatures = true;

    // Vehicles (from GUID prefix "Vehicle-")
    bool renderFriendlyVehicles = false;  // Off by default
    bool renderHostileVehicles = true;
    bool renderNeutralVehicles = true;

    // Pets (from GUID prefix "Pet-")
    bool renderFriendlyPets = false;  // Off by default
    bool renderHostilePets = true;
    bool renderNeutralPets = true;

    // Backward-compatible: returns true if any pets should render
    bool renderPets() const {
        return renderFriendlyPets || renderHostilePets || renderNeutralPets;
    }

    // Check if specific actor should render based on type and reaction
    bool shouldRender(ActorType type, UnitReaction reaction) const {
        switch (type) {
            case ActorType::Player:
                switch (reaction) {
                    case UnitReaction::Friendly: return renderFriendlyPlayers;
                    case UnitReaction::Hostile: return renderHostilePlayers;
                    case UnitReaction::Neutral: return renderNeutralPlayers;
                }
                break;
            case ActorType::Creature:
                switch (reaction) {
                    case UnitReaction::Friendly: return renderFriendlyCreatures;
                    case UnitReaction::Hostile: return renderHostileCreatures;
                    case UnitReaction::Neutral: return renderNeutralCreatures;
                }
                break;
            case ActorType::Vehicle:
                switch (reaction) {
                    case UnitReaction::Friendly: return renderFriendlyVehicles;
                    case UnitReaction::Hostile: return renderHostileVehicles;
                    case UnitReaction::Neutral: return renderNeutralVehicles;
                }
                break;
            case ActorType::Pet:
                switch (reaction) {
                    case UnitReaction::Friendly: return renderFriendlyPets;
                    case UnitReaction::Hostile: return renderHostilePets;
                    case UnitReaction::Neutral: return renderNeutralPets;
                }
                break;
            default:
                return true;  // Unknown types always render
        }
        return true;
    }
};
