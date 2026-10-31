#pragma once

#include <array>
#include <string>

// Settings for the facing direction indicator cone on selected actors
struct FacingIndicatorSettings {
    bool enabled = false;                         // Show/hide the facing cone (off by default)
    float opacity = 0.7f;                         // 0.0 - 1.0
    float size = 1.0f;                            // Size in yards (0.5 - 5.0)
    std::array<float, 3> color = {1.0f, 1.0f, 0.0f};  // RGB (default: yellow)

    // Serialization
    std::string toJson() const;
    void fromJson(const std::string& json);
};
