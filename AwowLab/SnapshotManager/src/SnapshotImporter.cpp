#include "SnapshotImporter.h"
#include "Core/StringInterner.h"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <zlib.h>

namespace snapshot {

namespace {

// The size limit and section offsets from the header, checked before any of
// them is used to index the payload.
bool checkHeaderLayout(const AwsnapHeader& header, std::string& error) {
    if (header.uncompressed_size > MAX_UNCOMPRESSED_SIZE) {
        error = "Snapshot too large (max 50MB uncompressed, got " +
                std::to_string(header.uncompressed_size / (1024 * 1024)) + "MB)";
        return false;
    }

    if (header.string_table_offset > header.uncompressed_size ||
        header.context_section_offset > header.uncompressed_size ||
        header.events_section_offset > header.uncompressed_size ||
        header.metadata_section_offset > header.uncompressed_size) {
        error = "Invalid section offset in header (exceeds payload size)";
        return false;
    }

    if (header.string_table_offset > header.context_section_offset ||
        header.context_section_offset > header.events_section_offset ||
        header.events_section_offset > header.metadata_section_offset) {
        error = "Section offsets out of order";
        return false;
    }

    return true;
}

// A count read from the file can claim far more records than the bytes
// left could hold; reserve no more than could really follow.
size_t cappedCount(uint32_t count, const uint8_t* ptr, const uint8_t* end, size_t recordSize) {
    const size_t fit = (end > ptr) ? static_cast<size_t>(end - ptr) / recordSize : 0;
    return (std::min)(static_cast<size_t>(count), fit);
}

void checkBounds(const uint8_t* ptr, const uint8_t* end, size_t needed, const char* section) {
    if (ptr > end || needed > static_cast<size_t>(end - ptr)) {
        throw std::runtime_error(std::string("Buffer overflow while reading ") + section);
    }
}

// Copies the next fixed-size record out of the payload and steps past it.
template <typename Record>
Record readRecord(const uint8_t*& ptr, const uint8_t* end, const char* section) {
    checkBounds(ptr, end, sizeof(Record), section);
    Record record;
    std::memcpy(&record, ptr, sizeof(record));
    ptr += sizeof(record);
    return record;
}

void applyUnitFlags(uint8_t packed, ActorEvent& event) {
    uint8_t affil, react, ctrl, utype;
    unpackUnitFlags(packed, affil, react, ctrl, utype);
    event.flags.affiliation = static_cast<UnitAffiliation>(affil);
    event.flags.reaction = static_cast<UnitReaction>(react);
    event.flags.controller = static_cast<UnitController>(ctrl);
    event.flags.unitType = static_cast<UnitType>(utype);
}

} // namespace

ImportResult SnapshotImporter::importSnapshot(
    const std::string& filePath,
    ImportProgressCallback progressCallback
) {
    ImportResult result;

    try {
        if (progressCallback) progressCallback("Opening file", 0.0f);

        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            result.errorMessage = "Failed to open file: " + filePath;
            return result;
        }

        AwsnapHeader header;
        if (!readHeader(file, header, result.errorMessage)) {
            return result;
        }

        result.windowStartMs = header.window_start_ms;
        result.windowEndMs = header.window_end_ms;
        result.encounterDurationMs = header.encounter_duration_ms;
        result.encounterId = header.encounter_id;
        result.difficulty = header.difficulty;
        result.mapId = header.map_id;

        if (!checkHeaderLayout(header, result.errorMessage)) {
            return result;
        }

        if (progressCallback) progressCallback("Reading compressed data", 0.1f);

        std::vector<uint8_t> compressed;
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        size_t compressedSize = fileSize - sizeof(AwsnapHeader);
        file.seekg(sizeof(AwsnapHeader), std::ios::beg);

        compressed.resize(compressedSize);
        file.read(reinterpret_cast<char*>(compressed.data()), compressedSize);

        if (!file) {
            result.errorMessage = "Failed to read compressed data";
            return result;
        }

        if (progressCallback) progressCallback("Decompressing", 0.2f);

        std::vector<uint8_t> payload;
        if (header.flags & FLAG_COMPRESSED) {
            payload = decompress(compressed, header.uncompressed_size);
        } else {
            payload = std::move(compressed);
        }

        if (payload.size() != header.uncompressed_size) {
            result.errorMessage = "Decompression size mismatch";
            return result;
        }

        uint32_t computedCrc = crc32(0, payload.data(), static_cast<uInt>(payload.size()));
        if (computedCrc != header.payload_crc32) {
            result.errorMessage = "Checksum mismatch - file may be corrupted (expected " +
                                  std::to_string(header.payload_crc32) + ", got " +
                                  std::to_string(computedCrc) + ")";
            return result;
        }

        if (progressCallback) progressCallback("Parsing string table", 0.3f);

        // Kept in result because the parsed events hold string_views into it
        result.stringTable = std::make_unique<StringTableReader>();
        if (!result.stringTable->deserialize(payload.data() + header.string_table_offset,
                                              payload.size() - header.string_table_offset)) {
            result.errorMessage = "Failed to parse string table: " + result.stringTable->getError();
            return result;
        }
        const StringTableReader& strings = *result.stringTable;

        if (progressCallback) progressCallback("Parsing events", 0.5f);

        result.data = std::make_unique<ExtractedData>();

        size_t eventsSize = header.metadata_section_offset - header.events_section_offset;
        parseEvents(
            payload.data() + header.events_section_offset,
            eventsSize,
            strings,
            header.window_start_ms,
            *result.data
        );

        if (progressCallback) progressCallback("Parsing context", 0.7f);

        size_t contextSize = header.events_section_offset - header.context_section_offset;
        parseContext(
            payload.data() + header.context_section_offset,
            contextSize,
            strings,
            header.window_start_ms,
            *result.data,
            result.annotations,
            result.cachedEvents,
            result.triggersJson
        );

        if (progressCallback) progressCallback("Parsing metadata", 0.85f);

        size_t metadataSize = payload.size() - header.metadata_section_offset;
        parseMetadata(
            payload.data() + header.metadata_section_offset,
            metadataSize,
            strings,
            header.window_start_ms,
            header.map_id,
            header.version_minor,
            *result.data,
            result.encounterName
        );

        if (progressCallback) progressCallback("Applying names", 0.95f);

        applyNamesToEvents(*result.data);

        if (progressCallback) progressCallback("Complete", 1.0f);

        result.success = true;

    } catch (const std::exception& e) {
        result.errorMessage = std::string("Import failed: ") + e.what();
    }

    return result;
}

SnapshotInfo SnapshotImporter::getInfo(const std::string& filePath) {
    SnapshotInfo info;

    try {
        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            info.errorMessage = "Failed to open file";
            return info;
        }

        file.seekg(0, std::ios::end);
        info.fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        AwsnapHeader header;
        if (!readHeader(file, header, info.errorMessage)) {
            return info;
        }

        if (!checkHeaderLayout(header, info.errorMessage)) {
            return info;
        }

        info.valid = true;
        info.versionMajor = header.version_major;
        info.versionMinor = header.version_minor;
        info.windowStartMs = header.window_start_ms;
        info.windowEndMs = header.window_end_ms;
        info.encounterDurationMs = header.encounter_duration_ms;
        info.encounterId = header.encounter_id;
        info.compressedSize = info.fileSize - sizeof(AwsnapHeader);
        info.uncompressedSize = header.uncompressed_size;

        if (header.encounter_name_id != 0) {
            // The name lives in the string table, so the payload has to be decompressed
            std::vector<uint8_t> compressed(info.compressedSize);
            file.read(reinterpret_cast<char*>(compressed.data()), info.compressedSize);

            if (file) {
                std::vector<uint8_t> payload;
                if (header.flags & FLAG_COMPRESSED) {
                    payload = decompress(compressed, header.uncompressed_size);
                } else {
                    payload = std::move(compressed);
                }

                StringTableReader strings;
                if (payload.size() == header.uncompressed_size &&
                    strings.deserialize(payload.data() + header.string_table_offset,
                                         payload.size() - header.string_table_offset)) {
                    info.encounterName = strings.lookupCopy(header.encounter_name_id);
                }
            }
        }

    } catch (const std::exception& e) {
        info.valid = false;
        info.errorMessage = e.what();
    }

    return info;
}

bool SnapshotImporter::validate(const std::string& filePath, std::string& outError) {
    auto info = getInfo(filePath);
    if (!info.valid) {
        outError = info.errorMessage;
        return false;
    }
    return true;
}

bool SnapshotImporter::readHeader(std::ifstream& file, AwsnapHeader& header, std::string& error) {
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    if (!file) {
        error = "File too small for header";
        return false;
    }

    if (!header.isValid()) {
        error = "Invalid file format (bad magic bytes)";
        return false;
    }

    if (!header.isCompatible()) {
        error = "Unsupported file version: " +
                std::to_string(header.version_major) + "." +
                std::to_string(header.version_minor);
        return false;
    }

    return true;
}

std::vector<uint8_t> SnapshotImporter::decompress(
    const std::vector<uint8_t>& compressed,
    size_t uncompressedSize
) {
    std::vector<uint8_t> decompressed(uncompressedSize);
    uLongf destLen = static_cast<uLongf>(uncompressedSize);

    int result = uncompress(
        decompressed.data(), &destLen,
        compressed.data(), static_cast<uLong>(compressed.size())
    );

    if (result != Z_OK) {
        throw std::runtime_error("zlib decompression failed with code: " + std::to_string(result));
    }

    decompressed.resize(destLen);
    return decompressed;
}

void SnapshotImporter::parseEvents(
    const uint8_t* data,
    size_t size,
    const StringTableReader& strings,
    int32_t windowStartMs,
    ExtractedData& output
) {
    if (size < sizeof(EventsSectionHeader)) {
        throw std::runtime_error("Events section too small");
    }

    const uint8_t* end = data + size;
    EventsSectionHeader header;
    std::memcpy(&header, data, sizeof(header));
    const uint8_t* ptr = data + sizeof(header);

    if (header.position_count > MAX_POSITION_EVENTS) {
        throw std::runtime_error("Position event count exceeds limit: " +
                                 std::to_string(header.position_count));
    }
    if (header.combat_count > MAX_COMBAT_EVENTS) {
        throw std::runtime_error("Combat event count exceeds limit: " +
                                 std::to_string(header.combat_count));
    }
    if (header.spell_cast_count > MAX_SPELL_CAST_EVENTS) {
        throw std::runtime_error("Spell cast count exceeds limit: " +
                                 std::to_string(header.spell_cast_count));
    }
    if (header.aura_count > MAX_AURA_EVENTS) {
        throw std::runtime_error("Aura event count exceeds limit: " +
                                 std::to_string(header.aura_count));
    }
    if (header.resource_count > MAX_RESOURCE_EVENTS) {
        throw std::runtime_error("Resource event count exceeds limit: " +
                                 std::to_string(header.resource_count));
    }

    // Summon events aren't stored; they are rebuilt from the owner on each position event
    std::unordered_map<std::string, std::string> petToOwnerMap;  // pet_guid -> owner_guid
    output.actorEvents.reserve(cappedCount(header.position_count, ptr, end, sizeof(CompactPositionEvent)));
    for (uint32_t i = 0; i < header.position_count; ++i) {
        const CompactPositionEvent ce = readRecord<CompactPositionEvent>(ptr, end, "position events");

        ActorEvent e;
        e.source_guid = strings.lookup(ce.guid_id);
        e.source_actor = {};  // filled by applyNamesToEvents
        e.timestamp_ms = ce.timestamp_ms + windowStartMs;
        e.x_loc = ce.x;
        e.y_loc = ce.y;
        e.facing_radians = ce.facing;
        e.actor_type = static_cast<ActorType>(ce.actor_type);

        applyUnitFlags(ce.flags, e);

        if (ce.owner_guid_id != 0) {
            std::string petGuid(e.source_guid);
            std::string ownerGuid = strings.lookupCopy(ce.owner_guid_id);
            petToOwnerMap[petGuid] = ownerGuid;
        }

        output.actorEvents.push_back(e);
    }

    for (const auto& [petGuid, ownerGuid] : petToOwnerMap) {
        SummonEvent summon;
        summon.summoner_guid = ownerGuid;
        summon.summoned_guid = petGuid;
        summon.timestamp_ms = windowStartMs - 1;
        summon.spell_id = 0;  // not stored
        output.summonEvents.push_back(summon);
    }

    output.combatEvents.reserve(cappedCount(header.combat_count, ptr, end, sizeof(CompactCombatEvent)));
    for (uint32_t i = 0; i < header.combat_count; ++i) {
        const CompactCombatEvent ce = readRecord<CompactCombatEvent>(ptr, end, "combat events");

        CombatEvent e;
        // Snapshot string ids are not guidInterner ids, so intern the strings again
        e.source_guid_id = guidInterner().intern(strings.lookupCopy(ce.source_guid_id));
        e.target_guid_id = guidInterner().intern(strings.lookupCopy(ce.target_guid_id));
        e.timestamp_ms = ce.timestamp_ms + windowStartMs;
        e.spell_id = ce.spell_id;
        e.amount = ce.amount;
        e.effective_amount = ce.effective_amount;
        e.event_type = static_cast<CombatEventType>(ce.event_type);
        e.flags = ce.flags;
        e.spell_school = ce.spell_school;

        output.combatEvents.push_back(e);
    }

    output.spellCastEvents.reserve(cappedCount(header.spell_cast_count, ptr, end, sizeof(CompactSpellCast)));
    for (uint32_t i = 0; i < header.spell_cast_count; ++i) {
        const CompactSpellCast ce = readRecord<CompactSpellCast>(ptr, end, "spell casts");

        SpellCastEvent e;
        e.actor_guid = strings.lookupCopy(ce.actor_guid_id);
        e.end_timestamp_ms = ce.timestamp_ms + windowStartMs;
        e.start_timestamp_ms = e.end_timestamp_ms;  // cast start isn't stored
        e.spell_id = ce.spell_id;
        e.haste_permille = ce.haste_permille;
        e.cancelled = (ce.flags & 0x01) != 0;

        output.spellCastEvents.push_back(e);
    }

    output.auraEvents.reserve(cappedCount(header.aura_count, ptr, end, sizeof(CompactAuraEvent)));
    for (uint32_t i = 0; i < header.aura_count; ++i) {
        const CompactAuraEvent ce = readRecord<CompactAuraEvent>(ptr, end, "aura events");

        AuraEvent e;
        e.target_guid = strings.lookupCopy(ce.target_guid_id);
        e.spell_id = ce.spell_id;
        e.timestamp_ms = ce.timestamp_ms + windowStartMs;
        e.event_type = static_cast<AuraEventType>(ce.event_type);
        e.stacks = ce.stacks;
        e.aura_type = static_cast<AuraType>(ce.aura_type);

        output.auraEvents.push_back(e);
    }

    output.resourceEvents.reserve(cappedCount(header.resource_count, ptr, end, sizeof(CompactResourceEvent)));
    for (uint32_t i = 0; i < header.resource_count; ++i) {
        const CompactResourceEvent ce = readRecord<CompactResourceEvent>(ptr, end, "resource events");

        ResourceEvent e;
        e.actor_guid = strings.lookupCopy(ce.actor_guid_id);
        e.timestamp_ms = ce.timestamp_ms + windowStartMs;
        e.current_health = ce.current_health;
        e.max_health = ce.max_health;
        e.current_power = ce.current_power;
        e.max_power = ce.max_power;
        e.power_type = ce.power_type;

        output.resourceEvents.push_back(e);
    }

    output.deathEvents.reserve(cappedCount(header.death_count, ptr, end, sizeof(CompactDeathEvent)));
    for (uint32_t i = 0; i < header.death_count; ++i) {
        const CompactDeathEvent ce = readRecord<CompactDeathEvent>(ptr, end, "death events");

        DeathEvent e;
        e.actor_guid = strings.lookupCopy(ce.actor_guid_id);
        e.timestamp_ms = ce.timestamp_ms + windowStartMs;
        e.killing_spell_id = ce.killing_spell_id;

        output.deathEvents.push_back(e);
    }

    output.resurrectEvents.reserve(cappedCount(header.resurrect_count, ptr, end, sizeof(CompactResurrectEvent)));
    for (uint32_t i = 0; i < header.resurrect_count; ++i) {
        const CompactResurrectEvent ce = readRecord<CompactResurrectEvent>(ptr, end, "resurrect events");

        ResurrectEvent e;
        e.actor_guid = strings.lookupCopy(ce.actor_guid_id);
        e.timestamp_ms = ce.timestamp_ms + windowStartMs;
        e.spell_id = ce.spell_id;

        output.resurrectEvents.push_back(e);
    }
}

void SnapshotImporter::parseContext(
    const uint8_t* data,
    size_t size,
    const StringTableReader& strings,
    int32_t windowStartMs,
    ExtractedData& output,
    std::vector<Annotation>& annotations,
    std::vector<CachedAnnotationEvent>& cachedEvents,
    std::string& triggersJson
) {
    if (size < sizeof(ContextSectionHeader)) {
        throw std::runtime_error("Context section too small");
    }

    const uint8_t* end = data + size;
    ContextSectionHeader header;
    std::memcpy(&header, data, sizeof(header));
    const uint8_t* ptr = data + sizeof(header);

    if (header.annotation_count > MAX_ANNOTATION_COUNT) {
        throw std::runtime_error("Annotation count exceeds limit: " +
                                 std::to_string(header.annotation_count));
    }

    // Auras active at window start become APPLIED events at windowStartMs - 1
    for (uint32_t i = 0; i < header.active_aura_count; ++i) {
        const AuraSnapshot as = readRecord<AuraSnapshot>(ptr, end, "active auras");

        AuraEvent e;
        e.target_guid = strings.lookupCopy(as.target_guid_id);
        e.source_guid = strings.lookupCopy(as.source_guid_id);
        e.spell_id = as.spell_id;
        e.timestamp_ms = windowStartMs - 1;
        e.event_type = AuraEventType::Applied;
        e.stacks = as.stacks;
        e.aura_type = static_cast<AuraType>(as.aura_type);

        output.auraEvents.push_back(e);
    }

    annotations.reserve(cappedCount(header.annotation_count, ptr, end, sizeof(CompactAnnotation)));
    for (uint32_t i = 0; i < header.annotation_count; ++i) {
        const CompactAnnotation ca = readRecord<CompactAnnotation>(ptr, end, "annotations");

        Annotation ann;
        ann.id = ca.id;
        ann.creationTimestamp_ms = ca.creation_timestamp_ms;
        ann.durationMs = ca.duration_ms;
        ann.worldX = ca.worldX;
        ann.worldY = ca.worldY;
        ann.sourceX = ca.sourceX;
        ann.sourceY = ca.sourceY;
        ann.actorGuid = (ca.actorGuid_id != 0) ? strings.lookupCopy(ca.actorGuid_id) : "";

        unpackColor(ca.color_packed, ann.color[0], ann.color[1], ann.color[2], ann.color[3]);

        ann.dimensions[0] = ca.dimensions[0];
        ann.dimensions[1] = ca.dimensions[1];
        ann.dimensions[2] = ca.dimensions[2];
        ann.dimensions[3] = ca.dimensions[3];

        ann.motion.endOffsetX = ca.motionEndOffsetX;
        ann.motion.endOffsetY = ca.motionEndOffsetY;
        ann.motion.travelTime_ms = ca.motionTravelTime_ms;
        ann.motion.rotationSpeed_degPerSec = ca.motionRotationSpeed;
        ann.motion.projectileDistance = ca.motionProjectileDistance;

        ann.shape = static_cast<AnnotationShape>(ca.shape);
        ann.anchorMode = static_cast<AnnotationAnchor>(ca.anchorMode);
        ann.durationType = static_cast<AnnotationDuration>(ca.durationType);
        ann.visualStyle = static_cast<AnnotationVisualStyle>(ca.visualStyle);
        ann.motion.type = static_cast<AnnotationMotionType>(ca.motionType);
        ann.motion.endBehavior = static_cast<ProjectileEndBehavior>(ca.motionEndBehavior);
        ann.motion.endMode = static_cast<ProjectileEndMode>(ca.motionEndMode);
        ann.worldMarkerType = static_cast<WorldMarkerType>(ca.worldMarkerType);
        ann.pointCount = ca.pointCount;

        unpackAnnotationFlags(ca.flags, ann.hasSourcePosition, ann.visible, ann.isAutoGenerated);

        ann.deleted = false;
        ann.isPreview = false;
        ann.currentX = ann.worldX;
        ann.currentY = ann.worldY;
        ann.positionValid = true;

        annotations.push_back(ann);
    }

    cachedEvents.reserve(cappedCount(header.cached_event_count, ptr, end, sizeof(CompactCachedAnnotationEvent)));
    for (uint32_t i = 0; i < header.cached_event_count; ++i) {
        const CompactCachedAnnotationEvent cce = readRecord<CompactCachedAnnotationEvent>(ptr, end, "cached events");

        CachedAnnotationEvent ce;
        ce.triggerId = cce.triggerId;
        ce.timestamp_ms = cce.timestamp_ms;
        ce.eventTimestamp_ms = cce.eventTimestamp_ms;
        ce.worldX = cce.worldX;
        ce.worldY = cce.worldY;
        ce.sourceX = cce.sourceX;
        ce.sourceY = cce.sourceY;
        ce.targetGuid = (cce.targetGuid_id != 0) ? strings.lookupCopy(cce.targetGuid_id) : "";
        ce.sampleDurationMs = cce.sampleDurationMs;
        ce.debuffEndTime_ms = cce.debuffEndTime_ms;
        ce.actorDeathTime_ms = cce.actorDeathTime_ms;
        ce.negatedAt_ms = cce.negatedAt_ms;
        ce.projectileEndX = cce.projectileEndX;
        ce.projectileEndY = cce.projectileEndY;
        ce.anchorFacing = cce.anchorFacing;
        ce.stackCount = cce.stackCount;
        ce.deathBehavior = static_cast<ActorDeathBehavior>(cce.deathBehavior);

        unpackCachedEventFlags(cce.flags,
            ce.hasSourcePosition, ce.hasFacing, ce.isDuringDebuffSample,
            ce.isActorTrigger, ce.hasProjectileEnd
        );

        cachedEvents.push_back(ce);
    }

    if (header.trigger_json_size > 0) {
        if (header.trigger_json_size > MAX_TRIGGERS_JSON_SIZE) {
            throw std::runtime_error("Triggers JSON size exceeds limit: " +
                                     std::to_string(header.trigger_json_size));
        }
        checkBounds(ptr, end, header.trigger_json_size, "triggers JSON");
        triggersJson.assign(reinterpret_cast<const char*>(ptr), header.trigger_json_size);
        ptr += header.trigger_json_size;
    }

    // Pre-window positions become position events at windowStartMs - 1
    for (uint32_t i = 0; i < header.position_snapshot_count; ++i) {
        const PositionSnapshot ps = readRecord<PositionSnapshot>(ptr, end, "position snapshots");

        ActorEvent e;
        e.source_guid = strings.lookup(ps.guid_id);
        e.source_actor = strings.lookup(ps.name_id);
        e.timestamp_ms = windowStartMs - 1;
        e.x_loc = ps.x;
        e.y_loc = ps.y;
        e.facing_radians = ps.facing;
        e.actor_type = static_cast<ActorType>(ps.actor_type);

        applyUnitFlags(ps.flags, e);

        output.actorEvents.push_back(e);
    }
}

void SnapshotImporter::parseMetadata(
    const uint8_t* data,
    size_t size,
    const StringTableReader& strings,
    int32_t windowStartMs,
    uint16_t headerMapId,
    uint16_t versionMinor,
    ExtractedData& output,
    std::string& encounterName
) {
    if (size < sizeof(MetadataSectionHeader)) {
        throw std::runtime_error("Metadata section too small");
    }

    const uint8_t* end = data + size;
    MetadataSectionHeader header;
    std::memcpy(&header, data, sizeof(header));
    const uint8_t* ptr = data + sizeof(header);

    encounterName = strings.lookupCopy(header.encounter_name_id);

    for (uint32_t i = 0; i < header.guid_name_count; ++i) {
        const GuidNameEntry entry = readRecord<GuidNameEntry>(ptr, end, "GUID->Name mappings");

        std::string guid = strings.lookupCopy(entry.guid_id);
        std::string name = strings.lookupCopy(entry.name_id);
        output.guidToNameMap[guid] = name;
    }

    for (uint32_t i = 0; i < header.spell_name_count; ++i) {
        const SpellNameEntry entry = readRecord<SpellNameEntry>(ptr, end, "SpellID->Name mappings");

        output.spellNameMap[entry.spell_id] = strings.lookupCopy(entry.name_id);
    }

    for (uint32_t i = 0; i < header.combatant_count; ++i) {
        const CompactCombatantInfo ci = readRecord<CompactCombatantInfo>(ptr, end, "combatant info");

        CombatantInfoEvent info;
        info.player_guid = strings.lookupCopy(ci.guid_id);
        info.spec_id = ci.spec_id;

        // Files before 1.2 stored talent ids as uint16, which cut off ids above 65535
        const size_t talentSize = (versionMinor >= 2) ? sizeof(uint32_t) : sizeof(uint16_t);
        checkBounds(ptr, end, ci.talent_count * talentSize, "talent IDs");
        for (uint16_t t = 0; t < ci.talent_count; ++t) {
            parser::TalentEntry talent;
            if (talentSize == sizeof(uint32_t)) {
                uint32_t talentId;
                std::memcpy(&talentId, ptr, sizeof(talentId));
                talent.talent_id = talentId;
            } else {
                uint16_t talentId;
                std::memcpy(&talentId, ptr, sizeof(talentId));
                talent.talent_id = talentId;
            }
            ptr += talentSize;
            info.talents.push_back(talent);
        }

        output.combatantInfoEvents.push_back(info);
    }

    output.mapChangeEvents.reserve(cappedCount(header.map_change_count, ptr, end, sizeof(CompactMapChangeEvent)));
    for (uint32_t i = 0; i < header.map_change_count; ++i) {
        const CompactMapChangeEvent cmc = readRecord<CompactMapChangeEvent>(ptr, end, "map change events");

        MapChangeEvent mc;
        mc.timestamp_ms = cmc.timestamp_ms + windowStartMs;
        mc.map_id = cmc.map_id;
        mc.map_name = strings.lookupCopy(cmc.map_name_id);
        mc.x1 = cmc.x1;
        mc.y1 = cmc.y1;
        mc.x2 = cmc.x2;
        mc.y2 = cmc.y2;
        output.mapChangeEvents.push_back(mc);
    }

    // No map changes stored: build one from the header's map id or bounds
    if (output.mapChangeEvents.empty() && (headerMapId != 0 || header.map_x1 != 0 || header.map_y1 != 0)) {
        MapChangeEvent mc;
        mc.timestamp_ms = 0;
        mc.map_id = headerMapId;
        mc.x1 = header.map_x1;
        mc.y1 = header.map_y1;
        mc.x2 = header.map_x2;
        mc.y2 = header.map_y2;
        output.mapChangeEvents.push_back(mc);
    }
}

void SnapshotImporter::applyNamesToEvents(ExtractedData& data) {
    for (auto& e : data.actorEvents) {
        auto it = data.guidToNameMap.find(std::string(e.source_guid));
        if (it != data.guidToNameMap.end()) {
            e.source_actor = it->second;
        }
    }

    for (auto& e : data.deathEvents) {
        auto it = data.guidToNameMap.find(e.actor_guid);
        if (it != data.guidToNameMap.end()) {
            e.actor_name = it->second;
        }
    }

    for (auto& e : data.resurrectEvents) {
        auto it = data.guidToNameMap.find(e.actor_guid);
        if (it != data.guidToNameMap.end()) {
            e.actor_name = it->second;
        }
    }
}

} // namespace snapshot
