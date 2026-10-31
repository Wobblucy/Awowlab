#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace warcraftlogs {

// One boss, one dungeon in a keystone zone, or one dummy configuration. The id
// is WCL's own encounter id, which is what characterRankings takes.
struct SeasonEncounter {
    int32_t id = 0;
    std::string name;
};

// What a zone is for. A baseline built from one kind should not be mixed with
// another: a dummy pull is a fixed-length test with nothing interrupting it,
// where a boss pull is the same rotation surviving movement and phases. The
// difference between them is the interesting part, so they stay separate.
enum class ZoneKind {
    Raid,       // Four raid tiers: Mythic, Heroic, Normal, LFR.
    Dungeon,    // A keystone zone, one "Dungeon" difficulty.
    Dummy,      // Target dummy configurations, Normal only.
    Aggregate,  // The whole instance ranked as one encounter.
};

struct SeasonZone {
    int32_t id = 0;
    std::string name;
    ZoneKind kind = ZoneKind::Raid;
    std::vector<SeasonEncounter> encounters;

    // WCL's difficulty ids for this zone: raids list Mythic/Heroic/Normal/LFR
    // as 5/4/3/1, keystone zones list a single "Dungeon" as 10.
    std::vector<int32_t> difficultyIds;
};

// Pulls the live season out of a WORLD_ZONES reply.
//
// The reply holds every expansion WCL has ever tracked, and several copies of
// each season within one - a PTR zone, a beta zone, and the frozen previous
// season all sit beside the live one under the same expansion. The live season
// is in the newest expansion and is not frozen.
//
// Zones are classified by shape rather than by name, because name rules don't
// hold: the live raid and its PTR copy differ only
// by a parenthesis, an aggregate zone is not always called "Complete Raid",
// and "Dummy Dome" is real content that shipped as a rankable zone. What does
// separate them is the difficulties they offer and how many bosses they hold -
// see zoneKindOf.
//
// json is the reply as saved, including the outer data/worldData. Empty when
// the reply has no zone of that kind or can't be read.
std::vector<SeasonZone> parseSeasonZones(const std::string& json, ZoneKind kind);

// Every live zone, whatever its kind, newest-looking first. For a picker that
// shows the user what is available instead of guessing on their behalf.
std::vector<SeasonZone> parseAllSeasonZones(const std::string& json);

} // namespace warcraftlogs
