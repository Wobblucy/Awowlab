#include "UI/Overlay/UIOverlayControls.h"
#include "UI/AwlUI/Widgets.h"

bool UIOverlayControls::renderLockToggle(bool isLocked, std::function<void(bool)> onToggle,
                                          int outScreenRect[4]) {
    // Locked = mouse passes through to WoW; we highlight this as the
    // "danger" state because it's how the user makes the overlay
    // unclickable. Unlocked = Ghost (subtle) since it's the default.
    const char* label = isLocked ? "Locked" : "Unlocked";
    const auto variant = isLocked
        ? awlui::ButtonVariant::Danger
        : awlui::ButtonVariant::Ghost;
    bool clicked = awlui::Button(label, variant, awlui::ButtonSize::Sm);

    if (outScreenRect) {
        // Overlay coords - caller adds window position for
        // screen-absolute WM_NCHITTEST math.
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        outScreenRect[0] = static_cast<int>(rmin.x);
        outScreenRect[1] = static_cast<int>(rmin.y);
        outScreenRect[2] = static_cast<int>(rmax.x);
        outScreenRect[3] = static_cast<int>(rmax.y);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isLocked
            ? "Click to unlock. When locked, clicks pass through to\nthe game everywhere except this button."
            : "Overlay captures mouse input.\nClick to lock and let clicks pass through.\nCtrl+Left/Right cycles meter views.");
    }

    if (clicked && onToggle) {
        onToggle(!isLocked);
    }
    return clicked;
}
