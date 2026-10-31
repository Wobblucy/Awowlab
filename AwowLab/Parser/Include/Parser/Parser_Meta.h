#pragma once
#include <vector>
#include <string_view>
#include <charconv>
#include "../structures.h"
#include "Parser.h"
#include "ParserDebug.h"
#include "ParserMacros.h"
#include "Parser_Damage.h"  // For util namespace

namespace parser {

// Meta Event Data Structures
// These are events that provide metadata about the log or game state

struct CombatLogVersionData {
    uint32_t version;
    uint32_t advanced_log_enabled;
    std::string_view build_version;
    uint32_t project_id;
};

struct ZoneChangeData {
    uint32_t zone_id;
    std::string_view zone_name;
    uint32_t difficulty_id;
};

struct MapChangeData {
    uint32_t map_id;
    std::string_view map_name;
    float x1, y1;  // Top-left corner
    float x2, y2;  // Bottom-right corner
};

struct ArenaMatchData {
    int32_t timestamp_ms;       // Relative to log start (negative = before encounter)
    uint32_t instance_id;
    std::string_view arena_name;
    uint32_t team_size;
    bool is_rated;
    // For ARENA_MATCH_END
    uint32_t winner_team;
    int32_t duration_ms;        // Duration can be int32_t for consistency
    uint32_t team1_rating;
    uint32_t team2_rating;
};

// COMBAT_LOG_VERSION Parser
// Token count: 8+
// Structure: date,time,event,version,advancedLogEnabled,buildVersion,projectID

struct CombatLogVersionTag {};

template <>
struct EventParser<CombatLogVersionTag> {
    static constexpr size_t minimum_token_count() { return 7; }

    static CombatLogVersionData parse_and_return(const std::vector<std::string_view>& tokens) {
        CombatLogVersionData data{};

        PARSER_VALIDATE("COMBAT_LOG_VERSION", tokens, minimum_token_count(), data);

        data.version = util::parseUint32(tokens[3]);

        data.advanced_log_enabled = util::parseUint32(tokens[4]);

        data.build_version = tokens[5];

        data.project_id = util::parseUint32(tokens[6]);

        PARSER_FINALIZE("COMBAT_LOG_VERSION", tokens);
        PARSER_DEBUG("COMBAT_LOG_VERSION",
            "version=" << data.version
            << " advanced=" << data.advanced_log_enabled
            << " build=" << data.build_version);

        return data;
    }
};

// ZONE_CHANGE Parser
// Token count: 6
// Structure: date,time,event,zoneID,zoneName,difficultyID

struct ZoneChangeTag {};

template <>
struct EventParser<ZoneChangeTag> {
    static constexpr size_t expected_token_count() { return 6; }

    static ZoneChangeData parse_and_return(const std::vector<std::string_view>& tokens) {
        ZoneChangeData data{};

        PARSER_VALIDATE("ZONE_CHANGE", tokens, expected_token_count(), data);

        data.zone_id = util::parseUint32(tokens[3]);

        data.zone_name = tokens[4];

        data.difficulty_id = util::parseUint32(tokens[5]);

        PARSER_FINALIZE("ZONE_CHANGE", tokens);
        PARSER_DEBUG("ZONE_CHANGE",
            "zone=" << data.zone_id << " (" << data.zone_name << ")"
            << " difficulty=" << data.difficulty_id);

        return data;
    }
};

// MAP_CHANGE Parser
// Token count: 8
// Structure: date,time,event,mapID,mapName,x1,y1,x2,y2

struct MapChangeTag {};

template <>
struct EventParser<MapChangeTag> {
    static constexpr size_t expected_token_count() { return 9; }

    static MapChangeData parse_and_return(const std::vector<std::string_view>& tokens) {
        MapChangeData data{};

        PARSER_VALIDATE("MAP_CHANGE", tokens, expected_token_count(), data);

        data.map_id = util::parseUint32(tokens[3]);

        data.map_name = tokens[4];

        data.x1 = util::parseFloat(tokens[5]);
        data.y1 = util::parseFloat(tokens[6]);
        data.x2 = util::parseFloat(tokens[7]);
        data.y2 = util::parseFloat(tokens[8]);

        PARSER_FINALIZE("MAP_CHANGE", tokens);
        PARSER_DEBUG("MAP_CHANGE",
            "map=" << data.map_id << " (" << data.map_name << ")"
            << " bounds=(" << data.x1 << "," << data.y1 << ")-("
            << data.x2 << "," << data.y2 << ")");

        return data;
    }
};

// ARENA_MATCH_START Parser
// Token count: 7
// Structure: date,time,event,instanceID,arenaName,teamSize,isRated

struct ArenaMatchStartTag {};

template <>
struct EventParser<ArenaMatchStartTag> {
    static constexpr size_t expected_token_count() { return 7; }

    static ArenaMatchData parse_and_return(const std::vector<std::string_view>& tokens) {
        ArenaMatchData data{};

        PARSER_VALIDATE("ARENA_MATCH_START", tokens, expected_token_count(), data);

        data.timestamp_ms = relative_timestamp_ms(EventParser<void>::parse_timestamp_ms(tokens));

        data.instance_id = util::parseUint32(tokens[3]);

        data.arena_name = tokens[4];

        data.team_size = util::parseUint32(tokens[5]);

        data.is_rated = util::parseBool(tokens[6]);

        PARSER_FINALIZE("ARENA_MATCH_START", tokens);
        PARSER_DEBUG("ARENA_MATCH_START",
            "instance=" << data.instance_id << " arena=" << data.arena_name
            << " size=" << data.team_size << " rated=" << (data.is_rated ? "yes" : "no"));

        return data;
    }
};

// ARENA_MATCH_END Parser
// Token count: 10
// Structure: date,time,event,winningTeam,duration,newTeam1Rating,newTeam2Rating

struct ArenaMatchEndTag {};

template <>
struct EventParser<ArenaMatchEndTag> {
    static constexpr size_t expected_token_count() { return 7; }

    static ArenaMatchData parse_and_return(const std::vector<std::string_view>& tokens) {
        ArenaMatchData data{};

        PARSER_VALIDATE("ARENA_MATCH_END", tokens, expected_token_count(), data);

        data.timestamp_ms = relative_timestamp_ms(EventParser<void>::parse_timestamp_ms(tokens));

        data.winner_team = util::parseUint32(tokens[3]);

        data.duration_ms = util::parseUint32(tokens[4]);

        data.team1_rating = util::parseUint32(tokens[5]);
        data.team2_rating = util::parseUint32(tokens[6]);

        PARSER_FINALIZE("ARENA_MATCH_END", tokens);
        PARSER_DEBUG("ARENA_MATCH_END",
            "winner=" << data.winner_team << " duration=" << data.duration_ms << "ms"
            << " ratings=" << data.team1_rating << "/" << data.team2_rating);

        return data;
    }
};

// WORLD_MARKER_PLACED Parser
// Token count: 6
// Structure: date,time,event,markerType,x,y

struct WorldMarkerPlacedTag {};

template <>
struct EventParser<WorldMarkerPlacedTag> {
    static constexpr size_t expected_token_count() { return 6; }

    struct WorldMarkerData {
        int32_t timestamp_ms;   // Relative to log start (negative = before encounter)
        uint32_t marker_type;
        float x;
        float y;
    };

    static WorldMarkerData parse_and_return(const std::vector<std::string_view>& tokens) {
        WorldMarkerData data{};

        PARSER_VALIDATE("WORLD_MARKER_PLACED", tokens, expected_token_count(), data);

        data.timestamp_ms = relative_timestamp_ms(EventParser<void>::parse_timestamp_ms(tokens));

        data.marker_type = util::parseUint32(tokens[3]);
        data.x = util::parseFloat(tokens[4]);
        data.y = util::parseFloat(tokens[5]);

        PARSER_FINALIZE("WORLD_MARKER_PLACED", tokens);
        PARSER_DEBUG("WORLD_MARKER_PLACED",
            "marker=" << data.marker_type
            << " pos=(" << data.x << ", " << data.y << ")");

        return data;
    }
};

// WORLD_MARKER_REMOVED Parser
// Token count: 4
// Structure: date,time,event,markerType

struct WorldMarkerRemovedTag {};

template <>
struct EventParser<WorldMarkerRemovedTag> {
    static constexpr size_t expected_token_count() { return 4; }

    struct WorldMarkerRemovedData {
        int32_t timestamp_ms;   // Relative to log start (negative = before encounter)
        uint32_t marker_type;
    };

    static WorldMarkerRemovedData parse_and_return(const std::vector<std::string_view>& tokens) {
        WorldMarkerRemovedData data{};

        PARSER_VALIDATE("WORLD_MARKER_REMOVED", tokens, expected_token_count(), data);

        data.timestamp_ms = relative_timestamp_ms(EventParser<void>::parse_timestamp_ms(tokens));

        data.marker_type = util::parseUint32(tokens[3]);

        PARSER_FINALIZE("WORLD_MARKER_REMOVED", tokens);
        PARSER_DEBUG("WORLD_MARKER_REMOVED", "marker=" << data.marker_type);

        return data;
    }
};

} // namespace parser
