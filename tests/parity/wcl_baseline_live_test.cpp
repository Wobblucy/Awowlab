#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>

#include "WarcraftLogsSession.h"
#include "Models/RankingSample.h"
#include "Models/Fight.h"
#include "Database/AnalysisPackage/PackageBuilder.h"
#include "Settings/WclSettings.h"
#include "Settings/RateLimitBackup.h"
#include "UnifiedSettings.h"
#include "SpellNameDatabase.h"
#include "HeroTalents.h"

namespace fs = std::filesystem;

// Builds a real baseline: reads the cached rankings, fetches each chosen fight
// scoped to the player who ranked, and runs them through the ordinary package
// exporter. Spends one query per page per fight, so it is gated.
TEST_CASE("a baseline is built from ranked fights", "[wcl][live][baseline]") {
    if (!std::getenv("AWL_LIVE_WCL")) {
        WARN("AWL_LIVE_WCL not set; skipping");
        return;
    }

    // Keep the run small: the point is to see the path work end to end, not to
    // spend the hour's budget proving it twice.
    const size_t pullsPerEncounter =
        std::getenv("AWL_PULLS") ? std::atoi(std::getenv("AWL_PULLS")) : 1;
    const size_t maxFights =
        std::getenv("AWL_FIGHTS") ? std::atoi(std::getenv("AWL_FIGHTS")) : 3;

    const std::string rankingsDir = std::string(WCL_CACHE_DIR) + "/rankings";
    if (!fs::exists(rankingsDir)) { WARN("no cached rankings; skipping"); return; }

    warcraftlogs::RankingSample sample;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(rankingsDir, ec)) {
        if (entry.path().extension() != ".json") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        std::string json((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        warcraftlogs::parseRankingReply(
            json, std::atoi(entry.path().filename().string().c_str()), sample);
    }
    REQUIRE_FALSE(sample.pulls.empty());

    // AWL_HERO narrows the sample to one hero talent tree. There is no
    // heroTree argument on the rankings query and its filter string is
    // ignored, but includeCombatantInfo puts each ranked build's talents on
    // the entry, so the tree is known here - before anything is fetched.
    awow::HeroTalents::instance().load(HERO_TALENTS_DIR);
    const char* wantTree = std::getenv("AWL_HERO");

    std::vector<const warcraftlogs::RankedPull*> chosen;
    std::unordered_map<int32_t, size_t> taken;
    size_t wrongTree = 0, unknownTree = 0;
    for (const auto& pull : sample.pulls) {
        if (wantTree) {
            if (pull.talentEntryIds.empty()) { ++unknownTree; continue; }
            const std::string tree =
                awow::HeroTalents::instance().treeForTalents(pull.talentEntryIds);
            if (tree != wantTree) { ++wrongTree; continue; }
        }
        if (taken[pull.encounterId] >= pullsPerEncounter) continue;
        ++taken[pull.encounterId];
        chosen.push_back(&pull);
        if (chosen.size() >= maxFights) break;
    }
    if (wantTree) {
        UNSCOPED_INFO("hero tree " << wantTree << ": kept " << chosen.size()
                      << ", skipped " << wrongTree << " of another tree and "
                      << unknownTree << " with no talents on the ranking");
    }
    REQUIRE_FALSE(chosen.empty());

    awow::registerRateLimitBackups();
    SettingsCache::instance().initialize();
    const auto& settings = SettingsCache::instance().get();
    const std::string token = WclSettings::readFrom(settings).accessToken;
    if (token.empty()) { WARN("no stored WCL token; skipping"); return; }

    auto& spellDb = SpellNameDatabase::instance();
    spellDb.setDataDirectory(SPELL_NAMES_DIR);

    std::vector<ExtractedData> fetched;
    fetched.reserve(chosen.size());
    std::vector<awow::analysis::FightData> fights;

    size_t index = 0;
    for (const auto* pull : chosen) {
        ++index;
        warcraftlogs::WarcraftLogsSession session;
        session.setCacheEnabled(true, "./wcl_cache");
        session.setUserAccessToken(token);
        // A long fight returns far more per page than the thirty second
        // default allows for - The Coiled Altar runs to ten minutes and timed
        // out reproducibly at exactly the same pull.
        session.setRequestTimeout(120000);

        if (!session.initialize(pull->reportCode) || !session.fetchReport()) {
            WARN("could not open report " << pull->reportCode);
            continue;
        }

        int32_t actorId = 0;
        if (const auto* metadata = session.getReportMetadata()) {
            for (const auto& actor : metadata->actors) {
                if (actor.name == pull->playerName) { actorId = actor.id; break; }
            }
        }
        if (actorId == 0) { WARN("player not in report"); continue; }

        // A fetch can fail mid-run - most likely the hourly rate limit, since
        // each fight is several pages and each page is a query. Catching it
        // keeps the fights already gathered instead of losing the whole run.
        ExtractedData data;
        try {
            data = session.loadFight(pull->fightId, nullptr, actorId);
        } catch (const std::exception& e) {
            WARN("stopped after " << fights.size() << " fights: " << e.what());
            break;
        }
        if (data.combatEvents.empty()) { WARN("fight had no events"); continue; }

        // The tree this build took, read from the talents the fight carries.
        // Only knowable here - the rankings query has no argument for it - so a
        // fight of the wrong tree is fetched and then dropped.
        std::string rankedGuid;
        for (const auto& [guid, name] : data.guidToNameMap) {
            if (name == pull->playerName) { rankedGuid = guid; break; }
        }
        std::string tree;
        for (const auto& info : data.combatantInfoEvents) {
            if (info.player_guid != rankedGuid) continue;
            std::vector<uint32_t> ids;
            ids.reserve(info.talents.size());
            for (const auto& talent : info.talents) ids.push_back(talent.spell_id);
            tree = awow::HeroTalents::instance().treeForTalents(ids);
            break;
        }


        UNSCOPED_INFO(pull->encounterName << " / " << pull->playerName
                      << ": " << data.combatEvents.size() << " combat, "
                      << data.spellCastEvents.size() << " casts, "
                      << data.missedEvents.size() << " misses, "
                      << data.energizeEvents.size() << " energize");

        std::string playerGuid;
        for (const auto& [guid, name] : data.guidToNameMap) {
            if (name == pull->playerName) { playerGuid = guid; break; }
        }

        fetched.push_back(std::move(data));
        const ExtractedData& stored = fetched.back();
        spellDb.loadSpellIds(stored.collectAllSpellIds());

        awow::analysis::FightData fight;
        fight.data = &stored;
        // Same as the shipping path: the report says which tier the fight was
        // fought at, and without it the manifest reports difficulty 0.
        uint32_t fightDifficulty = 0;
        if (const auto* metadata = session.getReportMetadata()) {
            for (const auto& reported : metadata->fights) {
                if (reported.id == pull->fightId) {
                    // WCL's scale is not WoW's - 5 is Mythic there, 10-player
                    // Heroic here - so it converts at the boundary like the
                    // shipping path does.
                    fightDifficulty =
                        warcraftlogs::wclDifficultyToWow(reported.difficulty);
                    break;
                }
            }
        }
        fight.info.difficultyId = fightDifficulty;
        fight.info.heroTree = tree;
        fight.info.fightKey = std::to_string(index) + "_"
                            + std::to_string(pull->encounterId);
        fight.info.sourceName = pull->reportCode;
        fight.info.fightId = static_cast<uint32_t>(pull->fightId);
        fight.info.encounterId = static_cast<uint32_t>(pull->encounterId);
        fight.info.encounterName = pull->encounterName;
        fight.info.kill = true;
        fight.info.durationMs = static_cast<int32_t>(pull->durationMs);
        fight.info.playerGuid = playerGuid;
        fight.info.playerName = pull->playerName;
        fight.info.specId = 577;                      // Havoc
        fight.info.itemLevel = static_cast<uint32_t>(pull->itemLevel);
        fights.push_back(std::move(fight));
    }

    REQUIRE_FALSE(fights.empty());

    awow::analysis::PackageRequest request;
    request.type = awow::analysis::PackageType::BaselineSample;
    request.source = awow::analysis::PackageSource::Wcl;
    request.notes = "Demon Hunter Havoc baseline from The Venomous Abyss";
    request.sampling.present = true;
    request.sampling.rankingMetric = "dps";
    request.sampling.stratum = "raid";
    request.resolveSpellName = [&spellDb](uint32_t id) -> std::string_view {
        return spellDb.hasSpellName(id) ? std::string_view(spellDb.getSpellName(id))
                                        : std::string_view{};
    };

    const auto outDir = fs::path(PACKAGE_OUT_DIR) / "baseline";
    fs::remove_all(outDir, ec);

    const auto result =
        awow::analysis::buildPackage(outDir.string(), request, fights);
    INFO("error: " << result.error);
    REQUIRE(result.ok);

    UNSCOPED_INFO("zip: " << result.zipPath);
    UNSCOPED_INFO("fights: " << fights.size() << "  files: " << result.fileCount);
    CHECK(result.fileCount == fights.size() * 13 + 1);   // thirteen tables plus manifest
}
