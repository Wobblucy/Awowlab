#pragma once

#include <string>
#include <string_view>

#include "PackageCsvWriter.h"   // SpellNames

struct ExtractedData;

namespace awow::analysis {

// Writes the resource tables of the AI analysis package. Unlike
// DebugDatabaseDumper these compile into release builds, because the package is
// something users export rather than a debugging aid.
//
// Scoping is the caller's job: pass the GUID of the player the package is about
// and only their rows are written, plus boss health for context. An empty GUID
// writes every actor, which is what the tests use.
//
// Both files follow the package format rules: a header row, one event per row,
// timestamps in ms relative to the pull, and full GUIDs rather than interned
// ids, so a row means something on its own.

// Player health and every power type over time, plus boss health.
// One row per resource per event, so a spec with two resources produces two
// rows sharing a timestamp - a wide format would need a column count that
// varies by spec.
bool writeResourcesCsv(const std::string& path,
                       const ExtractedData& data,
                       std::string_view playerGuid = {});

// Resource gains: which spell granted what, and how much spilled over the cap.
bool writeEnergizeCsv(const std::string& path,
                      const ExtractedData& data,
                      std::string_view playerGuid = {},
                      const SpellNames* names = nullptr);

} // namespace awow::analysis
