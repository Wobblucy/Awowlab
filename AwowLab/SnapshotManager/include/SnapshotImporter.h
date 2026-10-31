#pragma once
#include "SnapshotFormat.h"
#include "StringTableReader.h"
#include "Database/EventTypes.h"
#include "AnnotationManager/include/AnnotationTypes.h"
#include "AnnotationManager/include/AutoAnnotationTypes.h"
#include "AnnotationManager/include/TriggerEvaluators/TriggerEvaluatorCommon.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace snapshot {

struct ImportResult {
    bool success = false;
    std::string errorMessage;

    int32_t windowStartMs = 0;
    int32_t windowEndMs = 0;
    int32_t encounterDurationMs = 0;

    std::unique_ptr<ExtractedData> data;

    std::vector<Annotation> annotations;
    std::vector<CachedAnnotationEvent> cachedEvents;
    std::string triggersJson;  // empty if the snapshot has no triggers

    std::string encounterName;
    uint32_t encounterId = 0;
    uint16_t difficulty = 0;
    uint16_t mapId = 0;

    // Owns the characters every string_view in data points into, so it must
    // outlive any use of data
    std::unique_ptr<StringTableReader> stringTable;
};

// Header-level info, read without a full import
struct SnapshotInfo {
    bool valid = false;
    uint16_t versionMajor = 0;
    uint16_t versionMinor = 0;
    int32_t windowStartMs = 0;
    int32_t windowEndMs = 0;
    int32_t encounterDurationMs = 0;
    uint32_t encounterId = 0;
    std::string encounterName;
    size_t fileSize = 0;
    size_t compressedSize = 0;
    size_t uncompressedSize = 0;
    std::string errorMessage;
};

using ImportProgressCallback = std::function<void(const std::string& phase, float progress)>;

// Reads an .awsnap file back into ExtractedData for the normal visualization
// path. Positions saved from before the window are injected as events just
// before window start so interpolation has something to start from.
//
// Instances share no state, so separate instances can import concurrently.
class SnapshotImporter {
public:
    SnapshotImporter() = default;

    ImportResult importSnapshot(
        const std::string& filePath,
        ImportProgressCallback progressCallback = nullptr
    );

    // For previewing a file before importing it
    SnapshotInfo getInfo(const std::string& filePath);

    bool validate(const std::string& filePath, std::string& outError);

private:
    bool readHeader(std::ifstream& file, AwsnapHeader& header, std::string& error);

    std::vector<uint8_t> decompress(
        const std::vector<uint8_t>& compressed,
        size_t uncompressedSize
    );

    void parseEvents(
        const uint8_t* data,
        size_t size,
        const StringTableReader& strings,
        int32_t windowStartMs,
        ExtractedData& output
    );

    void parseContext(
        const uint8_t* data,
        size_t size,
        const StringTableReader& strings,
        int32_t windowStartMs,
        ExtractedData& output,
        std::vector<Annotation>& annotations,
        std::vector<CachedAnnotationEvent>& cachedEvents,
        std::string& triggersJson
    );

    // versionMinor is the file's: talent ids widened in 1.2
    void parseMetadata(
        const uint8_t* data,
        size_t size,
        const StringTableReader& strings,
        int32_t windowStartMs,
        uint16_t headerMapId,
        uint16_t versionMinor,
        ExtractedData& output,
        std::string& encounterName
    );

    // Fills actor names on events from guidToNameMap; runs after parseMetadata
    void applyNamesToEvents(ExtractedData& data);
};

} // namespace snapshot
