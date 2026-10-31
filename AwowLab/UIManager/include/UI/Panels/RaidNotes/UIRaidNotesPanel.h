#pragma once

#include "RaidNoteDocument.h"
#include "RaidNoteExporter.h"
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

// Forward declarations
class SpellIconLoader;

/**
 * Metadata for a saved raid note preset.
 */
struct RaidNotePreset {
    std::string name;           // Display name (e.g., "Mythic Raz P2 delayed")
    std::string filename;       // Filename without path (e.g., "mythic_raz_p2.json")
};

/**
 * UI panel for viewing and editing raid notes.
 * Allows users to manage entries, adjust timestamps, and export to clipboard.
 */
class UIRaidNotesPanel {
public:
    UIRaidNotesPanel();
    ~UIRaidNotesPanel();

    // Main render function (call each frame)
    void render(SpellIconLoader* iconLoader);

    // Visibility control
    bool isVisible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }
    bool* getVisiblePtr() { return &visible_; }

    // Get the raid note document (for context menu to add entries)
    RaidNotes::RaidNoteDocument* getDocument() { return &document_; }

    // Set current encounter context. A new encounter clears the preset
    // selection and rescans the preset folder on the next render.
    void setCurrentEncounter(uint32_t encounterId, const std::string& encounterName);

    // Callback for finding all instances of a spell (set by owner)
    using FindAllInstancesCallback = std::function<std::vector<int32_t>(uint32_t spell_id)>;
    void setFindAllInstancesCallback(FindAllInstancesCallback callback) {
        findAllInstances_ = std::move(callback);
    }

    // Get the callback (for context menu)
    const FindAllInstancesCallback& getFindAllInstancesCallback() const {
        return findAllInstances_;
    }

private:
    bool visible_ = false;

    // The document being edited
    RaidNotes::RaidNoteDocument document_;

    // Export options
    RaidNotes::ExportOptions exportOptions_;

    // Callback for querying all spell instances
    FindAllInstancesCallback findAllInstances_;

    // Current encounter context
    uint32_t currentEncounterId_ = 0;
    std::string currentEncounterName_;

    // Saved presets
    std::vector<RaidNotePreset> savedPresets_;
    int selectedPresetIndex_ = -1;
    bool presetsLoaded_ = false;

    // UI state
    char delayAfterBuffer_[32] = "0:00";   // "Delay after X" timestamp input
    char delayAmountBuffer_[32] = "0";     // "by Y seconds" input
    char manualNoteBuffer_[256] = "";
    char saveNameBuffer_[128] = "";        // Name for saving preset
    float copyFeedbackTimer_ = 0.0f;       // Shows "Copied!" feedback
    float saveFeedbackTimer_ = 0.0f;       // Shows "Saved!" feedback
    bool saveFailed_ = false;              // Last save could not be written
    std::string entryToDelete_;            // Entry scheduled for deletion
    std::string delayToDelete_;            // Phase delay scheduled for deletion

    // Render helpers
    void renderToolbar();
    void renderSaveLoadSection();
    void renderPhaseDelays();              // Render editable phase delays
    void renderGroup(RaidNotes::RaidNoteGroup& group, SpellIconLoader* iconLoader);
    void renderEntry(RaidNotes::RaidNoteEntry& entry, SpellIconLoader* iconLoader);
    void renderAddManualNoteSection();
    void renderFooter();

    // Format timestamp for display (M:SS format)
    std::string formatTimestamp(int32_t ms) const;

    // Parse timestamp from display format (returns ms, or -1 on error)
    int32_t parseTimestamp(const std::string& str) const;

    // Preset management
    bool hasSelectedPreset() const;
    void refreshPresetList();
    bool savePreset(const std::string& name);  // false if nothing was written
    void loadPreset(const std::string& filename);
    void deletePreset(const std::string& filename);
    std::string getPresetsDirectory() const;
    std::string sanitizeFilename(const std::string& name) const;

    // Crash-safe autosave: unsaved edits land in a sidecar file every
    // few seconds; on launch, a sidecar left behind by a crash gets a
    // restore/discard prompt. Explicit saves clear the sidecar.
    float autosaveTimer_ = 0.0f;
    std::string lastAutosavedJson_;
    bool autosaveChecked_ = false;         // Startup sidecar check done
    bool autosaveRecoveryAvailable_ = false;
    void updateAutosave();
    void renderRecoveryBar();
    std::string getAutosavePath() const;
};
