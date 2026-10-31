#include "PackageBuilder.h"

#include "../EventTypes.h"
#include "CsvUtil.h"
#include "ZipUtil.h"
#include "PackageCsvWriter.h"
#include "ResourceCsvWriter.h"
#include "Parser/SpecializationColors.h"
#include "Encounter/EncounterSegment.h"
#include "Core/Version.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

extern "C" {
#include "miniz.h"
}

namespace fs = std::filesystem;

namespace awow::analysis {
namespace {

const char* sourceName(PackageSource s) {
    return s == PackageSource::Wcl ? "wcl" : "local";
}

// Builds a unique temporary directory to stage the package in. Using the
// process-unique name rather than a fixed one keeps two exports from colliding.
fs::path makeStagingDir(std::error_code& ec) {
    std::random_device rd;
    for (int attempt = 0; attempt < 8; ++attempt) {
        std::ostringstream name;
        name << "awowlab_pkg_" << std::hex << rd();
        fs::path candidate = fs::temp_directory_path(ec) / name.str();
        if (ec) return {};
        if (fs::exists(candidate)) continue;
        if (fs::create_directories(candidate, ec)) return candidate;
        if (ec) return {};
    }
    ec = std::make_error_code(std::errc::file_exists);
    return {};
}

// Adds one file on disk to the archive under the given entry name. Reads the
// whole file, which is fine for these tables - the biggest is a few MB - and
// avoids miniz's file-adding path needing a matching working directory.
} // namespace

const char* packageTypeName(PackageType type) {
    switch (type) {
        case PackageType::BaselineSample:  return "baseline_sample";
        case PackageType::EncounterSet:    return "encounter_set";
        case PackageType::ReviewEncounter: return "review_encounter";
        case PackageType::DummyReview:     return "dummy_review";
    }
    return "unknown";
}

FightInfo fightInfoFromSegment(const EncounterSegment& segment,
                               const ExtractedData& data,
                               std::string_view playerGuid,
                               uint32_t displayIndex,
                               std::string_view sourceName) {
    FightInfo info;

    // Folder name sorts in pull order, so 03_ comes after 02_ rather than
    // alphabetically by boss name
    std::ostringstream key;
    key << std::setw(2) << std::setfill('0') << displayIndex;
    const std::string nameSlug = slugify(segment.encounterName);
    info.fightKey = nameSlug.empty() ? key.str() : key.str() + "_" + nameSlug;

    info.sourceName = std::string(sourceName);
    info.fightId = displayIndex;
    info.encounterId = segment.encounterId;
    info.encounterName = segment.encounterName;
    info.difficultyId = segment.difficultyId;
    info.keystoneLevel = segment.keystoneLevel;
    info.kill = segment.isSuccess;

    // Segment timestamps are already relative to the log, so the difference is
    // the pull length without needing the absolute start
    info.durationMs = segment.endTimestamp_ms - segment.startTimestamp_ms;

    info.playerGuid = std::string(playerGuid);

    if (!playerGuid.empty()) {
        auto nameIt = data.guidToNameMap.find(info.playerGuid);
        if (nameIt != data.guidToNameMap.end()) info.playerName = nameIt->second;

        for (const auto& ev : data.combatantInfoEvents) {
            if (ev.player_guid == playerGuid) {
                info.specId = ev.spec_id;
                info.itemLevel = ev.item_level;   // 0 when gear is unknown
                break;
            }
        }
    }

    // gameBuild and itemLevel stay empty: COMBAT_LOG_VERSION's buildVersion is
    // parsed but never recorded into ExtractedData, and gear is not parsed at
    // all. The caller fills them or the manifest reports them as unknown.
    return info;
}

std::string packageFileName(const PackageRequest& request,
                            const std::vector<FightData>& fights) {
    // <type>_<spec>_<hero>_<date>.zip, skipping the parts we cannot fill in
    // rather than writing the word "unknown" into a filename.
    std::string name = packageTypeName(request.type);

    uint16_t specId = 0;
    for (const auto& f : fights) {
        if (f.info.specId != 0) { specId = f.info.specId; break; }
    }
    if (specId != 0) {
        const std::string spec = slugify(parser::getSpecName(specId));
        if (!spec.empty()) name += "_" + spec;
    }

    // Hero tree is in the spec but nothing parses it yet, so it is left out of
    // the name instead of being guessed at.

    name += "_" + dateStampUtc();
    return name + ".zip";
}

std::string buildManifestJson(const PackageRequest& request,
                              const std::vector<FightData>& fights) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"package_version\": " << kPackageVersion << ",\n";
    // Falling back to the compiled-in version means a caller cannot ship a
    // manifest claiming the wrong build by forgetting to set this
    out << "  \"app_version\": "
        << jsonString(request.appVersion.empty() ? awow::VERSION : request.appVersion.c_str())
        << ",\n";
    out << "  \"package_type\": " << jsonString(packageTypeName(request.type)) << ",\n";
    out << "  \"source\": " << jsonString(sourceName(request.source)) << ",\n";
    out << "  \"created_utc\": " << jsonString(isoTimestampUtc()) << ",\n";
    out << "  \"notes\": " << jsonString(request.notes) << ",\n";

    if (request.sampling.present) {
        const auto& s = request.sampling;
        out << "  \"sampling\": {\n"
            << "    \"percentile\": " << s.percentile << ",\n"
            << "    \"stratum\": " << jsonString(s.stratum) << ",\n"
            << "    \"ranking_metric\": " << jsonString(s.rankingMetric) << ",\n"
            << "    \"date_window_start\": " << jsonString(s.dateWindowStart) << ",\n"
            << "    \"date_window_end\": " << jsonString(s.dateWindowEnd) << "\n"
            << "  },\n";
    }

    out << "  \"fights\": [\n";
    for (size_t i = 0; i < fights.size(); ++i) {
        const auto& f = fights[i].info;
        if (i) out << ",\n";
        out << "    {\n"
            << "      \"fight_key\": " << jsonString(f.fightKey) << ",\n"
            << "      \"source_name\": " << jsonString(f.sourceName) << ",\n"
            << "      \"fight_id\": " << f.fightId << ",\n"
            << "      \"encounter_id\": " << f.encounterId << ",\n"
            << "      \"encounter_name\": " << jsonString(f.encounterName) << ",\n"
            << "      \"difficulty_id\": " << f.difficultyId << ",\n"
            << "      \"difficulty_name\": "
            << jsonString(f.difficultyId ? getDifficultyName(f.difficultyId) : "") << ",\n"
            << "      \"keystone_level\": " << f.keystoneLevel << ",\n"
            << "      \"kill\": " << (f.kill ? "true" : "false") << ",\n"
            << "      \"duration_ms\": " << f.durationMs << ",\n"
            << "      \"player_guid\": " << jsonString(f.playerGuid) << ",\n"
            << "      \"player_name\": " << jsonString(f.playerName) << ",\n"
            << "      \"spec_id\": " << f.specId << ",\n"
            << "      \"spec_name\": "
            << jsonString(f.specId ? parser::getSpecName(f.specId) : "") << ",\n";

        // Null says "not collected" rather than implying a real value. The hero
        // tree is resolved from the build's talents, so it is empty on a local
        // log, where talents are not reported the way WCL reports them.
        out << "      \"hero_tree\": "
            << (f.heroTree.empty() ? std::string("null")
                                   : jsonString(f.heroTree))
            << ",\n";
        if (f.itemLevel != 0) {
            out << "      \"item_level\": " << f.itemLevel << ",\n";
        } else {
            out << "      \"item_level\": null,\n";
        }
        out << "      \"game_build\": " << jsonString(f.gameBuild) << "\n"
            << "    }";
    }
    out << "\n  ]\n}\n";
    return out.str();
}

namespace {

PackageResult buildPackageImpl(const std::string& outputDir,
                               const PackageRequest& request,
                               const std::vector<FightData>& fights) {
    PackageResult result;

    if (fights.empty()) {
        result.error = "no fights to package";
        return result;
    }
    for (const auto& f : fights) {
        if (f.data == nullptr) {
            result.error = "fight '" + f.info.fightKey + "' has no data";
            return result;
        }
    }

    std::error_code ec;
    fs::create_directories(pathFromUtf8(outputDir), ec);
    if (ec) {
        result.error = "cannot create output directory: " + ec.message();
        return result;
    }

    const fs::path staging = makeStagingDir(ec);
    if (staging.empty()) {
        result.error = "cannot create staging directory: " + ec.message();
        return result;
    }
    // Whatever happens from here, the staging directory goes away
    struct Cleanup {
        const fs::path& dir;
        ~Cleanup() { std::error_code e; fs::remove_all(dir, e); }
    } cleanup{staging};

    // Per-fight tables, written to disk first so the zip is assembled from
    // finished files - a writer that fails partway cannot corrupt the archive
    struct Entry { fs::path file; std::string entryName; };
    std::vector<Entry> entries;

    for (const auto& fight : fights) {
        const std::string key = fight.info.fightKey.empty()
                                    ? std::string("fight")
                                    : fight.info.fightKey;
        const fs::path dir = staging / "fights" / key;
        if (!fs::create_directories(dir, ec) && ec) {
            result.error = "cannot create fight directory: " + ec.message();
            return result;
        }

        const auto& data = *fight.data;
        const std::string& guid = fight.info.playerGuid;

        // Resolve a name for every spell the fight actually references. The
        // ids come from the events themselves, so this covers both sources -
        // local logs name spells inline, WCL does not name them at all.
        SpellNames names;
        if (request.resolveSpellName) {
            for (uint32_t spellId : data.collectAllSpellIds()) {
                const std::string_view resolved = request.resolveSpellName(spellId);
                if (!resolved.empty()) names.emplace(spellId, std::string(resolved));
            }
        }

        struct Table {
            const char* name;
            bool ok;
        };
        const std::vector<Table> written = {
            {"casts.csv",         writeCastsCsv(utf8FromPath(dir / "casts.csv"), data, guid, &names)},
            {"combat.csv",        writeCombatCsv(utf8FromPath(dir / "combat.csv"), data, guid, &names)},
            {"auras.csv",         writeAurasCsv(utf8FromPath(dir / "auras.csv"), data, guid, &names)},
            {"resources.csv",     writeResourcesCsv(utf8FromPath(dir / "resources.csv"), data, guid)},
            {"energize.csv",      writeEnergizeCsv(utf8FromPath(dir / "energize.csv"), data, guid, &names)},
            {"positions.csv",     writePositionsCsv(utf8FromPath(dir / "positions.csv"), data, guid)},
            {"deaths.csv",        writeDeathsCsv(utf8FromPath(dir / "deaths.csv"), data, guid, &names)},
            {"defense.csv",       writeDefenseCsv(utf8FromPath(dir / "defense.csv"), data, guid, &names)},
            {"utility.csv",       writeUtilityCsv(utf8FromPath(dir / "utility.csv"), data, guid, &names)},
            {"summons.csv",       writeSummonsCsv(utf8FromPath(dir / "summons.csv"), data, guid, &names)},
            {"cd_reductions.csv", writeCdReductionsCsv(utf8FromPath(dir / "cd_reductions.csv"), data, guid, &names)},
            {"combatant.json",    writeCombatantJson(utf8FromPath(dir / "combatant.json"), data, guid)},
            {"lookups.json",      writeLookupsJson(utf8FromPath(dir / "lookups.json"), data, &names)},
        };

        for (const auto& table : written) {
            if (!table.ok) {
                result.error = std::string("failed writing ") + table.name
                             + " for fight '" + key + "'";
                return result;
            }
            entries.push_back({dir / table.name,
                               "fights/" + key + "/" + table.name});
        }
    }

    // Manifest last, so it only describes tables that actually got written
    const fs::path manifestPath = staging / "manifest.json";
    {
        std::ofstream manifest(manifestPath, std::ios::binary);
        if (!manifest.is_open()) {
            result.error = "cannot write manifest";
            return result;
        }
        manifest << buildManifestJson(request, fights);
        manifest.flush();
        if (!manifest.good()) {
            result.error = "manifest write failed";
            return result;
        }
    }
    entries.push_back({manifestPath, "manifest.json"});

    if (!request.aplPath.empty()) {
        const fs::path aplPath = pathFromUtf8(request.aplPath);
        if (fs::exists(aplPath)) {
            entries.push_back({aplPath, "apl/" + utf8FromPath(aplPath.filename())});
        } else {
            result.error = "apl file not found: " + request.aplPath;
            return result;
        }
    }

    const fs::path zipPath = pathFromUtf8(outputDir) / pathFromUtf8(packageFileName(request, fights));

    // miniz takes a UTF-8 filename on Windows and opens it through the wide API
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, utf8FromPath(zipPath).c_str(), 0)) {
        result.error = "cannot create zip at " + utf8FromPath(zipPath);
        return result;
    }

    for (const auto& entry : entries) {
        if (!addFileToZip(zip, entry.file, entry.entryName)) {
            mz_zip_writer_end(&zip);
            fs::remove(zipPath, ec);      // no half-written archive left behind
            result.error = "failed adding " + entry.entryName + " to zip";
            return result;
        }
    }

    if (!mz_zip_writer_finalize_archive(&zip)) {
        mz_zip_writer_end(&zip);
        fs::remove(zipPath, ec);
        result.error = "failed finalizing zip";
        return result;
    }
    mz_zip_writer_end(&zip);

    result.ok = true;
    result.zipPath = utf8FromPath(zipPath);
    result.fileCount = entries.size();
    result.zipBytes = fs::file_size(zipPath, ec);
    return result;
}

} // namespace

PackageResult buildPackage(const std::string& outputDir,
                           const PackageRequest& request,
                           const std::vector<FightData>& fights) {
    // The filesystem calls that take no error_code throw, and an export
    // failing should come back as a message, not take the app down.
    try {
        return buildPackageImpl(outputDir, request, fights);
    } catch (const std::exception& e) {
        PackageResult result;
        result.error = std::string("package failed: ") + e.what();
        return result;
    }
}

} // namespace awow::analysis
