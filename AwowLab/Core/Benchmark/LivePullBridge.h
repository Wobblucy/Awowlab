#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Database/EventTypes.h"

namespace benchmark {

// Turns a finished live pull back into the same ExtractedData the imported
// path produces, by re-reading its byte range from the log.
//
// The live session cannot supply this itself: it keeps only the first cast of
// each spell (LiveFirstCast) and filters aura events down to tracked
// defensives, so cooldown timing, casts per minute and dropped globals are not
// recorded live.
//
// Re-reading one pull is cheap, and it means live and imported comparisons go
// through the same analysis code instead of two implementations that could
// disagree.

struct LivePullEvents {
    bool valid = false;

    // The bytes `data` is built from, kept because parts of ExtractedData
    // point into them rather than owning their text - ActorEvent holds
    // string_views straight at the parsed line. Everywhere else that text is
    // an mmapped log which outlives the app's use of it; here it is read into
    // memory, so it has to be carried alongside.
    //
    // Declared before `data` so it is destroyed after it.
    std::string backing;

    ExtractedData data;

    // Timestamps in the returned data are relative to the pull start, matching
    // what the imported path produces, so the extractor needs no special case.
    int32_t durationMs = 0;

    // Neither copied nor moved. `data` points into `backing`, and relocating
    // the bytes - which a short string does on move, staying inline rather
    // than handing over a heap pointer - would leave every event's guid
    // pointing at the old object. Callers keep the returned value in place
    // (NRVO), so no copy or move is needed.
    LivePullEvents() = default;
    LivePullEvents(const LivePullEvents&) = delete;
    LivePullEvents& operator=(const LivePullEvents&) = delete;
    LivePullEvents(LivePullEvents&&) = delete;
    LivePullEvents& operator=(LivePullEvents&&) = delete;
};

// One player of a pull, for asking the user which one is them.
struct RosterEntry {
    std::string guid;
    std::string name;
    uint16_t specId = 0;
};

// The players who fought in [startByteOffset, endByteOffset), by name.
//
// Deliberately not extractLivePull: this reads a handful of lines rather than
// parsing the pull. A raid pull is hundreds of thousands of events and the
// roster is twenty of them, so building the whole event set to fill a dropdown
// meant a multi-second stall and a hundred-odd megabytes for nothing.
//
// COMBATANT_INFO gives the GUIDs and specs but no names, so the scan continues
// into the combat lines only until every GUID has one.
std::vector<RosterEntry> readRoster(const std::filesystem::path& logPath,
                                    size_t startByteOffset,
                                    size_t endByteOffset);

// Read [startByteOffset, endByteOffset) from logPath and extract it into out.
//
// Partial lines at either end are dropped - a byte window can slice mid-line,
// and half a line parses into nonsense rather than failing loudly.
//
// Filled in place rather than returned, because the result cannot be moved:
// its events point into its own bytes. Check out.valid.
void extractLivePull(LivePullEvents& out,
                     const std::filesystem::path& logPath,
                     size_t startByteOffset,
                     size_t endByteOffset);

} // namespace benchmark
