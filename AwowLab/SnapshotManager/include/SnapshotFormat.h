#pragma once
#include <cstdint>
#include <array>
#include <cstring>

namespace snapshot {

constexpr char AWSNAP_MAGIC[8] = {'A', 'W', 'S', 'N', 'A', 'P', '\0', '\0'};
constexpr uint16_t VERSION_MAJOR = 1;
constexpr uint16_t VERSION_MINOR = 2;  // 1.2: talent ids stored as uint32

// Limits applied when reading, so a crafted file can't exhaust memory
constexpr size_t MAX_UNCOMPRESSED_SIZE = 50 * 1024 * 1024;   // 50 MB (typical encounter ~3MB)
constexpr uint32_t MAX_POSITION_EVENTS = 1'000'000;          // ~20MB of position data
constexpr uint32_t MAX_COMBAT_EVENTS = 1'000'000;            // ~28MB of combat data
constexpr uint32_t MAX_SPELL_CAST_EVENTS = 500'000;
constexpr uint32_t MAX_AURA_EVENTS = 1'000'000;
constexpr uint32_t MAX_RESOURCE_EVENTS = 1'000'000;
constexpr uint32_t MAX_STRING_TABLE_SIZE = 10 * 1024 * 1024; // 10 MB
constexpr uint32_t MAX_ANNOTATION_COUNT = 10'000;
constexpr uint32_t MAX_TRIGGERS_JSON_SIZE = 5 * 1024 * 1024; // 5 MB

enum HeaderFlags : uint32_t {
    FLAG_COMPRESSED = 1 << 0,
    FLAG_HAS_ANNOTATIONS = 1 << 1,
    FLAG_HAS_TRIGGERS = 1 << 2
};

// File header (64 bytes, fixed size)
// Layout:
//   [0-7]   magic: "AWSNAP\0\0"
//   [8-9]   version_major
//   [10-11] version_minor
//   [12-15] flags
//   [16-19] window_start_ms
//   [20-23] window_end_ms
//   [24-27] encounter_duration_ms
//   [28-31] encounter_id
//   [32-33] difficulty
//   [34-35] map_id
//   [36-39] string_table_offset
//   [40-43] context_section_offset
//   [44-47] events_section_offset
//   [48-51] metadata_section_offset
//   [52-55] uncompressed_size
//   [56-59] encounter_name_id (string table index for quick info lookup)
//   [60-63] payload_crc32
#pragma pack(push, 1)
struct AwsnapHeader {
    char magic[8];              // "AWSNAP\0\0"
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t flags;
    int32_t window_start_ms;
    int32_t window_end_ms;
    int32_t encounter_duration_ms;
    uint32_t encounter_id;
    uint16_t difficulty;
    uint16_t map_id;
    uint32_t string_table_offset;
    uint32_t context_section_offset;
    uint32_t events_section_offset;
    uint32_t metadata_section_offset;
    uint32_t uncompressed_size;
    uint32_t encounter_name_id;     // String table index for encounter name
    uint32_t payload_crc32;         // CRC32 of uncompressed payload for integrity check

    void init() {
        std::memcpy(magic, AWSNAP_MAGIC, 8);
        version_major = VERSION_MAJOR;
        version_minor = VERSION_MINOR;
        flags = 0;
        window_start_ms = 0;
        window_end_ms = 0;
        encounter_duration_ms = 0;
        encounter_id = 0;
        difficulty = 0;
        map_id = 0;
        string_table_offset = 0;
        context_section_offset = 0;
        events_section_offset = 0;
        metadata_section_offset = 0;
        uncompressed_size = 0;
        encounter_name_id = 0;
        payload_crc32 = 0;
    }

    bool isValid() const {
        return std::memcmp(magic, AWSNAP_MAGIC, 8) == 0;
    }

    // Same major, minor no newer than ours
    bool isCompatible() const {
        return version_major == VERSION_MAJOR && version_minor <= VERSION_MINOR;
    }
};
#pragma pack(pop)
static_assert(sizeof(AwsnapHeader) == 64, "Header must be 64 bytes");

// Compact position event (24 bytes)
// Stores actor position at a specific timestamp
#pragma pack(push, 1)
struct CompactPositionEvent {
    uint32_t guid_id;           // String table index for GUID
    uint32_t owner_guid_id;     // String table index for owner GUID (pets/guardians), 0 if none
    int32_t timestamp_ms;       // Relative to window_start (can be negative for context)
    float x, y;                 // World coordinates
    uint16_t facing;            // radians * 10000
    uint8_t actor_type;         // ActorType enum value
    uint8_t flags;              // Packed: affiliation(2) | reaction(2) | controller(1) | unit_type(3)
};
#pragma pack(pop)
static_assert(sizeof(CompactPositionEvent) == 24, "CompactPositionEvent must be 24 bytes");

// Compact combat event (28 bytes)
// Stores damage or healing event
#pragma pack(push, 1)
struct CompactCombatEvent {
    uint32_t source_guid_id;    // String table index
    uint32_t target_guid_id;    // String table index
    int32_t timestamp_ms;       // Relative to window_start
    uint32_t spell_id;          // Spell ID (0 for melee)
    int32_t amount;             // Raw damage/heal amount (32-bit sufficient for 15s windows)
    int32_t effective_amount;   // Amount after overkill/overheal
    uint8_t event_type;         // CombatEventType enum
    uint8_t flags;              // Crit, periodic, etc.
    uint8_t spell_school;       // SpellSchool bitmask
    uint8_t padding;
};
#pragma pack(pop)
static_assert(sizeof(CompactCombatEvent) == 28, "CompactCombatEvent must be 28 bytes");

// Compact spell cast (16 bytes)
// Stores spell cast completion (for cooldown tracking)
#pragma pack(push, 1)
struct CompactSpellCast {
    uint32_t actor_guid_id;     // String table index
    int32_t timestamp_ms;       // Cast completion time
    uint32_t spell_id;
    uint16_t haste_permille;    // Haste in permille (300 = 30.0%)
    uint8_t flags;              // Bit 0: cancelled, Bit 1: instant
    uint8_t padding;
};
#pragma pack(pop)
static_assert(sizeof(CompactSpellCast) == 16, "CompactSpellCast must be 16 bytes");

// Compact aura event (16 bytes)
// Stores buff/debuff application/removal
#pragma pack(push, 1)
struct CompactAuraEvent {
    uint32_t target_guid_id;    // String table index
    uint32_t spell_id;
    int32_t timestamp_ms;
    uint8_t event_type;         // AuraEventType: Applied, Removed, Refresh, AppliedDose, RemovedDose
    uint8_t stacks;             // Stack count
    uint8_t aura_type;          // AuraType: BUFF or DEBUFF
    uint8_t padding;
};
#pragma pack(pop)
static_assert(sizeof(CompactAuraEvent) == 16, "CompactAuraEvent must be 16 bytes");

// Compact resource event (24 bytes)
// Stores health/power changes
#pragma pack(push, 1)
struct CompactResourceEvent {
    uint32_t actor_guid_id;     // String table index
    int32_t timestamp_ms;
    uint32_t current_health;    // 32-bit for snapshots (full 64-bit in main data)
    uint32_t max_health;
    uint32_t current_power;
    uint16_t max_power;         // 16-bit power is usually enough
    int8_t power_type;          // PowerType enum
    uint8_t padding;
};
#pragma pack(pop)
static_assert(sizeof(CompactResourceEvent) == 24, "CompactResourceEvent must be 24 bytes");

// Compact death event (12 bytes)
#pragma pack(push, 1)
struct CompactDeathEvent {
    uint32_t actor_guid_id;     // String table index
    int32_t timestamp_ms;
    uint32_t killing_spell_id;  // 0 if not from SPELL_INSTAKILL
};
#pragma pack(pop)
static_assert(sizeof(CompactDeathEvent) == 12, "CompactDeathEvent must be 12 bytes");

// Compact resurrect event (12 bytes)
#pragma pack(push, 1)
struct CompactResurrectEvent {
    uint32_t actor_guid_id;     // String table index
    int32_t timestamp_ms;
    uint32_t spell_id;          // Resurrect spell ID
};
#pragma pack(pop)
static_assert(sizeof(CompactResurrectEvent) == 12, "CompactResurrectEvent must be 12 bytes");

// Compact map change event (28 bytes)
// Stores map boundaries when entering a new area
#pragma pack(push, 1)
struct CompactMapChangeEvent {
    int32_t timestamp_ms;       // Relative to window_start
    uint32_t map_id;            // uiMapID
    uint32_t map_name_id;       // String table index for map name
    float x1, y1;               // Top-left world bounds
    float x2, y2;               // Bottom-right world bounds
};
#pragma pack(pop)
static_assert(sizeof(CompactMapChangeEvent) == 28, "CompactMapChangeEvent must be 28 bytes");

// Active aura at window start (24 bytes)
// Represents a buff/debuff that was active when the snapshot window begins
#pragma pack(push, 1)
struct AuraSnapshot {
    uint32_t target_guid_id;    // String table index
    uint32_t source_guid_id;    // String table index
    uint32_t spell_id;
    int32_t applied_at_ms;      // Original application time (may be before window)
    int32_t remaining_ms;       // Remaining duration at window_start (-1 = unknown)
    uint8_t stacks;
    uint8_t aura_type;          // AuraType: BUFF or DEBUFF
    uint16_t padding;
};
#pragma pack(pop)
static_assert(sizeof(AuraSnapshot) == 24, "AuraSnapshot must be 24 bytes");

// Actor position at window start (20 bytes)
// Last known position for actors before the window starts
#pragma pack(push, 1)
struct PositionSnapshot {
    uint32_t guid_id;           // String table index for GUID
    uint32_t name_id;           // String table index for actor name
    float x, y;
    uint16_t facing;            // radians * 10000
    uint8_t actor_type;         // ActorType enum
    uint8_t flags;              // Packed unit flags
};
#pragma pack(pop)
static_assert(sizeof(PositionSnapshot) == 20, "PositionSnapshot must be 20 bytes");

// Compact annotation for serialization (88 bytes), including motion settings.
//
// The Raindrops motion fields (dropsPerSecond, dropRadius, dropLifetime_ms) are
// not stored, so raindrop annotations import with default motion parameters.
#pragma pack(push, 1)
struct CompactAnnotation {
    // Identity & timing
    uint64_t id;                        // 8 bytes - Unique ID
    int32_t creation_timestamp_ms;      // 4 bytes - When annotation was created
    int32_t duration_ms;                // 4 bytes - Duration (if timed)

    // Position
    float worldX, worldY;               // 8 bytes - Target/anchor position
    float sourceX, sourceY;             // 8 bytes - Source position (for lines)
    uint32_t actorGuid_id;              // 4 bytes - String table ID (if actor-anchored)

    // Visual
    uint32_t color_packed;              // 4 bytes - RGBA packed (r | g<<8 | b<<16 | a<<24)
    float dimensions[4];                // 16 bytes - Shape-specific dimensions

    // Motion config (for animation replay)
    float motionEndOffsetX;             // 4 bytes
    float motionEndOffsetY;             // 4 bytes
    int32_t motionTravelTime_ms;        // 4 bytes
    float motionRotationSpeed;          // 4 bytes - degrees per second
    float motionProjectileDistance;     // 4 bytes

    // Enums & flags (packed into bytes)
    uint8_t shape;                      // AnnotationShape enum
    uint8_t anchorMode;                 // AnnotationAnchor enum
    uint8_t durationType;               // AnnotationDuration enum
    uint8_t visualStyle;                // AnnotationVisualStyle enum
    uint8_t motionType;                 // AnnotationMotionType enum
    uint8_t motionEndBehavior;          // ProjectileEndBehavior enum
    uint8_t motionEndMode;              // ProjectileEndMode enum
    uint8_t worldMarkerType;            // WorldMarkerType enum
    uint8_t pointCount;                 // Star ray count (3-12)
    uint8_t flags;                      // Bit 0: hasSourcePosition, Bit 1: visible, Bit 2: isAutoGenerated
    uint16_t padding;
};
#pragma pack(pop)
static_assert(sizeof(CompactAnnotation) == 88, "CompactAnnotation must be 88 bytes");

inline uint8_t packAnnotationFlags(bool hasSourcePosition, bool visible, bool isAutoGenerated) {
    return (hasSourcePosition ? 0x01 : 0) |
           (visible ? 0x02 : 0) |
           (isAutoGenerated ? 0x04 : 0);
}

inline void unpackAnnotationFlags(uint8_t flags, bool& hasSourcePosition, bool& visible, bool& isAutoGenerated) {
    hasSourcePosition = (flags & 0x01) != 0;
    visible = (flags & 0x02) != 0;
    isAutoGenerated = (flags & 0x04) != 0;
}

// Compact cached annotation event for serialization (68 bytes)
// Pre-computed trigger results with positions captured at event time
#pragma pack(push, 1)
struct CompactCachedAnnotationEvent {
    uint64_t triggerId;           // 8 bytes - Which trigger created this
    int32_t timestamp_ms;         // 4 bytes - When annotation should appear (event time - lead time)
    int32_t eventTimestamp_ms;    // 4 bytes - Actual event time
    float worldX, worldY;         // 8 bytes - Target/anchor position
    float sourceX, sourceY;       // 8 bytes - Source/caster position
    uint32_t targetGuid_id;       // 4 bytes - String table index
    int32_t sampleDurationMs;     // 4 bytes - DuringDebuff sample duration
    int32_t debuffEndTime_ms;     // 4 bytes - When debuff ends
    int32_t actorDeathTime_ms;    // 4 bytes - When actor died
    int32_t negatedAt_ms;         // 4 bytes - When negated
    float projectileEndX;         // 4 bytes - Pre-computed end X
    float projectileEndY;         // 4 bytes - Pre-computed end Y
    uint16_t anchorFacing;        // 2 bytes - Facing * 10000
    uint8_t stackCount;           // 1 byte
    uint8_t deathBehavior;        // 1 byte - ActorDeathBehavior enum
    uint8_t flags;                // 1 byte - packed bools (see pack/unpack functions)
    uint8_t padding[3];           // 3 bytes
};
#pragma pack(pop)
static_assert(sizeof(CompactCachedAnnotationEvent) == 68, "CompactCachedAnnotationEvent must be 68 bytes");

// Bit 0: hasSourcePosition, Bit 1: hasFacing, Bit 2: isDuringDebuffSample,
// Bit 3: isActorTrigger, Bit 4: hasProjectileEnd
inline uint8_t packCachedEventFlags(bool hasSourcePosition, bool hasFacing, bool isDuringDebuffSample,
                                     bool isActorTrigger, bool hasProjectileEnd) {
    return (hasSourcePosition ? 0x01 : 0) |
           (hasFacing ? 0x02 : 0) |
           (isDuringDebuffSample ? 0x04 : 0) |
           (isActorTrigger ? 0x08 : 0) |
           (hasProjectileEnd ? 0x10 : 0);
}

inline void unpackCachedEventFlags(uint8_t flags, bool& hasSourcePosition, bool& hasFacing,
                                    bool& isDuringDebuffSample, bool& isActorTrigger, bool& hasProjectileEnd) {
    hasSourcePosition = (flags & 0x01) != 0;
    hasFacing = (flags & 0x02) != 0;
    isDuringDebuffSample = (flags & 0x04) != 0;
    isActorTrigger = (flags & 0x08) != 0;
    hasProjectileEnd = (flags & 0x10) != 0;
}

// RGBA floats in 0-1 to r | g<<8 | b<<16 | a<<24
inline uint32_t packColor(float r, float g, float b, float a) {
    return static_cast<uint32_t>(r * 255) |
           (static_cast<uint32_t>(g * 255) << 8) |
           (static_cast<uint32_t>(b * 255) << 16) |
           (static_cast<uint32_t>(a * 255) << 24);
}

inline void unpackColor(uint32_t packed, float& r, float& g, float& b, float& a) {
    r = (packed & 0xFF) / 255.0f;
    g = ((packed >> 8) & 0xFF) / 255.0f;
    b = ((packed >> 16) & 0xFF) / 255.0f;
    a = ((packed >> 24) & 0xFF) / 255.0f;
}

#pragma pack(push, 1)
struct StringTableHeader {
    uint32_t entry_count;       // Number of strings
    uint32_t total_bytes;       // Total bytes (excluding this header)
};
#pragma pack(pop)

#pragma pack(push, 1)
struct ContextSectionHeader {
    uint32_t active_aura_count;
    uint32_t annotation_count;
    uint32_t trigger_json_size;     // Size in bytes of triggers JSON string (0 if none)
    uint32_t position_snapshot_count;
    uint32_t cached_event_count;    // Pre-computed trigger cache events
};
#pragma pack(pop)

#pragma pack(push, 1)
struct EventsSectionHeader {
    uint32_t position_count;
    uint32_t combat_count;
    uint32_t spell_cast_count;
    uint32_t aura_count;
    uint32_t resource_count;
    uint32_t death_count;
    uint32_t resurrect_count;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct MetadataSectionHeader {
    uint32_t encounter_name_id; // String table index
    float map_x1, map_y1;       // Map bounds (from first map change)
    float map_x2, map_y2;
    uint32_t guid_name_count;   // GUID -> Name mappings
    uint32_t spell_name_count;  // SpellID -> Name mappings
    uint32_t combatant_count;   // CombatantInfo entries
    uint32_t map_change_count;  // Map change events
};
#pragma pack(pop)

// GUID to Name mapping entry
#pragma pack(push, 1)
struct GuidNameEntry {
    uint32_t guid_id;           // String table index for GUID
    uint32_t name_id;           // String table index for Name
};
#pragma pack(pop)

// Spell ID to Name mapping entry
#pragma pack(push, 1)
struct SpellNameEntry {
    uint32_t spell_id;
    uint32_t name_id;           // String table index
};
#pragma pack(pop)

// Compact combatant info header (talents follow as array of uint32_t;
// files before 1.2 used uint16_t)
#pragma pack(push, 1)
struct CompactCombatantInfo {
    uint32_t guid_id;           // String table index
    uint16_t spec_id;
    uint16_t talent_count;      // Followed by talent_count * uint32_t talent IDs
};
#pragma pack(pop)

// affiliation(2) | reaction(2) | controller(1) | unit_type(3)
inline uint8_t packUnitFlags(uint8_t affiliation, uint8_t reaction, uint8_t controller, uint8_t unitType) {
    return ((affiliation & 0x03) << 6) |
           ((reaction & 0x03) << 4) |
           ((controller & 0x01) << 3) |
           (unitType & 0x07);
}

inline void unpackUnitFlags(uint8_t packed, uint8_t& affiliation, uint8_t& reaction, uint8_t& controller, uint8_t& unitType) {
    affiliation = (packed >> 6) & 0x03;
    reaction = (packed >> 4) & 0x03;
    controller = (packed >> 3) & 0x01;
    unitType = packed & 0x07;
}

} // namespace snapshot
