#pragma once

/**
 * UIHelpPanel - Displays keyboard shortcuts and controls help overlay.
 *
 * Toggleable with F1 or Shift+?. Shows all available keyboard shortcuts
 * organized by category (Playback, Camera, Speed).
 */
class UIHelpPanel {
public:
    UIHelpPanel() = default;
    ~UIHelpPanel() = default;

    // Render the help panel (only when visible)
    void render();

    // Toggle visibility
    void toggle() { visible_ = !visible_; }

    // Visibility accessors
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }

private:
    bool visible_ = false;

    // Helper to render a shortcut row
    void renderShortcutRow(const char* keys, const char* description);
};
