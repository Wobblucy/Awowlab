#pragma once

#include "Haste/HasteBuffTable.h"

#include <string>
#include <string_view>

namespace awow {

// Fills haste::HasteBuffTable from data/haste_buffs_{version}.json, which
// tools/db2_extractor/build_haste_buffs.py generates from the game's spell data.
//
// The file looks like
//   { "version": "12.1", "buffs": { "2825": {"haste": 300, "stacks": 1, ...}, ... } }
// with haste in permille per stack. An entry with a field missing or of the
// wrong type is skipped rather than failing the whole file.

/**
 * Replace the table's contents with the buffs in a JSON document.
 * @return Number of buffs read; 0 when the text is not a buff file, in which
 *         case the table is left as it was.
 */
size_t parseHasteBuffs(std::string_view json, haste::HasteBuffTable& table);

/**
 * Load a haste buff file into the shared table the haste tracker reads.
 * Call once at startup, before any log is parsed.
 * @return True when at least one buff was loaded.
 */
bool loadHasteBuffs(const std::string& path);

} // namespace awow
