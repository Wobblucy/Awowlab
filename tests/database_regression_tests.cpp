// Regression tests for database and extraction bugs found in review. Each
// case builds the smallest input that showed the bug.
#include <catch2/catch_all.hpp>
#include "../AwowLab/Database/CombatDatabase.h"
#include "../AwowLab/Database/OutputWriter.h"
#include "../AwowLab/Database/DebugDatabaseDumper.h"
#include "../AwowLab/Database/DeathDatabase.h"
#include "../AwowLab/Database/EventDatabase.h"
#include "../AwowLab/Database/CooldownCalculator.h"
#include "../AwowLab/Database/TalentDatabase.h"
#include "../AwowLab/Database/CooldownModifiers/CooldownModifierRegistry.h"
#include "../AwowLab/Txttokenizer/include/Txttokenizer/Txttokenizer.h"
#include "../AwowLab/Parser/Include/Actor/ActorMap.h"
#include "../AwowLab/Parser/Include/Structures/CombatTypes.h"
#include "../AwowLab/Core/StringInterner.h"
#include "../AwowLab/Core/Utils/ExecutablePath.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path writeTempFile(const std::string& name, const std::string& body) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary);
    out << body;
    return path;
}

ExtractedData extractText(const std::string& content) {
    tokenized_segment tokenizer;
    auto lines = tokenizer.tokenize(content.data(), content.size());
    return OutputWriter::extractAllEvents(lines);
}

} // namespace

TEST_CASE("Cooldown modifiers do not stack when the registry is reloaded",
          "[database][cooldown]") {
    // Every WarcraftLogs import reloads the registry. A 30s talent reduction
    // on a 120s cooldown has to stay 90s however many times that happens.
    auto path = writeTempFile("awowlab_cd_modifiers_test.json",
        R"({"flat_modifiers":[{"talent_spell_id":111,"modified_spell_id":222,"reduction_ms":30000}]})");

    auto& reg = awow::CooldownModifierRegistry::instance();
    CombatantInfoEvent ci;
    ci.player_guid = "Player-1-CDREG";
    ci.spec_id = 62;
    parser::TalentEntry talent{};
    talent.spell_id = 111;
    ci.talents.push_back(talent);
    awow::TalentDatabase talents;
    talents.loadFromCombatantInfo({ci});
    awow::CooldownCalculator calc(&talents, &reg);

    for (int load = 0; load < 3; ++load) {
        REQUIRE(reg.loadFromJson(path.string()));
        CHECK(calc.calculateCooldown("Player-1-CDREG", 222, 120000) == 90000);
    }

    reg.clear();
    std::filesystem::remove(path);
}

TEST_CASE("Friendly Fire meter counts only flagged friendly fire",
          "[database][combat][friendlyfire]") {
    // A boss hitting a player is not friendly fire, though the target is a
    // Player- guid. The meter has to agree with the drill-down, which only
    // takes records the extractor flagged.
    ActorMap map;
    auto boss = guidInterner().intern("Creature-0-1-2-3-200000-00FFREG01");
    auto p1 = guidInterner().intern("Player-1-FFREGA");
    auto p2 = guidInterner().intern("Player-1-FFREGB");

    CombatRecord bossHit{};
    bossHit.timestamp_ms = 1000;
    bossHit.amount = 500000;
    bossHit.effective_amount = 500000;
    bossHit.target_guid_id = p1;
    map[boss].damage_dealt_table.push_back(bossHit);

    CombatRecord ff{};
    ff.timestamp_ms = 2000;
    ff.amount = 1000;
    ff.effective_amount = 1000;
    ff.target_guid_id = p2;
    ff.flags = CombatEventFlags::FriendlyFire;
    map[p1].damage_dealt_table.push_back(ff);

    CombatDatabase db;
    db.loadFromActorMap(&map);
    auto rows = db.getRankedByFriendlyFire(0, 10000, 10);

    REQUIRE(rows.size() == 1);
    CHECK(rows[0].actor_guid == "Player-1-FFREGA");
    CHECK(rows[0].effective_amount == 1000);
}

TEST_CASE("An off-GCD spell mid-cast does not cancel the cast",
          "[database][cast]") {
    // Combustion goes off half way through a Pyroblast cast. The Pyroblast
    // still lands and must keep its cast start, with no cancelled entry.
    const std::string adv = ",Player-11-0E7DD19F,0000000000000000,23759830,29217467,156218,12359,"
                            "291791,2139,168,890897,0,97,120,0,2042.07,3350.48,2467,4.7194,728";
    std::string content =
        "12/4/2025 21:32:20.000-7  ENCOUNTER_START,3135,\"Dimensius\",16,20,2810\n"
        "12/4/2025 21:32:31.000-7  SPELL_CAST_START,Player-11-0E7DD19F,\"Methodpanda-Tichondrius-US\",0x514,0x80000000,"
        "0000000000000000,nil,0x80000000,0x80000000,11366,\"Pyroblast\",0x4\n"
        "12/4/2025 21:32:31.500-7  SPELL_CAST_SUCCESS,Player-11-0E7DD19F,\"Methodpanda-Tichondrius-US\",0x514,0x80000000,"
        "0000000000000000,nil,0x80000000,0x80000000,190319,\"Combustion\",0x4" + adv + "\n"
        "12/4/2025 21:32:34.000-7  SPELL_CAST_SUCCESS,Player-11-0E7DD19F,\"Methodpanda-Tichondrius-US\",0x514,0x80000000,"
        "Creature-0-3018-2810-26542-241517-00003256C6,\"Dimensius\",0x30a48,0x80000000,11366,\"Pyroblast\",0x4" + adv + "\n";

    auto data = extractText(content);

    const SpellCastEvent* pyro = nullptr;
    for (const auto& c : data.spellCastEvents) {
        CHECK_FALSE(c.cancelled);
        if (c.spell_id == 11366) pyro = &c;
    }
    REQUIRE(data.spellCastEvents.size() == 2);
    REQUIRE(pyro != nullptr);
    CHECK(pyro->end_timestamp_ms - pyro->start_timestamp_ms == 3000);
}

TEST_CASE("SPELL_CAST_FAILED of the spell being cast cancels it",
          "[database][cast]") {
    std::string content =
        "12/4/2025 21:32:20.000-7  ENCOUNTER_START,3135,\"Dimensius\",16,20,2810\n"
        "12/4/2025 21:32:31.000-7  SPELL_CAST_START,Player-11-0E7DD19F,\"Methodpanda-Tichondrius-US\",0x514,0x80000000,"
        "0000000000000000,nil,0x80000000,0x80000000,11366,\"Pyroblast\",0x4\n"
        "12/4/2025 21:32:32.000-7  SPELL_CAST_FAILED,Player-11-0E7DD19F,\"Methodpanda-Tichondrius-US\",0x514,0x80000000,"
        "0000000000000000,nil,0x80000000,0x80000000,11366,\"Pyroblast\",0x4,\"Interrupted\"\n";

    auto data = extractText(content);

    REQUIRE(data.spellCastEvents.size() == 1);
    CHECK(data.spellCastEvents[0].spell_id == 11366);
    CHECK(data.spellCastEvents[0].cancelled);
    CHECK(data.spellCastEvents[0].end_timestamp_ms - data.spellCastEvents[0].start_timestamp_ms == 1000);
}

TEST_CASE("Release after a second death is seen once the timelines are sorted",
          "[database][death]") {
    // Dies at 10s, battle-rezzed at 20s, dies again at 60s, releases and runs
    // back: position events resume at 90s with no SPELL_RESURRECT.
    const std::string guid = "Player-1-DEATHREG";
    std::vector<DeathEvent> deaths(2);
    deaths[0].actor_guid = guid; deaths[0].timestamp_ms = 10000; deaths[0].actor_type = ActorType::Player;
    deaths[1].actor_guid = guid; deaths[1].timestamp_ms = 60000; deaths[1].actor_type = ActorType::Player;
    std::vector<ResurrectEvent> res(1);
    res[0].actor_guid = guid; res[0].timestamp_ms = 20000;
    std::vector<ActorEvent> pos;
    for (int t : {5000, 25000, 90000, 95000}) {
        ActorEvent e;
        e.source_guid = guid;
        e.source_actor = "Me";
        e.actor_type = ActorType::Player;
        e.timestamp_ms = t;
        pos.push_back(e);
    }
    EventDatabase events;
    events.loadEvents(pos);
    DeathDatabase db;
    db.loadEvents(deaths, res, &events);

    CHECK(db.shouldHide(guid, 15000));
    CHECK_FALSE(db.shouldHide(guid, 30000));
    CHECK(db.shouldHide(guid, 70000));
    CHECK_FALSE(db.shouldHide(guid, 95000));
}

TEST_CASE("SPELL_INSTAKILL and its UNIT_DIED make one death",
          "[database][death]") {
    std::string content =
        "12/4/2025 21:35:40.000-7  ENCOUNTER_START,3135,\"Dimensius\",16,20,2810\n"
        "12/4/2025 21:35:44.425-7  SPELL_INSTAKILL,0000000000000000,nil,0x514,0x80000000,Player-11-0E745E05,"
        "\"Beekteek-Tichondrius-US\",0x514,0x80000000,1237718,\"Overcharged Mana\",0x40,0\n"
        "12/4/2025 21:35:44.426-7  UNIT_DIED,0000000000000000,nil,0x80000000,0x80000000,Player-11-0E745E05,"
        "\"Beekteek-Tichondrius-US\",0x514,0x80000000,0\n"
        "12/4/2025 21:35:45.000-7  UNIT_DIED,0000000000000000,nil,0x80000000,0x80000000,Player-11-0BC4BE8C,"
        "\"Bloodymun-Tichondrius-US\",0x514,0x80000000,0\n";

    auto data = extractText(content);

    REQUIRE(data.deathEvents.size() == 2);
    CHECK(data.deathEvents[0].actor_guid == "Player-11-0E745E05");
    CHECK(data.deathEvents[0].killing_spell_id == 1237718);
    CHECK(data.deathEvents[0].killing_spell_name == "Overcharged Mana");
    CHECK(data.deathEvents[1].actor_guid == "Player-11-0BC4BE8C");
}

TEST_CASE("Cooldown modifier loader skips entries it cannot use",
          "[database][cooldown]") {
    // resource_per_second is a divisor, and a wrong-typed field would trip a
    // rapidjson assert. Those entries are dropped; the good one still loads.
    auto path = writeTempFile("awowlab_cd_modifiers_bad_test.json", R"({
        "resource_modifiers":[
            {"talent_spell_id":1,"modified_spell_id":2,"power_type":3,"resource_per_second":0},
            {"talent_spell_id":"1","modified_spell_id":2,"power_type":3,"resource_per_second":30}
        ],
        "flat_modifiers":[
            {"talent_spell_id":111,"modified_spell_id":222,"reduction_ms":"30000"},
            {"talent_spell_id":111,"modified_spell_id":222,"reduction_ms":30000}
        ]})");

    auto& reg = awow::CooldownModifierRegistry::instance();
    REQUIRE(reg.loadFromJson(path.string()));
    CHECK(reg.modifierCount() == 1);
    CHECK(reg.getModifiersForTalent(1) == nullptr);

    reg.clear();
    std::filesystem::remove(path);
}

TEST_CASE("The executable directory is read through the wide API",
          "[core][path]") {
    // The wide path names the folder this test runs from. The narrow form is
    // the same folder with a trailing separator, or ".\" where the code page
    // cannot spell it.
    const std::filesystem::path dir = awow::getExecutableDirectoryPath();
    REQUIRE_FALSE(dir.empty());
    CHECK(std::filesystem::exists(dir / "database_tests.exe"));

    const std::string narrow = awow::getExecutableDirectory();
    REQUIRE_FALSE(narrow.empty());
    CHECK(narrow.back() == '\\');
    if (narrow != ".\\") {
        CHECK(std::filesystem::equivalent(std::filesystem::path(narrow), dir));
    }
}

TEST_CASE("Damage taken dump counts each target's distinct sources",
          "[database][dump]") {
    ActorMap map;
    auto boss = guidInterner().intern("Creature-0-1-2-3-200000-00DUMPSRC1");
    auto p1 = guidInterner().intern("Player-1-DUMPSRCA");
    auto p2 = guidInterner().intern("Player-1-DUMPSRCB");

    CombatRecord hit{};
    hit.timestamp_ms = 1000;
    hit.amount = 100;
    hit.effective_amount = 100;
    hit.target_guid_id = boss;
    map[p1].damage_dealt_table.push_back(hit);
    map[p1].damage_dealt_table.push_back(hit);
    map[p2].damage_dealt_table.push_back(hit);

    CombatDatabase db;
    db.loadFromActorMap(&map);

    auto path = std::filesystem::temp_directory_path() / "awowlab_damage_taken_dump_test.csv";
    REQUIRE(DebugDatabaseDumper::dumpDamageTakenIndex(path.string(), &db));

    std::ifstream in(path);
    std::string header, row;
    std::getline(in, header);
    std::getline(in, row);
    in.close();
    std::filesystem::remove(path);

    CHECK(header == "target_guid,total_damage,hit_count,max_hit,crit_count,source_count");
    CHECK(row == "Creature-0-1-2-3-200000-00DUMPSRC1,300,3,100,0,2");
}
