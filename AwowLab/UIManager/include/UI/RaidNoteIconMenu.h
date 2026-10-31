#pragma once
#include <imgui.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Right-click handlers for spell icons in the cooldown/buff/timer panels.
// These forward to the shared spell context menu, which includes the
// "add to raid note" action - the names date back to when the menu was
// raid-note only and are kept so call sites read the same.
namespace ui {

// Check for right-click on the last ImGui item and open the spell context menu.
// Call immediately after ImGui::Image() for the spell icon.
// Returns true if the menu was opened.
// Optional: groupedTimestamps - if non-empty, "all instances" uses these instead of querying the database
// Optional: actorGuid - if provided, used for spec lookup when opening spell settings
bool checkRaidNoteContextMenu(uint32_t spellId, const std::string& spellName,
                              int32_t timestamp_ms, bool isHostile,
                              const std::vector<int32_t>& groupedTimestamps = {},
                              std::string_view actorGuid = {});

// Same, but for a specific rect (for ImDrawList::AddImage cases)
bool checkRaidNoteContextMenuRect(uint32_t spellId, const std::string& spellName,
                                  int32_t timestamp_ms, bool isHostile,
                                  ImVec2 min, ImVec2 max,
                                  const std::vector<int32_t>& groupedTimestamps = {},
                                  std::string_view actorGuid = {});

} // namespace ui
