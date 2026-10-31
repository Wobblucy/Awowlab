#pragma once
#include <vector>
#include <string_view>
#include "Encounter/EncounterSegment.h"

// Scans tokenized combat log lines to identify encounter segments
// Handles both boss encounters (ENCOUNTER_START/END) and M+ dungeons (CHALLENGE_MODE_START/END)
// M+ dungeons are treated as single segments that may contain boss encounters
class EncounterScanner {
public:
    // Scans the raw buffer and tokenizes only the encounter marker lines.
    // The segments carry byte offsets so a chosen encounter can be tokenized
    // on its own later.
    static std::vector<EncounterSegment> scanForEncountersLightweight(
        const char* data,
        size_t size
    );

    // Same segmentation over already-tokenized lines. Standalone boss pulls
    // come back as their own segments; a key comes back as one segment and the
    // bosses inside it are not listed separately. Only the tests use this.
    static std::vector<EncounterSegment> scanForEncounters(
        const std::vector<std::vector<std::string_view>>& all_lines
    );
};
