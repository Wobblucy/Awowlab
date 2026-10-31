#include "SnapshotExporter.h"
#include "Core/StringInterner.h"
#include "AnnotationManager/include/AutoAnnotationTriggerManager.h"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <zlib.h>

namespace snapshot {

namespace {

void appendBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
    if (size == 0) return;
    size_t offset = out.size();
    out.resize(offset + size);
    std::memcpy(out.data() + offset, data, size);
}

// Sections are written as raw arrays of their packed record structs.
template <typename Record>
void appendRecords(std::vector<uint8_t>& out, const std::vector<Record>& records) {
    appendBytes(out, records.data(), records.size() * sizeof(Record));
}

} // namespace

ExportResult SnapshotExporter::exportSnapshot(
    const ExtractedData& data,
    const std::vector<Annotation>& annotations,
    const std::vector<AutoAnnotationTrigger>& triggers,
    const std::vector<CachedAnnotationEvent>& cachedEvents,
    const ExportOptions& options,
    const std::string& outputPath,
    ExportProgressCallback progressCallback
) {
    ExportResult result;
    result.filePath = outputPath;

    try {
        if (options.windowEndMs <= options.windowStartMs) {
            result.success = false;
            result.errorMessage = "Invalid window: end time must be after start time";
            return result;
        }

        if (progressCallback) progressCallback("Collecting context", 0.0f);

        ContextCollector collector;
        SnapshotContext context = collector.collect(
            data, annotations, triggers, cachedEvents,
            options.windowStartMs, options.windowEndMs,
            strings_
        );

        if (progressCallback) progressCallback("Serializing events", 0.25f);

        // Sections intern their strings as they serialize, so the string table goes last
        std::vector<uint8_t> contextData = serializeContext(context);
        std::vector<uint8_t> eventsData = serializeEvents(data, options.windowStartMs, options.windowEndMs);
        std::vector<uint8_t> metadataData = serializeMetadata(data, options);

        if (progressCallback) progressCallback("Building string table", 0.4f);

        std::vector<uint8_t> stringTableData = strings_.serialize();

        if (progressCallback) progressCallback("Assembling payload", 0.5f);

        std::vector<uint8_t> payload;
        size_t totalSize = stringTableData.size() + contextData.size() +
                           eventsData.size() + metadataData.size();
        payload.reserve(totalSize);

        // Offsets are relative to the start of the payload
        uint32_t stringTableOffset = 0;
        uint32_t contextOffset = static_cast<uint32_t>(stringTableData.size());
        uint32_t eventsOffset = contextOffset + static_cast<uint32_t>(contextData.size());
        uint32_t metadataOffset = eventsOffset + static_cast<uint32_t>(eventsData.size());

        payload.insert(payload.end(), stringTableData.begin(), stringTableData.end());
        payload.insert(payload.end(), contextData.begin(), contextData.end());
        payload.insert(payload.end(), eventsData.begin(), eventsData.end());
        payload.insert(payload.end(), metadataData.begin(), metadataData.end());

        result.uncompressedSize = payload.size();

        if (progressCallback) progressCallback("Compressing", 0.6f);

        std::vector<uint8_t> compressed = compress(payload, options.compressionLevel);
        result.compressedSize = compressed.size();

        if (progressCallback) progressCallback("Writing file", 0.9f);

        AwsnapHeader header;
        header.init();
        header.flags = FLAG_COMPRESSED;
        if (!context.annotations.empty()) header.flags |= FLAG_HAS_ANNOTATIONS;
        if (!context.triggers.empty()) header.flags |= FLAG_HAS_TRIGGERS;
        header.window_start_ms = options.windowStartMs;
        header.window_end_ms = options.windowEndMs;
        header.encounter_duration_ms = options.encounterDurationMs;
        header.encounter_id = options.encounterId;
        header.difficulty = options.difficulty;
        // The latest MAP_CHANGE before the window overrides options.mapId
        header.map_id = options.mapId;
        for (const auto& mc : data.mapChangeEvents) {
            if (mc.timestamp_ms <= options.windowStartMs) {
                header.map_id = static_cast<uint16_t>(mc.map_id);
            }
        }

        // Otherwise take the first actor event with a map id, from anywhere in the
        // log: events inside the window often lack one
        if (header.map_id == 0) {
            for (const auto& e : data.actorEvents) {
                if (e.map_id != 0) {
                    header.map_id = e.map_id;
                    break;
                }
            }
        }

        header.string_table_offset = stringTableOffset;
        header.context_section_offset = contextOffset;
        header.events_section_offset = eventsOffset;
        header.metadata_section_offset = metadataOffset;
        header.uncompressed_size = static_cast<uint32_t>(payload.size());
        header.payload_crc32 = crc32(0, payload.data(), static_cast<uInt>(payload.size()));
        // Already interned by serializeMetadata, so this returns the existing id
        header.encounter_name_id = strings_.intern(options.encounterName);

        std::ofstream file(outputPath, std::ios::binary);
        if (!file) {
            result.success = false;
            result.errorMessage = "Failed to open output file: " + outputPath;
            return result;
        }

        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());

        if (!file) {
            result.success = false;
            result.errorMessage = "Failed to write to output file";
            return result;
        }

        file.close();

        if (progressCallback) progressCallback("Complete", 1.0f);

        result.success = true;

    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = std::string("Export failed: ") + e.what();
    }

    strings_.clear();

    return result;
}

size_t SnapshotExporter::estimateSize(
    const ExtractedData& data,
    int32_t windowStartMs,
    int32_t windowEndMs
) {
    size_t positionCount = 0;
    size_t combatCount = 0;
    size_t castCount = 0;
    size_t auraCount = 0;

    for (const auto& e : data.actorEvents) {
        if (e.timestamp_ms >= windowStartMs && e.timestamp_ms < windowEndMs) {
            ++positionCount;
        }
    }

    for (const auto& e : data.combatEvents) {
        if (e.timestamp_ms >= windowStartMs && e.timestamp_ms < windowEndMs) {
            ++combatCount;
        }
    }

    for (const auto& e : data.spellCastEvents) {
        if (e.end_timestamp_ms >= windowStartMs && e.end_timestamp_ms < windowEndMs) {
            ++castCount;
        }
    }

    for (const auto& e : data.auraEvents) {
        if (e.timestamp_ms >= windowStartMs && e.timestamp_ms < windowEndMs) {
            ++auraCount;
        }
    }

    size_t uncompressed = 64;  // header
    uncompressed += 10000;     // rough string table size
    uncompressed += positionCount * sizeof(CompactPositionEvent);
    uncompressed += combatCount * sizeof(CompactCombatEvent);
    uncompressed += castCount * sizeof(CompactSpellCast);
    uncompressed += auraCount * sizeof(CompactAuraEvent);

    // Combat log data typically compresses to about 40%
    return static_cast<size_t>(uncompressed * 0.4);
}

std::vector<uint8_t> SnapshotExporter::serializeEvents(
    const ExtractedData& data,
    int32_t windowStartMs,
    int32_t windowEndMs
) {
    std::vector<uint8_t> result;

    std::unordered_map<std::string, std::string> petToOwnerMap;
    for (const auto& summon : data.summonEvents) {
        petToOwnerMap[summon.summoned_guid] = summon.summoner_guid;
    }

    std::vector<CompactPositionEvent> positions;
    std::vector<CompactCombatEvent> combat;
    std::vector<CompactSpellCast> casts;
    std::vector<CompactAuraEvent> auras;
    std::vector<CompactResourceEvent> resources;
    std::vector<CompactDeathEvent> deaths;
    std::vector<CompactResurrectEvent> resurrects;

    for (const auto& e : data.actorEvents) {
        if (e.timestamp_ms >= windowStartMs && e.timestamp_ms < windowEndMs) {
            CompactPositionEvent ce{};
            ce.guid_id = strings_.intern(e.source_guid);

            std::string guidStr(e.source_guid);
            auto ownerIt = petToOwnerMap.find(guidStr);
            if (ownerIt != petToOwnerMap.end()) {
                ce.owner_guid_id = strings_.intern(ownerIt->second);
            } else {
                ce.owner_guid_id = 0;
            }

            ce.timestamp_ms = e.timestamp_ms - windowStartMs;
            ce.x = e.x_loc;
            ce.y = e.y_loc;
            ce.facing = e.facing_radians;
            ce.actor_type = static_cast<uint8_t>(e.actor_type);
            ce.flags = packUnitFlags(
                static_cast<uint8_t>(e.flags.affiliation),
                static_cast<uint8_t>(e.flags.reaction),
                static_cast<uint8_t>(e.flags.controller),
                static_cast<uint8_t>(e.flags.unitType)
            );
            positions.push_back(ce);
        }
    }

    for (const auto& e : data.combatEvents) {
        if (e.timestamp_ms >= windowStartMs && e.timestamp_ms < windowEndMs) {
            CompactCombatEvent ce{};
            // Interner ids only mean something inside this process
            ce.source_guid_id = strings_.intern(guidInterner().lookup(e.source_guid_id));
            ce.target_guid_id = strings_.intern(guidInterner().lookup(e.target_guid_id));
            ce.timestamp_ms = e.timestamp_ms - windowStartMs;
            ce.spell_id = e.spell_id;
            ce.amount = static_cast<int32_t>(std::clamp(e.amount, (int64_t)INT32_MIN, (int64_t)INT32_MAX));
            ce.effective_amount = static_cast<int32_t>(std::clamp(e.effective_amount, (int64_t)INT32_MIN, (int64_t)INT32_MAX));
            ce.event_type = static_cast<uint8_t>(e.event_type);
            ce.flags = static_cast<uint8_t>(e.flags);
            ce.spell_school = e.spell_school;
            combat.push_back(ce);
        }
    }

    for (const auto& e : data.spellCastEvents) {
        if (e.end_timestamp_ms >= windowStartMs && e.end_timestamp_ms < windowEndMs) {
            CompactSpellCast ce{};
            ce.actor_guid_id = strings_.intern(e.actor_guid);
            ce.timestamp_ms = e.end_timestamp_ms - windowStartMs;
            ce.spell_id = e.spell_id;
            ce.haste_permille = e.haste_permille;
            ce.flags = e.cancelled ? 0x01 : 0x00;
            casts.push_back(ce);
        }
    }

    for (const auto& e : data.auraEvents) {
        if (e.timestamp_ms >= windowStartMs && e.timestamp_ms < windowEndMs) {
            CompactAuraEvent ce{};
            ce.target_guid_id = strings_.intern(e.target_guid);
            ce.spell_id = e.spell_id;
            ce.timestamp_ms = e.timestamp_ms - windowStartMs;
            ce.event_type = static_cast<uint8_t>(e.event_type);
            ce.stacks = e.stacks;
            ce.aura_type = static_cast<uint8_t>(e.aura_type);
            auras.push_back(ce);
        }
    }

    for (const auto& e : data.resourceEvents) {
        if (e.timestamp_ms >= windowStartMs && e.timestamp_ms < windowEndMs) {
            CompactResourceEvent ce{};
            ce.actor_guid_id = strings_.intern(e.actor_guid);
            ce.timestamp_ms = e.timestamp_ms - windowStartMs;
            ce.current_health = static_cast<uint32_t>(std::min(e.current_health, (uint64_t)UINT32_MAX));
            ce.max_health = static_cast<uint32_t>(std::min(e.max_health, (uint64_t)UINT32_MAX));
            ce.current_power = e.current_power;
            ce.max_power = static_cast<uint16_t>(std::min(e.max_power, 65535u));
            ce.power_type = e.power_type;
            resources.push_back(ce);
        }
    }

    for (const auto& e : data.deathEvents) {
        if (e.timestamp_ms >= windowStartMs && e.timestamp_ms < windowEndMs) {
            CompactDeathEvent ce{};
            ce.actor_guid_id = strings_.intern(e.actor_guid);
            ce.timestamp_ms = e.timestamp_ms - windowStartMs;
            ce.killing_spell_id = e.killing_spell_id;
            deaths.push_back(ce);
        }
    }

    for (const auto& e : data.resurrectEvents) {
        if (e.timestamp_ms >= windowStartMs && e.timestamp_ms < windowEndMs) {
            CompactResurrectEvent ce{};
            ce.actor_guid_id = strings_.intern(e.actor_guid);
            ce.timestamp_ms = e.timestamp_ms - windowStartMs;
            ce.spell_id = e.spell_id;
            resurrects.push_back(ce);
        }
    }

    EventsSectionHeader header{};
    header.position_count = static_cast<uint32_t>(positions.size());
    header.combat_count = static_cast<uint32_t>(combat.size());
    header.spell_cast_count = static_cast<uint32_t>(casts.size());
    header.aura_count = static_cast<uint32_t>(auras.size());
    header.resource_count = static_cast<uint32_t>(resources.size());
    header.death_count = static_cast<uint32_t>(deaths.size());
    header.resurrect_count = static_cast<uint32_t>(resurrects.size());

    size_t totalSize = sizeof(header) +
        positions.size() * sizeof(CompactPositionEvent) +
        combat.size() * sizeof(CompactCombatEvent) +
        casts.size() * sizeof(CompactSpellCast) +
        auras.size() * sizeof(CompactAuraEvent) +
        resources.size() * sizeof(CompactResourceEvent) +
        deaths.size() * sizeof(CompactDeathEvent) +
        resurrects.size() * sizeof(CompactResurrectEvent);

    result.reserve(totalSize);

    appendBytes(result, &header, sizeof(header));
    appendRecords(result, positions);
    appendRecords(result, combat);
    appendRecords(result, casts);
    appendRecords(result, auras);
    appendRecords(result, resources);
    appendRecords(result, deaths);
    appendRecords(result, resurrects);

    return result;
}

std::vector<uint8_t> SnapshotExporter::serializeContext(const SnapshotContext& context) {
    std::vector<uint8_t> result;

    std::string triggersJson;
    if (!context.triggers.empty()) {
        triggersJson = AutoAnnotationTriggerManager::serializeTriggersToJson(context.triggers, nullptr);
    }

    if (triggersJson.size() > MAX_TRIGGERS_JSON_SIZE) {
        throw std::runtime_error("Triggers JSON exceeds size limit: " +
                                 std::to_string(triggersJson.size()) + " bytes (max " +
                                 std::to_string(MAX_TRIGGERS_JSON_SIZE) + ")");
    }

    ContextSectionHeader header{};
    header.active_aura_count = static_cast<uint32_t>(context.activeAuras.size());
    header.annotation_count = static_cast<uint32_t>(context.annotations.size());
    header.trigger_json_size = static_cast<uint32_t>(triggersJson.size());
    header.position_snapshot_count = static_cast<uint32_t>(context.lastPositions.size());
    header.cached_event_count = static_cast<uint32_t>(context.cachedEvents.size());

    appendBytes(result, &header, sizeof(header));
    appendRecords(result, context.activeAuras);

    if (!context.annotations.empty()) {
        std::vector<CompactAnnotation> compactAnnotations;
        compactAnnotations.reserve(context.annotations.size());

        for (const auto& ann : context.annotations) {
            CompactAnnotation ca{};
            ca.id = ann.id;
            ca.creation_timestamp_ms = ann.creationTimestamp_ms;
            ca.duration_ms = ann.durationMs;
            ca.worldX = ann.worldX;
            ca.worldY = ann.worldY;
            ca.sourceX = ann.sourceX;
            ca.sourceY = ann.sourceY;
            ca.actorGuid_id = ann.actorGuid.empty() ? 0 : strings_.intern(ann.actorGuid);
            ca.color_packed = packColor(ann.color[0], ann.color[1], ann.color[2], ann.color[3]);
            ca.dimensions[0] = ann.dimensions[0];
            ca.dimensions[1] = ann.dimensions[1];
            ca.dimensions[2] = ann.dimensions[2];
            ca.dimensions[3] = ann.dimensions[3];

            ca.motionEndOffsetX = ann.motion.endOffsetX;
            ca.motionEndOffsetY = ann.motion.endOffsetY;
            ca.motionTravelTime_ms = ann.motion.travelTime_ms;
            ca.motionRotationSpeed = ann.motion.rotationSpeed_degPerSec;
            ca.motionProjectileDistance = ann.motion.projectileDistance;

            ca.shape = static_cast<uint8_t>(ann.shape);
            ca.anchorMode = static_cast<uint8_t>(ann.anchorMode);
            ca.durationType = static_cast<uint8_t>(ann.durationType);
            ca.visualStyle = static_cast<uint8_t>(ann.visualStyle);
            ca.motionType = static_cast<uint8_t>(ann.motion.type);
            ca.motionEndBehavior = static_cast<uint8_t>(ann.motion.endBehavior);
            ca.motionEndMode = static_cast<uint8_t>(ann.motion.endMode);
            ca.worldMarkerType = static_cast<uint8_t>(ann.worldMarkerType);
            ca.pointCount = ann.pointCount;
            ca.flags = packAnnotationFlags(ann.hasSourcePosition, ann.visible, ann.isAutoGenerated);
            ca.padding = 0;

            compactAnnotations.push_back(ca);
        }

        appendRecords(result, compactAnnotations);
    }

    if (!context.cachedEvents.empty()) {
        std::vector<CompactCachedAnnotationEvent> compactEvents;
        compactEvents.reserve(context.cachedEvents.size());

        for (const auto& ce : context.cachedEvents) {
            CompactCachedAnnotationEvent cce{};
            cce.triggerId = ce.triggerId;
            cce.timestamp_ms = ce.timestamp_ms;
            cce.eventTimestamp_ms = ce.eventTimestamp_ms;
            cce.worldX = ce.worldX;
            cce.worldY = ce.worldY;
            cce.sourceX = ce.sourceX;
            cce.sourceY = ce.sourceY;
            cce.targetGuid_id = ce.targetGuid.empty() ? 0 : strings_.intern(ce.targetGuid);
            cce.sampleDurationMs = ce.sampleDurationMs;
            cce.debuffEndTime_ms = ce.debuffEndTime_ms;
            cce.actorDeathTime_ms = ce.actorDeathTime_ms;
            cce.negatedAt_ms = ce.negatedAt_ms;
            cce.projectileEndX = ce.projectileEndX;
            cce.projectileEndY = ce.projectileEndY;
            cce.anchorFacing = ce.anchorFacing;
            cce.stackCount = ce.stackCount;
            cce.deathBehavior = static_cast<uint8_t>(ce.deathBehavior);
            cce.flags = packCachedEventFlags(
                ce.hasSourcePosition, ce.hasFacing, ce.isDuringDebuffSample,
                ce.isActorTrigger, ce.hasProjectileEnd
            );
            std::memset(cce.padding, 0, sizeof(cce.padding));

            compactEvents.push_back(cce);
        }

        appendRecords(result, compactEvents);
    }

    appendBytes(result, triggersJson.data(), triggersJson.size());
    appendRecords(result, context.lastPositions);

    return result;
}

std::vector<uint8_t> SnapshotExporter::serializeMetadata(
    const ExtractedData& data,
    const ExportOptions& options
) {
    std::vector<uint8_t> result;

    std::vector<GuidNameEntry> guidNames;
    std::vector<SpellNameEntry> spellNames;
    std::unordered_set<std::string> existingGuids;

    for (const auto& [guid, name] : data.guidToNameMap) {
        GuidNameEntry entry{};
        entry.guid_id = strings_.intern(guid);
        entry.name_id = strings_.intern(name);
        guidNames.push_back(entry);
        existingGuids.insert(guid);
    }

    // Plus in-window actors that guidToNameMap doesn't have
    for (const auto& e : data.actorEvents) {
        if (e.timestamp_ms >= options.windowStartMs && e.timestamp_ms < options.windowEndMs) {
            std::string guid(e.source_guid);
            if (!e.source_actor.empty() && existingGuids.find(guid) == existingGuids.end()) {
                GuidNameEntry entry{};
                entry.guid_id = strings_.intern(e.source_guid);
                entry.name_id = strings_.intern(e.source_actor);
                guidNames.push_back(entry);
                existingGuids.insert(guid);
            }
        }
    }

    for (const auto& [spellId, name] : data.spellNameMap) {
        SpellNameEntry entry{};
        entry.spell_id = spellId;
        entry.name_id = strings_.intern(name);
        spellNames.push_back(entry);
    }

    // Map changes up to the window end; earlier ones give the starting map
    std::vector<CompactMapChangeEvent> mapChanges;
    for (const auto& mc : data.mapChangeEvents) {
        if (mc.timestamp_ms <= options.windowEndMs) {
            CompactMapChangeEvent cmc{};
            cmc.timestamp_ms = mc.timestamp_ms - options.windowStartMs;
            cmc.map_id = mc.map_id;
            cmc.map_name_id = strings_.intern(mc.map_name);
            cmc.x1 = mc.x1;
            cmc.y1 = mc.y1;
            cmc.x2 = mc.x2;
            cmc.y2 = mc.y2;
            mapChanges.push_back(cmc);
        }
    }

    // Prepend a synthetic map change at t=0 for the starting map, resolved from
    // options.mapId, then MAP_CHANGE events, then actor events
    uint16_t resolvedMapId = options.mapId;
    if (resolvedMapId == 0 && !data.mapChangeEvents.empty()) {
        for (const auto& mc : data.mapChangeEvents) {
            if (mc.timestamp_ms <= options.windowStartMs && mc.map_id != 0) {
                resolvedMapId = static_cast<uint16_t>(mc.map_id);
            }
        }
    }
    if (resolvedMapId == 0) {
        for (const auto& e : data.actorEvents) {
            if (e.map_id != 0) {
                resolvedMapId = e.map_id;
                break;
            }
        }
    }
    if (resolvedMapId != 0) {
        CompactMapChangeEvent cmc{};
        cmc.timestamp_ms = 0;
        cmc.map_id = resolvedMapId;
        cmc.map_name_id = 0;
        cmc.x1 = cmc.y1 = cmc.x2 = cmc.y2 = 0;
        mapChanges.insert(mapChanges.begin(), cmc);
    }

    MetadataSectionHeader header{};
    header.encounter_name_id = strings_.intern(options.encounterName);

    // Header bounds come from the first map change, for older readers
    if (!data.mapChangeEvents.empty()) {
        const auto& mapChange = data.mapChangeEvents[0];
        header.map_x1 = mapChange.x1;
        header.map_y1 = mapChange.y1;
        header.map_x2 = mapChange.x2;
        header.map_y2 = mapChange.y2;
    }

    header.guid_name_count = static_cast<uint32_t>(guidNames.size());
    header.spell_name_count = static_cast<uint32_t>(spellNames.size());
    header.combatant_count = static_cast<uint32_t>(data.combatantInfoEvents.size());
    header.map_change_count = static_cast<uint32_t>(mapChanges.size());

    appendBytes(result, &header, sizeof(header));
    appendRecords(result, guidNames);
    appendRecords(result, spellNames);

    for (const auto& info : data.combatantInfoEvents) {
        CompactCombatantInfo ci{};
        ci.guid_id = strings_.intern(info.player_guid);
        ci.spec_id = info.spec_id;
        ci.talent_count = static_cast<uint16_t>(info.talents.size());

        appendBytes(result, &ci, sizeof(ci));

        // uint32: talent ids run past 65535
        for (size_t i = 0; i < ci.talent_count; ++i) {
            const uint32_t talentId = info.talents[i].talent_id;
            appendBytes(result, &talentId, sizeof(talentId));
        }
    }

    appendRecords(result, mapChanges);

    return result;
}

std::vector<uint8_t> SnapshotExporter::compress(const std::vector<uint8_t>& data, int level) {
    level = std::clamp(level, 0, 9);

    uLongf compressedSize = compressBound(static_cast<uLong>(data.size()));
    std::vector<uint8_t> compressed(compressedSize);

    int result = compress2(
        compressed.data(), &compressedSize,
        data.data(), static_cast<uLong>(data.size()),
        level
    );

    if (result != Z_OK) {
        throw std::runtime_error("zlib compression failed with code: " + std::to_string(result));
    }

    compressed.resize(compressedSize);
    return compressed;
}

} // namespace snapshot
