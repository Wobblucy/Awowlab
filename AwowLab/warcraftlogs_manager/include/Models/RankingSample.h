#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace warcraftlogs {

// One ranked pull: who, how well, and where to find the fight.
//
// reportCode and fightId turn "this player did 268k" into a fight the package
// builder can fetch and take apart.
struct RankedPull {
    std::string playerName;
    std::string reportCode;
    int32_t fightId = 0;

    double amount = 0.0;         // The ranking metric, dps by default
    int64_t durationMs = 0;
    int32_t itemLevel = 0;       // bracketData
    std::string serverName;
    std::string serverRegion;
    std::string guildName;       // empty for an unguilded character

    int32_t encounterId = 0;
    std::string encounterName;

    // The trait node entry ids this build took, straight off the ranking when
    // the query asked for combatant info. They are what names the hero talent
    // tree, which the reply states nowhere else - see HeroTalents. Empty when
    // the query did not ask for them.
    std::vector<uint32_t> talentEntryIds;
};

// Every ranked pull gathered for one baseline, across the encounters of a zone.
struct RankingSample {
    std::string className;       // as the API spells it, "DemonHunter"
    std::string specName;
    std::vector<RankedPull> pulls;

    // Encounters that returned nothing. Not a failure: a boss nobody has killed
    // on this spec yet ranks empty, and a baseline should say so rather than
    // look like it silently skipped one.
    std::vector<std::string> emptyEncounters;
};

// Read one characterRankings reply, as saved by dumpCharacterRankings.
//
// A reply is a page of at most 100, with hasMorePages saying whether more exist.
// Only the first page is read: a baseline wants the top of the ladder, and the
// pages past it are progressively less useful while costing a query each.
//
// Players who set their WCL profile to private come back named "Anonymous" with
// no report code. There is nothing to fetch for them, so they are skipped
// rather than counted into a sample that cannot be built.
//
// encounterId is recorded on each pull, since the reply doesn't carry it.
// Pulls are appended to out, so several encounters accumulate. Returns an
// empty string on success, otherwise why the reply couldn't be read.
std::string parseRankingReply(const std::string& json,
                              int32_t encounterId,
                              RankingSample& out);

} // namespace warcraftlogs
