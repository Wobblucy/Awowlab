#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace PlayAlong {

// Represents a single keybind with optional modifiers
struct Keybind {
    int imguiKey = 0;           // ImGuiKey_* value (0 = unbound)
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    bool isUnbound() const { return imguiKey == 0; }
    std::string toString() const;  // e.g., "Ctrl+Shift+Q"

    bool operator==(const Keybind& other) const {
        return imguiKey == other.imguiKey &&
               ctrl == other.ctrl &&
               shift == other.shift &&
               alt == other.alt;
    }
};

// Manages keybinds for a single spec
class PlayAlongKeybindSettings {
public:
    PlayAlongKeybindSettings() = default;

    // Load keybinds for a spec (creates empty if file doesn't exist)
    bool load(uint16_t specId);

    // Save current keybinds to JSON
    bool save() const;

    // Get/set keybind for a spell
    const Keybind& getKeybind(uint32_t spellId) const;
    void setKeybind(uint32_t spellId, const Keybind& keybind);
    void clearKeybind(uint32_t spellId);

    // Check if a spell has a keybind
    bool hasKeybind(uint32_t spellId) const;

    // Get all keybinds (for iteration)
    const std::unordered_map<uint32_t, Keybind>& getAllKeybinds() const { return keybinds_; }

    // Ignore/unignore spells (excluded from GCD history and keybind assignment)
    void ignoreSpell(uint32_t spellId);
    void unignoreSpell(uint32_t spellId);
    bool isSpellIgnored(uint32_t spellId) const;
    const std::unordered_set<uint32_t>& getIgnoredSpells() const { return ignoredSpellIds_; }

    // Used spells tracking (for disabling keybinds from previous encounters)
    void setUsedSpells(const std::unordered_set<uint32_t>& usedSpellIds);
    bool isKeybindActive(uint32_t spellId) const;
    void clearUsedSpells() { usedSpellIds_.clear(); }

    // Get spec info
    uint16_t getSpecId() const { return specId_; }
    const std::string& getSpecName() const { return specName_; }

    // "Balance Druid"; empty for a spec id it doesn't know
    static std::string specDisplayName(uint16_t specId);

    // Convert ImGuiKey to human-readable name
    static std::string imguiKeyToName(int imguiKey);

    // Convert human-readable name back to ImGuiKey
    static int nameToImguiKey(const std::string& name);

    // True if the value is a real ImGuiKey (named key range)
    static bool isValidImguiKey(int imguiKey);

private:
    uint16_t specId_ = 0;
    std::string specName_;  // e.g., "balance", "frost-mage"
    std::unordered_map<uint32_t, Keybind> keybinds_;
    std::unordered_set<uint32_t> ignoredSpellIds_;  // Spells excluded from GCD history
    std::unordered_set<uint32_t> usedSpellIds_;     // Spells used in current encounter

    // Static empty keybind for returning references
    static const Keybind emptyKeybind_;

    std::string getFilePath() const;
    static std::string specIdToName(uint16_t specId);
    static std::string getSettingsDirectory();
};

} // namespace PlayAlong
