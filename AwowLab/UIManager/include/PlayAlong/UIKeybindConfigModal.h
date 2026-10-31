#pragma once
#include "PlayAlong/PlayAlongKeybindSettings.h"
#include <vector>
#include <string>
#include <cstdint>

// Forward declarations
class SpellIconLoader;
class SpellCastDatabase;

namespace PlayAlong {

// Information about a spell for keybind configuration
struct SpellForKeybind {
    uint32_t spellId = 0;
    std::string spellName;
    int castCount = 0;  // How many times cast (for sorting by importance)
};

// Modal dialog for configuring keybinds for PlayAlong mode
class UIKeybindConfigModal {
public:
    UIKeybindConfigModal() = default;

    // Opens the modal with the spells this player cast; keybinds are stored per spec
    void open(uint16_t specId,
              const std::string& playerGuid,
              const SpellCastDatabase* spellCastDb);

    // Returns true once the modal has closed (Done or Escape)
    bool render(SpellIconLoader* iconLoader);

    bool isOpen() const { return isOpen_; }

    // Get the configured keybind settings after modal closes
    const PlayAlongKeybindSettings& getKeybindSettings() const { return keybindSettings_; }

    // Check if user cancelled (pressed Escape or Back without completing)
    bool wasCancelled() const { return wasCancelled_; }

private:
    bool isOpen_ = false;
    bool wasCancelled_ = false;
    bool isCapturing_ = false;          // Currently capturing a keybind
    uint32_t capturingSpellId_ = 0;     // Which spell we're capturing for

    uint16_t specId_ = 0;
    std::vector<SpellForKeybind> spells_;  // Unique spells sorted by cast count
    PlayAlongKeybindSettings keybindSettings_;
    int rowIndex_ = 0;  // For alternating row colors

    // Gather unique spells from the player's cast history
    void gatherUniqueSpells(const std::string& playerGuid,
                            const SpellCastDatabase* spellCastDb);

    void resetRowIndex() { rowIndex_ = 0; }

    // Render a single spell row with icon, name, and keybind button
    void renderSpellRow(const SpellForKeybind& spell, SpellIconLoader* iconLoader);

    // Check for key press during capture mode
    void captureKeybind();

    // Render the capture overlay
    void renderCaptureOverlay();
};

} // namespace PlayAlong
