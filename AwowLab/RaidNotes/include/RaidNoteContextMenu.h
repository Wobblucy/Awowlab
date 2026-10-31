#pragma once

#include "RaidNoteDocument.h"
#include <string>
#include <cstdint>
#include <functional>
#include <imgui.h>

class SpellCastDatabase;
class CombatDatabase;

namespace RaidNotes {

// What was right-clicked
struct SpellContext {
    uint32_t spell_id = 0;
    std::string spell_name;
    int32_t timestamp_ms = -1;      // -1 if not applicable
    std::string source_guid;        // decides which group the entry goes in
    bool is_hostile_source = false;

    // When set, "all instances" uses these instead of querying the database
    std::vector<int32_t> grouped_timestamps;
};

// Returns every cast time of the spell, in ms
using FindAllInstancesCallback = std::function<std::vector<int32_t>(uint32_t spell_id)>;

// Right-click menu for adding a spell to the raid note. All state is static
// because only one modal can be open at a time.
class RaidNoteContextMenu {
public:
    // Call right after drawing a spell icon. Returns true if the menu opened.
    static bool checkAndShow(const SpellContext& context);

    // Same, for icons drawn with ImDrawList where there is no last item
    static bool checkAndShowRect(const SpellContext& context, ImVec2 min, ImVec2 max);

    // Call once per frame from UIWindowManager
    static void renderModal(RaidNoteDocument* document,
                           FindAllInstancesCallback findAllInstances);

    static bool isModalOpen() { return s_modalOpen; }

    // Closes without adding anything
    static void closeModal();

    // Used by SpellContextMenu to hand off to this modal
    static void openWithContext(const SpellContext& context);

private:
    static bool s_modalOpen;
    static bool s_modalJustOpened;
    static SpellContext s_pendingContext;
    static int s_selectedOption;        // 0 = just this, 1 = all instances
    static char s_customTextBuffer[256];
    static std::vector<int32_t> s_allInstanceTimestamps;
    static bool s_instancesQueried;
};

} // namespace RaidNotes
