#pragma once

#include "AnnotationTypes.h"
#include <optional>
#include <string>
#include <cstdint>

class AnnotationManager;

// Right-click context menu for creating annotations
class UIAnnotationContextMenu {
public:
    UIAnnotationContextMenu() = default;

    struct ActorNavigationRequest {
        enum class Target { None, AppearancePanel, SpellSettings };
        Target target = Target::None;
        std::string actorGuid;
        std::string actorName;
        uint16_t specId = 0;
    };

    // Call on a right-click in the viewport. The actor fields are empty / 0
    // when no actor is near the click; the spec ID is 0 for enemies.
    void openAt(float worldX, float worldY,
                const std::string& nearbyActorGuid = "",
                const std::string& nearbyActorName = "",
                uint16_t nearbyActorSpecId = 0);

    // Returns the ID of a newly created annotation, or 0. Call each frame
    // after ImGui::Begin.
    uint64_t render(AnnotationManager* manager, int32_t currentTimestamp_ms);

    bool isOpen() const { return isOpen_; }

    void close() { isOpen_ = false; }

    // Returns the pending request and clears it
    static ActorNavigationRequest consumePendingNavigation();

private:
    bool isOpen_ = false;
    float pendingWorldX_ = 0.0f;
    float pendingWorldY_ = 0.0f;
    std::string pendingActorGuid_;
    std::string pendingActorName_;
    uint16_t pendingActorSpecId_ = 0;

    // Static so callers can read it without holding the menu
    static ActorNavigationRequest s_pendingNavigation;
};
