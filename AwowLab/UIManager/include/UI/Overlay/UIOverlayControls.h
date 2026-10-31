#pragma once

#include "imgui.h"
#include <functional>

// Control bar for the overlay meter.
class UIOverlayControls {
public:
    UIOverlayControls() = default;

    // Render a lock/unlock toggle button that flips click-through state.
    // When locked, mouse events pass through to whatever is behind the
    // overlay (i.e. the game window). When unlocked, the overlay
    // captures mouse input so the user can drag and click. Returns
    // true if the button was clicked this frame.
    //
    // If outScreenRect is non-null, it receives the button's screen
    // rectangle {left, top, right, bottom} so the caller can register
    // it as a click-through escape hatch.
    bool renderLockToggle(bool isLocked, std::function<void(bool)> onToggle,
                          int outScreenRect[4] = nullptr);
};
