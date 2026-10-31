#include "UI/RaidNoteIconMenu.h"
#include "UI/SpellContextMenu.h"

namespace ui {

bool checkRaidNoteContextMenu(uint32_t spellId, const std::string& spellName,
                              int32_t timestamp_ms, bool isHostile,
                              const std::vector<int32_t>& groupedTimestamps,
                              std::string_view actorGuid) {
    return SpellContextMenu::checkAndShow(spellId, spellName, timestamp_ms, isHostile,
                                          groupedTimestamps, actorGuid);
}

bool checkRaidNoteContextMenuRect(uint32_t spellId, const std::string& spellName,
                                  int32_t timestamp_ms, bool isHostile,
                                  ImVec2 min, ImVec2 max,
                                  const std::vector<int32_t>& groupedTimestamps,
                                  std::string_view actorGuid) {
    return SpellContextMenu::checkAndShowRect(spellId, spellName, timestamp_ms, isHostile,
                                              min, max, groupedTimestamps, actorGuid);
}

} // namespace ui
