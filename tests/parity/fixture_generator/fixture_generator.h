#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <rapidjson/document.h>

namespace parity {

// A local log line and the WCL event judged to be the same event.
struct EventMatch {
    std::string local_line;           // Raw log line from local file
    std::string wcl_event_json;       // WCL JSON event as string (serialized)
    int32_t local_timestamp_ms;       // Relative timestamp in local log
    int32_t wcl_timestamp_ms;         // Relative timestamp in WCL
    std::string event_type;           // Normalized event type (damage, heal, aura, etc.)
    std::string local_event_type;     // Original local event type (SPELL_DAMAGE, etc.)
    std::string wcl_event_type;       // Original WCL event type
    uint32_t spell_id;                // Spell ID for matching
    size_t wcl_page_index;            // Which page this event came from
    size_t wcl_event_index;           // Index within the page
};

// Actor mapping between local GUID and WCL actor ID
struct ActorMapping {
    int32_t wcl_id;
    std::string local_guid;
    std::string name;
    std::string type;  // "Player", "NPC", "Pet", etc.
};

struct LocalEvent {
    std::string raw_line;
    std::string event_type;
    int32_t timestamp_ms;
    std::string source_guid;
    std::string source_name;
    std::string dest_guid;
    std::string dest_name;
    uint32_t spell_id;
    int64_t amount;               // Damage/heal amount
    int64_t max_hp = 0;           // Target max HP (for matching)
    int64_t current_hp = 0;       // Target current HP
    bool is_periodic;             // DOT/HOT tick
    bool is_critical;
    bool is_support = false;      // _SUPPORT event (Aug evoker)
};

struct WclEvent {
    size_t page_index;            // Which page this came from
    size_t event_index;           // Index within the page
    std::string event_type;
    int32_t timestamp_ms;         // Relative to fight start
    int32_t source_id;
    int32_t target_id;
    uint32_t spell_id;
    int64_t amount;
    int64_t max_hp = 0;           // maxHitPoints from WCL
    int64_t current_hp = 0;       // hitPoints from WCL
    bool is_periodic;
    bool is_critical;
    bool has_support_id = false;  // Has supportID field (Aug evoker)
};

enum class EventCategory {
    Damage,
    Healing,
    Aura,
    Cast,
    Death,
    Miss,
    Summon,
    Other
};

class FixtureGenerator {
public:
    FixtureGenerator();
    ~FixtureGenerator();

    bool loadLocalLog(const std::string& filepath);

    // page1.json, page2.json, ... from the directory.
    bool loadWclPages(const std::string& directory);

    // Pairs local GUIDs with WCL ids by actor name.
    void buildActorMapping();

    // Pairs events from the two sources within the timestamp tolerance.
    std::vector<EventMatch> matchEvents(int32_t tolerance_ms = 100);

    // n samples per event type, spread across the fight.
    std::vector<EventMatch> sampleByType(
        const std::vector<EventMatch>& matches,
        size_t samples_per_type = 3
    );

    bool exportFixtures(
        const std::string& output_dir,
        const std::vector<EventMatch>& samples
    );

    const std::unordered_map<int32_t, ActorMapping>& getActorMapping() const {
        return actor_mapping_;
    }

    int64_t getLocalEncounterStartMs() const { return local_encounter_start_ms_; }
    int64_t getWclFightStartMs() const { return wcl_fight_start_ms_; }

private:
    LocalEvent parseLocalLine(const std::string& line);

    // Absolute milliseconds.
    int64_t parseLocalTimestamp(const std::string& date, const std::string& time);

    EventCategory categorizeEvent(const std::string& event_type);

    std::string wclToLocalEventType(const std::string& wcl_type);

    bool eventsMatch(
        const LocalEvent& local,
        const WclEvent& wcl,
        int32_t tolerance_ms
    );

    int32_t findWclActorByName(const std::string& name) const;

    std::string findLocalGuidByWclId(int32_t wcl_id) const;

    // -1 if not found.
    int32_t getWclIdFromGuid(const std::string& guid) const;

    // Storage
    std::vector<std::string> local_lines_;
    std::vector<LocalEvent> local_events_;
    std::vector<rapidjson::Document> wcl_pages_;
    std::vector<WclEvent> wcl_events_;

    // Mappings
    std::unordered_map<int32_t, ActorMapping> actor_mapping_;
    std::unordered_map<std::string, int32_t> name_to_wcl_id_;
    std::unordered_map<std::string, std::string> guid_to_name_;

    // Timing
    int64_t local_log_start_ms_ = 0;        // First timestamp in local log
    int64_t local_encounter_start_ms_ = 0;   // ENCOUNTER_START timestamp
    int64_t wcl_fight_start_ms_ = 0;         // Fight start from WCL metadata
    std::string encounter_start_line_;       // Raw ENCOUNTER_START line for fixtures
};

// Map WCL event types to local event types
const std::unordered_map<std::string, std::string> WCL_TO_LOCAL_EVENT_TYPE = {
    {"damage", "SPELL_DAMAGE"},
    {"heal", "SPELL_HEAL"},
    {"applybuff", "SPELL_AURA_APPLIED"},
    {"removebuff", "SPELL_AURA_REMOVED"},
    {"applydebuff", "SPELL_AURA_APPLIED"},
    {"removedebuff", "SPELL_AURA_REMOVED"},
    {"refreshbuff", "SPELL_AURA_REFRESH"},
    {"refreshdebuff", "SPELL_AURA_REFRESH"},
    {"applybuffstack", "SPELL_AURA_APPLIED_DOSE"},
    {"applydebuffstack", "SPELL_AURA_APPLIED_DOSE"},
    {"removebuffstack", "SPELL_AURA_REMOVED_DOSE"},
    {"removedebuffstack", "SPELL_AURA_REMOVED_DOSE"},
    {"cast", "SPELL_CAST_SUCCESS"},
    {"begincast", "SPELL_CAST_START"},
    {"death", "UNIT_DIED"},
    {"summon", "SPELL_SUMMON"},
    {"miss", "SPELL_MISSED"},
    {"resurrect", "SPELL_RESURRECT"}
};

// Categorize event types
inline EventCategory categorizeEventType(const std::string& type) {
    // Damage, including support damage such as Augmentation's Shifting Sands,
    // which WCL sends as "absorbed" with a supportID.
    if (type == "damage" || type == "SPELL_DAMAGE" || type == "SPELL_PERIODIC_DAMAGE" ||
        type == "SWING_DAMAGE_LANDED" || type == "RANGE_DAMAGE" ||
        type == "SPELL_DAMAGE_SUPPORT" || type == "SPELL_PERIODIC_DAMAGE_SUPPORT" ||
        type == "SWING_DAMAGE_LANDED_SUPPORT") {
        return EventCategory::Damage;
    }
    // Healing, including support healing.
    if (type == "heal" || type == "SPELL_HEAL" || type == "SPELL_PERIODIC_HEAL" ||
        type == "SPELL_HEAL_SUPPORT" || type == "SPELL_PERIODIC_HEAL_SUPPORT") {
        return EventCategory::Healing;
    }
    if (type.find("buff") != std::string::npos || type.find("AURA") != std::string::npos) {
        return EventCategory::Aura;
    }
    if (type == "cast" || type == "begincast" || type.find("CAST") != std::string::npos) {
        return EventCategory::Cast;
    }
    if (type == "death" || type == "UNIT_DIED") {
        return EventCategory::Death;
    }
    if (type == "miss" || type.find("MISSED") != std::string::npos) {
        return EventCategory::Miss;
    }
    if (type == "summon" || type == "SPELL_SUMMON") {
        return EventCategory::Summon;
    }
    return EventCategory::Other;
}

// Directory name for a category.
inline std::string categoryToDir(EventCategory cat) {
    switch (cat) {
        case EventCategory::Damage: return "damage";
        case EventCategory::Healing: return "healing";
        case EventCategory::Aura: return "auras";
        case EventCategory::Cast: return "casts";
        case EventCategory::Death: return "deaths";
        // Misses and summons are filed with damage.
        case EventCategory::Miss: return "damage";
        case EventCategory::Summon: return "damage";
        default: return "other";
    }
}

} // namespace parity
