#pragma once
#include "AvoidanceDatabase.h"
#include "Core/LocalizationManager.h"

// Display label for an avoidance type, shared by the meter tooltip and
// the avoidance breakdown panel
inline const char* missTypeLabel(MissType type) {
    switch (type) {
        case MissType::DODGE:   return L("meter.avoidance_dodge");
        case MissType::PARRY:   return L("meter.avoidance_parry");
        case MissType::BLOCK:   return L("meter.avoidance_block");
        case MissType::MISS:    return L("meter.avoidance_miss");
        case MissType::DEFLECT: return L("meter.avoidance_deflect");
        case MissType::IMMUNE:  return L("meter.avoidance_immune");
        case MissType::RESIST:  return L("meter.avoidance_resist");
        case MissType::REFLECT: return L("meter.avoidance_reflect");
        case MissType::ABSORB:  return L("meter.avoidance_absorb");
        default:                return L("meter.avoidance_other");
    }
}
