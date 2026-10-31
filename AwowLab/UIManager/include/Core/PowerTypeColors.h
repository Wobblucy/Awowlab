#pragma once

#include "imgui.h"
#include <cstdint>

namespace ui {

// Get color for WoW power type (MANA, RAGE, ENERGY, etc.)
inline ImVec4 getPowerTypeColor(int8_t powerType) {
    switch (powerType) {
        case 0:  // MANA
            return ImVec4(0.0f, 0.0f, 1.0f, 1.0f);       // Blue
        case 1:  // RAGE
            return ImVec4(1.0f, 0.0f, 0.0f, 1.0f);       // Red
        case 2:  // FOCUS
            return ImVec4(1.0f, 0.5f, 0.25f, 1.0f);      // Orange-brown
        case 3:  // ENERGY
            return ImVec4(1.0f, 1.0f, 0.0f, 1.0f);       // Yellow
        case 6:  // RUNIC_POWER
            return ImVec4(0.0f, 0.82f, 1.0f, 1.0f);      // Cyan
        case 7:  // SOUL_SHARDS
            return ImVec4(0.5f, 0.32f, 0.55f, 1.0f);     // Purple
        case 8:  // LUNAR_POWER (Astral Power)
            return ImVec4(0.3f, 0.52f, 0.9f, 1.0f);      // Light blue
        case 9:  // HOLY_POWER
            return ImVec4(0.95f, 0.9f, 0.6f, 1.0f);      // Light gold
        case 11: // MAELSTROM
            return ImVec4(0.0f, 0.5f, 1.0f, 1.0f);       // Electric blue
        case 12: // CHI
            return ImVec4(0.7f, 1.0f, 0.6f, 1.0f);       // Light green
        case 13: // INSANITY
            return ImVec4(0.4f, 0.0f, 0.8f, 1.0f);       // Deep purple
        case 17: // FURY
            return ImVec4(0.79f, 0.26f, 0.99f, 1.0f);    // Violet
        case 18: // PAIN
            return ImVec4(1.0f, 0.61f, 0.0f, 1.0f);      // Orange
        case 19: // ESSENCE
            return ImVec4(0.2f, 0.58f, 0.5f, 1.0f);      // Teal (Evoker)
        default:
            return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);       // Gray for unknown
    }
}

// Get name for WoW power type
inline const char* getPowerTypeName(int8_t powerType) {
    switch (powerType) {
        case 0:  return "Mana";
        case 1:  return "Rage";
        case 2:  return "Focus";
        case 3:  return "Energy";
        case 6:  return "Runic Power";
        case 7:  return "Soul Shards";
        case 8:  return "Astral Power";
        case 9:  return "Holy Power";
        case 11: return "Maelstrom";
        case 12: return "Chi";
        case 13: return "Insanity";
        case 17: return "Fury";
        case 18: return "Pain";
        case 19: return "Essence";
        default: return "Unknown";
    }
}

} // namespace ui
