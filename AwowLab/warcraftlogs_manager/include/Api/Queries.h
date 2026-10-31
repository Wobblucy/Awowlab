#pragma once

namespace warcraftlogs {
namespace queries {

// Report metadata: fights and actors.
// Variables: code (String!)
constexpr const char* REPORT_METADATA = R"(
query($code: String!) @onlySpendUserPoints {
  reportData {
    report(code: $code) {
      startTime
      fights {
        id
        name
        startTime
        endTime
        encounterID
        difficulty
        keystoneLevel
        kill
      }
      masterData {
        actors {
          id
          gameID
          name
          type
          subType
          petOwner
          server
          icon
        }
      }
    }
  }
}
)";

// Every zone WCL tracks, each with its bosses and the difficulties it
// supports. This is the season listing, which needs no report or local log, so
// a baseline sample starts here. The encounter ids are WCL's own, which is what
// characterRankings takes.
//
// expansion.zones covers older content too; the caller picks the current one.
//
// Variables: none.
constexpr const char* WORLD_ZONES = R"(
query {
  worldData {
    expansions {
      id
      name
      zones {
        id
        name
        frozen
        encounters {
          id
          name
        }
        difficulties {
          id
          name
        }
      }
    }
  }
}
)";

// Top-ranked players for one boss, which is how a baseline sample picks whose
// pulls to study. Each ranking names the report and fight it came from, so the
// result is the (report, fight, player) list the package sampler needs.
//
// className and specName are strings, not spec ids ("Priest" / "Shadow"), and
// difficulty is on WCL's own scale - 3 Normal, 4 Heroic, 5 Mythic - not WoW's.
//
// Players with private profiles come back named "Anonymous" with no report
// code; there is nothing to fetch for them.
//
// includeCombatantInfo puts each ranked player's talents and gear on the
// entry. It is the only way to learn a build's hero talent tree before
// fetching the fight: there is no heroTree argument, and the filter string is
// accepted but ignored. It grows a page of 100 from about 114 KB to 2.9 MB,
// still in one query.
//
// Variables: encounterId (Int!), className (String), specName (String),
//            difficulty (Int), page (Int)
constexpr const char* CHARACTER_RANKINGS = R"(
query($encounterId: Int!, $className: String, $specName: String, $difficulty: Int, $page: Int) {
  worldData {
    encounter(id: $encounterId) {
      name
      characterRankings(
        className: $className,
        specName: $specName,
        difficulty: $difficulty,
        metric: dps,
        page: $page,
        includeCombatantInfo: true
      )
    }
  }
}
)";

// One page of events for a fight.
// Variables: code (String!), fightIDs ([Int!]), start (Float), end (Float)
constexpr const char* FIGHT_EVENTS = R"(
query($code: String!, $start: Float, $end: Float, $fightIDs: [Int!]) @onlySpendUserPoints {
  reportData {
    report(code: $code) {
      events(
        fightIDs: $fightIDs,
        startTime: $start,
        endTime: $end,
        dataType: All,
        includeResources: true,
        limit: 10000
      ) {
        data
        nextPageTimestamp
      }
    }
  }
}
)";

// The same events narrowed to one actor.
//
// A raid fight runs to about forty pages of ten thousand events, one query
// each, so fetching the whole raid to study one player wastes most of them.
// Passing sourceID filters on the server instead. Use it only when one actor
// is wanted, as for a baseline sample; a replay needs the whole raid and
// uses FIGHT_EVENTS.
//
// Despite the name, sourceID returns events in both directions, those the
// actor caused and those aimed at them, so a package built from this still
// has damage taken.
//
// Variables: code (String!), fightIDs ([Int!]), start (Float), end (Float),
//            sourceID (Int)
constexpr const char* FIGHT_EVENTS_FOR_ACTOR = R"(
query($code: String!, $start: Float, $end: Float, $fightIDs: [Int!], $sourceID: Int) @onlySpendUserPoints {
  reportData {
    report(code: $code) {
      events(
        fightIDs: $fightIDs,
        startTime: $start,
        endTime: $end,
        dataType: All,
        sourceID: $sourceID,
        includeResources: true,
        limit: 10000
      ) {
        data
        nextPageTimestamp
      }
    }
  }
}
)";

// Current API rate limit usage. No variables.
constexpr const char* RATE_LIMIT = R"(
{
  rateLimitData {
    limitPerHour
    pointsSpentThisHour
    pointsResetIn
    userPointsResetIn
  }
}
)";

// Zone ids and names. No variables.
constexpr const char* ZONES = R"(
{
  worldData {
    zones {
      id
      name
    }
  }
}
)";

} // namespace queries
} // namespace warcraftlogs
