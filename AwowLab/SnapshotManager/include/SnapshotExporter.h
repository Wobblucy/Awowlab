#pragma once
#include "SnapshotFormat.h"
#include "StringTableBuilder.h"
#include "ContextCollector.h"
#include "Database/EventTypes.h"
#include "AnnotationManager/include/AnnotationTypes.h"
#include "AnnotationManager/include/AutoAnnotationTypes.h"
#include <string>
#include <vector>
#include <functional>

namespace snapshot {

struct ExportOptions {
    int32_t windowStartMs;      // Start of export window (ms relative to encounter start)
    int32_t windowEndMs;        // End of export window
    int compressionLevel = 6;   // zlib level, 0-9

    // Passed in rather than read from the CurrentEncounter singleton
    uint32_t encounterId = 0;
    std::string encounterName;
    uint16_t difficulty = 0;
    uint16_t mapId = 0;
    int32_t encounterDurationMs = 0;
};

struct ExportResult {
    bool success = false;
    std::string errorMessage;
    std::string filePath;
    size_t uncompressedSize = 0;
    size_t compressedSize = 0;
};

// phase is a label such as "Compressing"; progress runs 0.0 to 1.0 over the whole export
using ExportProgressCallback = std::function<void(const std::string& phase, float progress)>;

// Writes the events inside a time window, plus the state needed to replay it,
// to an .awsnap file. The payload is laid out as string table, context, events,
// metadata, and is zlib-compressed after a fixed header.
//
// Not thread-safe: the string table is member state built up during an export.
// Use one instance per concurrent export.
class SnapshotExporter {
public:
    SnapshotExporter() = default;

    ExportResult exportSnapshot(
        const ExtractedData& data,
        const std::vector<Annotation>& annotations,
        const std::vector<AutoAnnotationTrigger>& triggers,
        const std::vector<CachedAnnotationEvent>& cachedEvents,
        const ExportOptions& options,
        const std::string& outputPath,
        ExportProgressCallback progressCallback = nullptr
    );

    // Rough compressed size in bytes, shown before the user commits to an export
    size_t estimateSize(
        const ExtractedData& data,
        int32_t windowStartMs,
        int32_t windowEndMs
    );

private:
    std::vector<uint8_t> serializeEvents(
        const ExtractedData& data,
        int32_t windowStartMs,
        int32_t windowEndMs
    );

    std::vector<uint8_t> serializeContext(const SnapshotContext& context);

    std::vector<uint8_t> serializeMetadata(
        const ExtractedData& data,
        const ExportOptions& options
    );

    std::vector<uint8_t> compress(const std::vector<uint8_t>& data, int level);

    StringTableBuilder strings_;
};

} // namespace snapshot
