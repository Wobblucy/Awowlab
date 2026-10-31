#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct ExtractedData;
struct EncounterSegment;

namespace awow::analysis {

// What the package is for. The analysis side reads this to know which
// questions the data is meant to answer.
enum class PackageType {
    BaselineSample,
    EncounterSet,
    ReviewEncounter,
    DummyReview,
};

const char* packageTypeName(PackageType type);

// Where the fight came from. Both paths converge on ExtractedData, but the
// analysis needs to know which one, because a WCL pull has no haste and a local
// log has no report code.
enum class PackageSource {
    LocalLog,
    Wcl,
};

// Everything about one fight that is not in ExtractedData itself: the encounter
// identity, whether it was a kill, and who the package is about. Most of this
// comes from EncounterSegment; the caller fills it because ExtractedData
// carries no encounter metadata.
struct FightInfo {
    std::string fightKey;        // Folder name under fights/, e.g. "01_midnight_falls"
    std::string sourceName;      // Log filename, or WCL report code
    uint32_t fightId = 0;        // WCL fight id, or the segment index for a local log
    uint32_t encounterId = 0;    // 0 for M+ dungeons
    std::string encounterName;
    uint32_t difficultyId = 0;
    uint32_t keystoneLevel = 0;  // 0 when not M+
    bool kill = false;
    int32_t durationMs = 0;

    std::string playerGuid;      // Scopes every table; empty writes all actors
    std::string playerName;
    uint16_t specId = 0;
    // Which hero talent tree the build took, e.g. "Aldrachi Reaver". Empty
    // when it could not be resolved. It matters because an action priority
    // list branches on it - Reaver's Glaive is 4.73 casts a minute in one tree
    // and does not exist in the other - so a package that pools trees reports
    // rates describing neither.
    std::string heroTree;
    uint32_t itemLevel = 0;      // 0 when unknown - gear is not parsed yet
    std::string gameBuild;
};

// Baseline samples carry how the fight was picked. Left empty for other types.
struct SamplingInfo {
    bool present = false;
    double percentile = 0.0;
    std::string stratum;
    std::string rankingMetric;
    std::string dateWindowStart;
    std::string dateWindowEnd;
};

struct PackageRequest {
    PackageType type = PackageType::ReviewEncounter;
    PackageSource source = PackageSource::LocalLog;
    std::string appVersion;
    std::string notes;           // Free text from the card, e.g. the fight plan
    std::string aplPath;         // Optional file copied in for baseline builds
    SamplingInfo sampling;

    // Resolves a spell id to a name. Local logs name spells inline but WCL
    // events carry bare ids, so without this a WCL package exports numbers
    // where a local one exports names - same schema, different usefulness
    // depending on where the fight came from.
    //
    // Database cannot call SpellNameDatabase itself: awowlab_lib links
    // Database, so reaching back would make the dependency circular. The
    // caller passes the lookup in instead, which also keeps this usable from
    // tests without the gzip database on disk.
    //
    // Returning an empty view means "no name known"; the column is then left
    // blank rather than filled with a placeholder.
    std::function<std::string_view(uint32_t)> resolveSpellName;
};

// One fight's data plus the metadata describing it.
struct FightData {
    const ExtractedData* data = nullptr;
    FightInfo info;
};

struct PackageResult {
    bool ok = false;
    std::string zipPath;         // Where the archive landed
    std::string error;           // Why it did not, when ok is false
    size_t fileCount = 0;
    uint64_t zipBytes = 0;
};

// Writes every table for every fight, plus manifest.json, into one zip.
//
// The archive is built in a temporary directory that is removed afterwards, so
// a failure partway through does not leave half a package behind, and the zip
// only appears once it is complete.
//
// outputDir is where the zip goes; the name follows the spec's
// <type>_<spec>_<hero>_<date>.zip, with the pieces that are unknown left out
// rather than written as "unknown".
PackageResult buildPackage(const std::string& outputDir,
                           const PackageRequest& request,
                           const std::vector<FightData>& fights);

// Fills a FightInfo from the encounter the user picked plus the fight's own
// events, so a caller does not have to restate what both already know.
//
// The encounter side comes from EncounterSegment: id, name, difficulty,
// keystone level, whether it was a kill, and how long it ran. The player side
// comes from ExtractedData - the name against playerGuid, and the spec from
// that player's COMBATANT_INFO.
//
// What it cannot fill:
//  - gameBuild. COMBAT_LOG_VERSION carries buildVersion but nothing records it
//    into ExtractedData, so the caller sets it or it stays empty.
//  - itemLevel on a local log. WCL reports carry a gear array so it is filled
//    from there; local COMBATANT_INFO gear is not parsed yet.
//  - fightId for local logs, which have no report-wide fight numbering; pass
//    the segment's index among the encounters shown to the user.
//
// fightKey becomes "<index>_<encounter name>", slugified, e.g. 03_midnight_falls,
// so the folders sort in pull order rather than alphabetically.
FightInfo fightInfoFromSegment(const EncounterSegment& segment,
                               const ExtractedData& data,
                               std::string_view playerGuid,
                               uint32_t displayIndex,
                               std::string_view sourceName);

// The zip name for a request, exposed so a caller can show it before building
// and so the tests can check it without writing anything.
std::string packageFileName(const PackageRequest& request,
                            const std::vector<FightData>& fights);

// The manifest as a string, exposed for testing - buildPackage writes this.
std::string buildManifestJson(const PackageRequest& request,
                              const std::vector<FightData>& fights);

// Format version of the package layout. Analysis scripts check this and refuse
// what they do not understand, so it has to go up whenever a column moves or a
// file changes meaning.
constexpr int kPackageVersion = 1;

} // namespace awow::analysis
