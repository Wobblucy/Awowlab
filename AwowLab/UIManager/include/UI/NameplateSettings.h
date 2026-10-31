#pragma once

#include <string>

// Per-category text appearance settings for nameplates
struct NameplateCategorySettings {
    float fontScale = 1.0f;                // 0.5 - 2.0 range
    bool showBackground = false;           // Toggle background behind text
    bool useActorColor = true;             // Default: use actor color
    float textColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // RGBA (used when above is false)
    float backgroundOpacity = 0.6f;        // 0.0 - 1.0 range
    int maxNameLength = 4;                 // 0 = show full name, 1-20 = truncate to N characters
};

struct NameplateSettings {
    // Global toggles
    bool showNameplates = true;
    bool showHealthBars = true;
    bool showAuras = true;
    float auraIconSize = 24.0f;  // Size of buff/debuff icons (16-48 range)
    bool showLastCast = false;   // Toggle for displaying last cast icon
    float lastCastIconSize = 20.0f;  // Size of last cast icon (16-32 range)

    // Per-category text appearance
    NameplateCategorySettings playerSettings;  // For Player actors
    NameplateCategorySettings otherSettings;   // For all non-Player actors

    // Serialization
    std::string toJson() const;
    void fromJson(const std::string& json);
};
