#include "RaidNoteExporter.h"
#include <sstream>
#include <algorithm>
#include <cmath>

namespace RaidNotes {

std::string RaidNoteExporter::exportToERT(const RaidNoteDocument& doc, const ExportOptions& opts) {
    std::ostringstream result;
    bool firstGroup = true;

    for (const auto& group : doc.getGroups()) {
        if (!group.hasEnabledEntries()) {
            continue;
        }

        if (opts.includeGroupHeaders) {
            if (!firstGroup) {
                result << "\n";
            }
            result << formatGroupHeader(group) << "\n";
        }
        firstGroup = false;

        std::vector<RaidNoteEntry> entriesToExport;
        for (const auto& entry : group.entries) {
            if (entry.enabled) {
                entriesToExport.push_back(entry);
            }
        }

        if (opts.sortByTimestamp) {
            std::sort(entriesToExport.begin(), entriesToExport.end(),
                [](const RaidNoteEntry& a, const RaidNoteEntry& b) {
                    return a.timestamp_ms < b.timestamp_ms;
                });
        }

        for (const auto& entry : entriesToExport) {
            // Template entries expand to one line per timestamp
            auto timestamps = entry.getEffectiveTimestamps();

            for (int32_t ts : timestamps) {
                int32_t adjustedTs = doc.getAdjustedTimestamp(ts);

                RaidNoteEntry tempEntry = entry;
                tempEntry.timestamp_ms = adjustedTs;

                result << formatEntry(tempEntry, opts.includeSpellNames) << "\n";
            }
        }
    }

    return result.str();
}

std::string RaidNoteExporter::formatTimestamp(int32_t ms) {
    // Negative = before the pull
    bool negative = ms < 0;
    int32_t absMs = std::abs(ms);

    int32_t totalSeconds = absMs / 1000;
    int32_t minutes = totalSeconds / 60;
    int32_t seconds = totalSeconds % 60;

    std::ostringstream oss;
    oss << "{time:";
    if (negative) {
        oss << "-";
    }
    oss << minutes << ":";
    if (seconds < 10) {
        oss << "0";
    }
    oss << seconds << "}";

    return oss.str();
}

std::string RaidNoteExporter::formatEntry(const RaidNoteEntry& entry, bool includeSpellName) {
    std::ostringstream oss;

    oss << formatTimestamp(entry.timestamp_ms);
    oss << " ";

    if (includeSpellName && !entry.spell_name.empty()) {
        oss << entry.spell_name;

        if (!entry.custom_text.empty()) {
            oss << " - ";
        }
    }

    if (!entry.custom_text.empty()) {
        oss << entry.custom_text;
    }

    return oss.str();
}

std::string RaidNoteExporter::formatGroupHeader(const RaidNoteGroup& group) {
    return group.display_name;
}

} // namespace RaidNotes
