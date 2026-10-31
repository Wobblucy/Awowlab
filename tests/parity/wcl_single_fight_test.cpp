#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "WarcraftLogsSession.h"
#include "Models/Fight.h"
#include "Database/AnalysisPackage/PackageBuilder.h"
#include "Settings/WclSettings.h"
#include "UnifiedSettings.h"
#include "SpellNameDatabase.h"
#include "HeroTalents.h"

namespace fs = std::filesystem;

// Package one named player's fight from one report, so a specific pull can be
// reviewed the way a baseline sample is. Driven by AWL_REPORT, AWL_FIGHT and
// AWL_PLAYER rather than the rankings, since this is a log someone handed over
// rather than one picked off the ladder.
TEST_CASE("package one fight for one player", "[wcl][live][onefight]") {
    if (!std::getenv("AWL_LIVE_WCL")) { WARN("AWL_LIVE_WCL not set; skipping"); return; }
    const char* code = std::getenv("AWL_REPORT");
    const char* fightEnv = std::getenv("AWL_FIGHT");
    const char* who = std::getenv("AWL_PLAYER");
    if (!code || !fightEnv || !who) { WARN("set AWL_REPORT, AWL_FIGHT, AWL_PLAYER"); return; }
    const int32_t wantFight = std::atoi(fightEnv);

    SettingsCache::instance().initialize();
    const std::string token =
        WclSettings::readFrom(SettingsCache::instance().get()).accessToken;
    if (token.empty()) { WARN("no stored WCL token; skipping"); return; }

    auto& spellDb = SpellNameDatabase::instance();
    spellDb.setDataDirectory(SPELL_NAMES_DIR);
    awow::HeroTalents::instance().load(HERO_TALENTS_DIR);

    warcraftlogs::WarcraftLogsSession session;
    session.setCacheEnabled(true, "./wcl_cache");
    session.setUserAccessToken(token);
    session.setRequestTimeout(120000);

    REQUIRE(session.initialize(code));
    REQUIRE(session.fetchReport());

    const auto* metadata = session.getReportMetadata();
    REQUIRE(metadata != nullptr);

    int32_t actorId = 0;
    std::string className, specName;
    for (const auto& actor : metadata->actors) {
        if (actor.name == who) {
            actorId = actor.id; className = actor.className;
            specName = actor.specName; break;
        }
    }
    if (actorId == 0) {
        WARN("no actor named " << who << " in " << code);
        for (const auto& actor : metadata->actors) {
            if (actor.type == "Player") UNSCOPED_INFO("  " << actor.name << " " << actor.className << " " << actor.specName);
        }
        return;
    }
    UNSCOPED_INFO(who << ": " << className << " " << specName << " (actor " << actorId << ")");

    uint32_t difficulty = 0; bool kill = false; int32_t durationMs = 0;
    std::string encounterName; int32_t encounterId = 0;
    for (const auto& fight : metadata->fights) {
        if (fight.id == wantFight) {
            difficulty = warcraftlogs::wclDifficultyToWow(fight.difficulty);
            kill = fight.kill; encounterName = fight.name;
            encounterId = fight.encounterID;
            durationMs = static_cast<int32_t>(fight.duration());
            break;
        }
    }
    UNSCOPED_INFO(encounterName << " diff=" << difficulty << " kill=" << kill
                  << " " << (durationMs / 1000) << "s");

    ExtractedData data = session.loadFight(wantFight, nullptr, actorId);
    REQUIRE_FALSE(data.combatEvents.empty());

    std::string playerGuid;
    for (const auto& [guid, name] : data.guidToNameMap) {
        if (name == who) { playerGuid = guid; break; }
    }
    spellDb.loadSpellIds(data.collectAllSpellIds());

    std::string tree;
    for (const auto& info : data.combatantInfoEvents) {
        if (info.player_guid != playerGuid) continue;
        std::vector<uint32_t> ids;
        for (const auto& talent : info.talents) ids.push_back(talent.spell_id);
        tree = awow::HeroTalents::instance().treeForTalents(ids);
        break;
    }
    UNSCOPED_INFO("hero tree: " << (tree.empty() ? "(unresolved)" : tree));
    UNSCOPED_INFO(data.combatEvents.size() << " combat, "
                  << data.spellCastEvents.size() << " casts, "
                  << data.auraEvents.size() << " auras");

    namespace pkg = awow::analysis;
    pkg::PackageRequest request;
    request.type = pkg::PackageType::ReviewEncounter;
    request.source = pkg::PackageSource::Wcl;
    request.notes = std::string(who) + " on " + encounterName;
    request.resolveSpellName = [&spellDb](uint32_t id) -> std::string_view {
        return spellDb.hasSpellName(id) ? std::string_view(spellDb.getSpellName(id))
                                        : std::string_view{};
    };

    pkg::FightData fight;
    fight.data = &data;
    fight.info.fightKey = "1_" + std::to_string(encounterId);
    fight.info.sourceName = code;
    fight.info.fightId = static_cast<uint32_t>(wantFight);
    fight.info.encounterId = static_cast<uint32_t>(encounterId);
    fight.info.encounterName = encounterName;
    fight.info.difficultyId = difficulty;
    fight.info.kill = kill;
    fight.info.durationMs = durationMs;
    fight.info.playerGuid = playerGuid;
    fight.info.playerName = who;
    fight.info.heroTree = tree;

    const auto result = pkg::buildPackage(
        (fs::path(PACKAGE_OUT_DIR) / "review").string(), request, {fight});
    if (result.ok) { UNSCOPED_INFO("zip: " << result.zipPath); }
    else { UNSCOPED_INFO("failed: " << result.error); }
    CHECK(result.ok);
}
