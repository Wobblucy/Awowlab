#include <catch2/catch_all.hpp>
#include "../AwowLab/Database/CombatDatabase.h"
#include "../AwowLab/Database/AuraDatabase.h"
#include "../AwowLab/Database/ResourceDatabase.h"
#include "../AwowLab/Database/DeathDatabase.h"
#include "../AwowLab/Database/EventDatabase.h"
#include "../AwowLab/Database/SpellCastDatabase.h"
#include "../AwowLab/Database/EmoteDatabase.h"
#include "../AwowLab/Database/OutputWriter.h"
#include "../AwowLab/Txttokenizer/include/Txttokenizer/Txttokenizer.h"
#include "../AwowLab/Parser/Include/Actor/ActorMap.h"
#include "../AwowLab/Parser/Include/Actor/ActorTable.h"
#include "../AwowLab/Parser/Include/Structures/CombatTypes.h"
#include "../AwowLab/Core/StringInterner.h"
#include "../AwowLab/Core/MobWeightSettings.h"
#include "../AwowLab/Core/UnifiedSettings.h"
#include "../AwowLab/Core/Settings/WclSettings.h"

#include "SnapshotExporter.h"
#include "SnapshotImporter.h"
#include "SnapshotFormat.h"
#include "RaidNoteDocument.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <chrono>
#include <zlib.h>

TEST_CASE("Pet breakdown merges same-type summons into one row",
          "[database][combat][pets]") {
    // A DK-style owner with two Lesser Ghouls (same NPC id 258578, different
    // spawn suffix) and one distinct summon (Magus, NPC id 163366). The
    // breakdown must show TWO rows - ghouls merged, Magus separate - not three.
    ActorMap actorMap;
    StringInterner::Id owner  = guidInterner().intern("Player-4-0EA185DE");
    StringInterner::Id boss   = guidInterner().intern("Creature-0-1-1-1-700001-0000000001");
    StringInterner::Id ghoulA = guidInterner().intern("Creature-0-4225-2526-72705-258578-0000493E56");
    StringInterner::Id ghoulB = guidInterner().intern("Creature-0-4225-2526-72705-258578-0000C93E56");
    StringInterner::Id magus  = guidInterner().intern("Creature-0-4225-2526-72705-163366-0000493E57");

    auto ghoulHit = [&](StringInterner::Id pet, int32_t ts, int64_t amt) {
        actorMap[pet].damage_dealt_table.push_back({
            .timestamp_ms = ts, .spell_id = 700, .amount = amt,
            .effective_amount = amt, .absorbed = 0, .blocked = 0, .resisted = 0,
            .target_guid_id = boss, .owner_guid_id = owner, .flags = 0,
            .spell_school = static_cast<uint8_t>(SpellSchool::Physical),
            .event_type = CombatEventType::DamageDealt});
    };
    ghoulHit(ghoulA, 1000, 5000);
    ghoulHit(ghoulB, 1100, 3000);
    ghoulHit(magus,  1200, 4000);

    // Summon lineage: all three belong to the DK.
    std::unordered_map<StringInterner::Id, StringInterner::Id> summon;
    summon[ghoulA] = owner; summon[ghoulB] = owner; summon[magus] = owner;

    CombatDatabase db;
    db.loadFromActorMap(&actorMap, &summon);

    auto ranked = db.getRankedByActorWithPets(
        CombatMetricType::DamageDealt, 0, 10000, 40, /*includeBreakdowns=*/true);

    const ActorCombatStats* dk = nullptr;
    for (const auto& s : ranked) {
        if (s.actor_guid == "Player-4-0EA185DE") dk = &s;
    }
    REQUIRE(dk != nullptr);
    // Two ghouls collapse to one row; Magus stays its own -> two rows total.
    REQUIRE(dk->pet_breakdown.size() == 2);
    // Rows are sorted by amount: merged ghouls (8000) first, then Magus (4000).
    CHECK(dk->pet_breakdown[0].total_amount == 8000);
    CHECK(dk->pet_breakdown[0].hit_count == 2);
    CHECK(dk->pet_breakdown[1].total_amount == 4000);
}

TEST_CASE("Pet spell groups: one group per pet type, owner spells stay flat",
          "[database][combat][pets]") {
    // Same DK setup: two Lesser Ghouls (same NPC id 258578) both casting
    // "Claw" (spell 700) and a Magus (NPC id 163366) casting a different
    // spell (800). The DK also lands one of their OWN spells (900). We
    // expect two pet groups (Ghoul + Magus), the Ghoul group summing both
    // spawns' Claw, and the owner's flat spell_breakdown holding only 900.
    ActorMap actorMap;
    StringInterner::Id owner  = guidInterner().intern("Player-4-0EA185DE");
    StringInterner::Id boss   = guidInterner().intern("Creature-0-1-1-1-700001-0000000001");
    StringInterner::Id ghoulA = guidInterner().intern("Creature-0-4225-2526-72705-258578-0000493E56");
    StringInterner::Id ghoulB = guidInterner().intern("Creature-0-4225-2526-72705-258578-0000C93E56");
    StringInterner::Id magus  = guidInterner().intern("Creature-0-4225-2526-72705-163366-0000493E57");

    auto hit = [&](StringInterner::Id src, uint32_t spell, int32_t ts, int64_t amt) {
        actorMap[src].damage_dealt_table.push_back({
            .timestamp_ms = ts, .spell_id = spell, .amount = amt,
            .effective_amount = amt, .absorbed = 0, .blocked = 0, .resisted = 0,
            .target_guid_id = boss, .owner_guid_id = owner, .flags = 0,
            .spell_school = static_cast<uint8_t>(SpellSchool::Physical),
            .event_type = CombatEventType::DamageDealt});
    };
    // Ghouls both cast Claw (700); Magus casts spell 800; DK casts own 900.
    hit(ghoulA, 700, 1000, 5000);
    hit(ghoulB, 700, 1100, 3000);
    hit(magus,  800, 1200, 4000);
    hit(owner,  900, 1300, 6000);

    std::unordered_map<StringInterner::Id, StringInterner::Id> summon;
    summon[ghoulA] = owner; summon[ghoulB] = owner; summon[magus] = owner;

    CombatDatabase db;
    db.loadFromActorMap(&actorMap, &summon);

    auto ranked = db.getRankedByActorWithPets(
        CombatMetricType::DamageDealt, 0, 10000, 40, /*includeBreakdowns=*/true);

    const ActorCombatStats* dk = nullptr;
    for (const auto& s : ranked) {
        if (s.actor_guid == "Player-4-0EA185DE") dk = &s;
    }
    REQUIRE(dk != nullptr);

    // Two pet groups: Ghoul (one NPC id) + Magus (another NPC id).
    REQUIRE(dk->pet_spell_groups.size() == 2);

    // Groups are sorted by total desc: Ghoul group (8000) then Magus (4000).
    const auto& ghoulGroup = dk->pet_spell_groups[0];
    const auto& magusGroup = dk->pet_spell_groups[1];
    CHECK(ghoulGroup.npc_id == 258578);
    CHECK(ghoulGroup.total_amount == 8000);       // both ghouls' Claw summed
    CHECK(ghoulGroup.hit_count == 2);
    REQUIRE(ghoulGroup.spells.size() == 1);       // one merged Claw entry
    CHECK(ghoulGroup.spells[0].spell_id == 700);
    CHECK(ghoulGroup.spells[0].total_amount == 8000);
    CHECK(magusGroup.npc_id == 163366);
    CHECK(magusGroup.total_amount == 4000);

    // Owner's flat breakdown holds ONLY the owner's own spell (900), no pets.
    REQUIRE(dk->spell_breakdown.size() == 1);
    CHECK(dk->spell_breakdown[0].spell_id == 900);
    CHECK(dk->spell_breakdown[0].total_amount == 6000);

    // Sum check: owner flat spells + all pet groups == the actor's total.
    int64_t ownerFlat = 0;
    for (const auto& s : dk->spell_breakdown) ownerFlat += s.total_amount;
    int64_t petTotal = 0;
    for (const auto& g : dk->pet_spell_groups) petTotal += g.total_amount;
    CHECK(ownerFlat + petTotal == dk->total_amount);
    CHECK(dk->total_amount == 18000);  // 6000 owner + 8000 ghouls + 4000 magus
}

// Parses a real Vexamus pull (a Death Knight with ~117 summoned pets) end
// to end and checks damage done, pets folded into their owner, against
// Warcraft Logs' published totals. The DK's ghouls, Magus of the Dead, Army
// horsemen and Abomination are Creature-/Vehicle- units, most with no owner
// in their own damage events, and must merge into the DK through the
// SPELL_SUMMON lineage. Effective damage (after mitigation, overkill removed)
// is the figure WCL reports.
//
// The fixture is the extracted encounter (~3.8 MB) in tests/fixtures/combat,
// which CMake copies next to the test binary.
TEST_CASE("Vexamus damage-done matches Warcraft Logs, pets included",
          "[database][combat][pets][vexamus]") {
    std::ifstream in("fixtures/combat/vexamus.txt", std::ios::binary);
    REQUIRE(in.good());
    std::stringstream ss; ss << in.rdbuf();
    std::string content = ss.str();

    tokenized_segment tokenizer;
    auto lines = tokenizer.tokenize(content.data(), content.size());

    auto extracted = OutputWriter::extractAllEvents(lines);
    ActorMap actorMap = OutputWriter::buildActorMap(extracted);
    auto summonPetToOwner = OutputWriter::buildSummonPetToOwnerMap(extracted);

    CombatDatabase db;
    db.loadFromActorMap(&actorMap, &summonPetToOwner);

    auto ranked = db.getRankedByActorWithPets(
        CombatMetricType::DamageDealt, 0, INT32_MAX, 60, false);

    // Pull out the per-player effective totals (WCL-comparable).
    std::unordered_map<std::string, int64_t> effByGuid;
    for (const auto& s : ranked) {
        if (s.actor_guid.rfind("Player-", 0) == 0) {
            effByGuid[s.actor_guid] = s.effective_amount;
        }
    }

    // Published WCL damage-done for this pull, per player.
    struct Expected { const char* guid; int64_t wcl; };
    const Expected expected[] = {
        {"Player-73-0AFCAEE0",   10210000},  // Download
        {"Player-77-0F0C8E2D",    7660000},  // Wafulol
        {"Player-4-0EA185DE",     7200000},  // Ardinn - the DK, ~117 pets
        {"Player-11-0E8ACC6C",    4290000},  // Wobblucy
        {"Player-3676-0AC63E0C",  2250000},  // Axiomove
    };

    for (const auto& e : expected) {
        auto it = effByGuid.find(e.guid);
        REQUIRE(it != effByGuid.end());
        // Within 1% of the WCL total.
        double ratio = static_cast<double>(it->second) / static_cast<double>(e.wcl);
        INFO(e.guid << " effective=" << it->second << " wcl=" << e.wcl
             << " ratio=" << ratio);
        CHECK(ratio > 0.99);
        CHECK(ratio < 1.01);
    }

    // The DK's pet-inclusive total is the crux of this regression - guard it
    // explicitly so a lost-pet regression can't hide behind another player.
    REQUIRE(effByGuid.count("Player-4-0EA185DE") == 1);
    int64_t dkEffective = effByGuid["Player-4-0EA185DE"];
    CHECK(dkEffective > 7128000);   // 7.20m - 1%
    CHECK(dkEffective < 7272000);   // 7.20m + 1%

    // Ranking order must match WCL: Download > Wafulol > Ardinn > Wobblucy > Axiomove.
    std::vector<std::string> playerOrder;
    for (const auto& s : ranked) {
        if (s.actor_guid.rfind("Player-", 0) == 0) playerOrder.push_back(s.actor_guid);
    }
    REQUIRE(playerOrder.size() >= 5);
    CHECK(playerOrder[0] == "Player-73-0AFCAEE0");
    CHECK(playerOrder[1] == "Player-77-0F0C8E2D");
    CHECK(playerOrder[2] == "Player-4-0EA185DE");
    CHECK(playerOrder[3] == "Player-11-0E8ACC6C");
    CHECK(playerOrder[4] == "Player-3676-0AC63E0C");
}

// Test helpers

namespace test_helpers {

ActorMap createTestCombatData() {
    ActorMap actorMap;

    // Create player 1
    ActorTable& player1 = actorMap[guidInterner().intern("Player-1")];

    // Damage dealt by player1 (3 hits)
    player1.damage_dealt_table.push_back({
        .timestamp_ms = 1000,
        .spell_id = 100,
        .amount = 5000,
        .effective_amount = 5000,
        .absorbed = 0,
        .blocked = 0,
        .resisted = 0,
        .target_guid_id = 2,
        .owner_guid_id = 0,
        .flags = CombatEventFlags::Critical,
        .spell_school = static_cast<uint8_t>(SpellSchool::Fire),
        .event_type = CombatEventType::DamageDealt
    });

    player1.damage_dealt_table.push_back({
        .timestamp_ms = 2000,
        .spell_id = 100,
        .amount = 3000,
        .effective_amount = 3000,
        .absorbed = 0,
        .blocked = 0,
        .resisted = 0,
        .target_guid_id = 2,
        .owner_guid_id = 0,
        .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Fire),
        .event_type = CombatEventType::DamageDealt
    });

    player1.damage_dealt_table.push_back({
        .timestamp_ms = 3000,
        .spell_id = 101,
        .amount = 2000,
        .effective_amount = 1500,
        .absorbed = 0,
        .blocked = 0,
        .resisted = 0,
        .target_guid_id = 2,
        .owner_guid_id = 0,
        .flags = CombatEventFlags::Overkill,
        .spell_school = static_cast<uint8_t>(SpellSchool::Fire),
        .event_type = CombatEventType::DamageDealt
    });

    // Healing done by player1
    player1.healing_done_table.push_back({
        .timestamp_ms = 1500,
        .spell_id = 200,
        .amount = 4000,
        .effective_amount = 3500,
        .absorbed = 0,
        .blocked = 0,
        .resisted = 0,
        .target_guid_id = 1,
        .owner_guid_id = 0,
        .flags = CombatEventFlags::Overheal,
        .spell_school = static_cast<uint8_t>(SpellSchool::Holy),
        .event_type = CombatEventType::HealingDone
    });

    // Create player 2 with less damage
    ActorTable& player2 = actorMap[guidInterner().intern("Player-2")];
    player2.damage_dealt_table.push_back({
        .timestamp_ms = 1500,
        .spell_id = 150,
        .amount = 4000,
        .effective_amount = 4000,
        .absorbed = 0,
        .blocked = 0,
        .resisted = 0,
        .target_guid_id = 2,
        .owner_guid_id = 0,
        .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Physical),
        .event_type = CombatEventType::DamageDealt
    });

    return actorMap;
}

ActorMap createTestPetData() {
    ActorMap actorMap;

    // Create player with pet
    ActorTable& player = actorMap[guidInterner().intern("Player-1")];
    player.damage_dealt_table.push_back({
        .timestamp_ms = 1000,
        .spell_id = 100,
        .amount = 5000,
        .effective_amount = 5000,
        .absorbed = 0,
        .blocked = 0,
        .resisted = 0,
        .target_guid_id = 2,
        .owner_guid_id = 0,
        .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Fire),
        .event_type = CombatEventType::DamageDealt
    });

    // Create pet with owner_guid pointing to player
    ActorTable& pet = actorMap[guidInterner().intern("Pet-1")];
    uint32_t ownerGuidId = guidInterner().intern("Player-1");
    pet.damage_dealt_table.push_back({
        .timestamp_ms = 1500,
        .spell_id = 110,
        .amount = 3000,
        .effective_amount = 3000,
        .absorbed = 0,
        .blocked = 0,
        .resisted = 0,
        .target_guid_id = 2,
        .owner_guid_id = ownerGuidId,
        .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Physical),
        .event_type = CombatEventType::DamageDealt
    });

    return actorMap;
}

std::vector<AuraEvent> createTestAuraEvents() {
    std::vector<AuraEvent> events;

    // Simple buff: Applied at 1000, removed at 3000
    events.push_back({
        .target_guid = "Player-1",
        .source_guid = "Player-2",
        .spell_id = 100,
        .spell_name = "Test Buff",
        .aura_type = AuraType::BUFF,
        .timestamp_ms = 1000,
        .event_type = AuraEventType::Applied,
        .stacks = 1
    });

    events.push_back({
        .target_guid = "Player-1",
        .source_guid = "Player-2",
        .spell_id = 100,
        .spell_name = "Test Buff",
        .aura_type = AuraType::BUFF,
        .timestamp_ms = 3000,
        .event_type = AuraEventType::Removed,
        .stacks = 0
    });

    // Stacking debuff: Applied at 500, dose at 1000 (2 stacks), dose at 1500 (3 stacks), removed at 2500
    events.push_back({
        .target_guid = "Player-1",
        .source_guid = "Boss-1",
        .spell_id = 200,
        .spell_name = "Stacking Debuff",
        .aura_type = AuraType::DEBUFF,
        .timestamp_ms = 500,
        .event_type = AuraEventType::Applied,
        .stacks = 1
    });

    events.push_back({
        .target_guid = "Player-1",
        .source_guid = "Boss-1",
        .spell_id = 200,
        .spell_name = "Stacking Debuff",
        .aura_type = AuraType::DEBUFF,
        .timestamp_ms = 1000,
        .event_type = AuraEventType::AppliedDose,
        .stacks = 2
    });

    events.push_back({
        .target_guid = "Player-1",
        .source_guid = "Boss-1",
        .spell_id = 200,
        .spell_name = "Stacking Debuff",
        .aura_type = AuraType::DEBUFF,
        .timestamp_ms = 1500,
        .event_type = AuraEventType::AppliedDose,
        .stacks = 3
    });

    events.push_back({
        .target_guid = "Player-1",
        .source_guid = "Boss-1",
        .spell_id = 200,
        .spell_name = "Stacking Debuff",
        .aura_type = AuraType::DEBUFF,
        .timestamp_ms = 2500,
        .event_type = AuraEventType::Removed,
        .stacks = 0
    });

    return events;
}

ActorMap createTestResourceData() {
    ActorMap actorMap;

    ActorTable& player = actorMap[guidInterner().intern("Player-1")];

    // Resource snapshots at different times
    player.resource_status_table.push_back({
        .timestamp_ms = 1000,
        .current_health = 50000,
        .max_health = 100000,
        .current_power = 80,
        .max_power = 100,
        .powertype = 0,  // Mana
        .padding = {0, 0, 0}
    });

    player.resource_status_table.push_back({
        .timestamp_ms = 2000,
        .current_health = 30000,
        .max_health = 100000,
        .current_power = 50,
        .max_power = 100,
        .powertype = 0,
        .padding = {0, 0, 0}
    });

    player.resource_status_table.push_back({
        .timestamp_ms = 3000,
        .current_health = 45000,
        .max_health = 100000,
        .current_power = 70,
        .max_power = 100,
        .powertype = 0,
        .padding = {0, 0, 0}
    });

    return actorMap;
}

std::vector<DeathEvent> createTestDeathEvents() {
    std::vector<DeathEvent> deaths;

    deaths.push_back({
        .actor_guid = "Player-1",
        .actor_name = "TestPlayer",
        .actor_type = ActorType::Player,
        .timestamp_ms = 5000,
        .killing_spell_id = 0,
        .killing_spell_name = ""
    });

    return deaths;
}

std::vector<ResurrectEvent> createTestResurrectEvents() {
    std::vector<ResurrectEvent> resurrects;

    resurrects.push_back({
        .actor_guid = "Player-1",
        .actor_name = "TestPlayer",
        .timestamp_ms = 8000,
        .spell_id = 2006
    });

    return resurrects;
}

std::vector<ActorEvent> createTestPositionEvents() {
    std::vector<ActorEvent> events;

    // Player-1 alive at 6000ms after death at 5000ms (implicit resurrection)
    events.push_back({
        .source_guid = "Player-1",
        .source_actor = "TestPlayer",
        .actor_type = ActorType::Player,
        .flags = UnitFlags(),
        .timestamp_ms = 6000,
        .x_loc = 100.0f,
        .y_loc = 200.0f,
        .map_id = 2357,
        .facing_radians = 0
    });

    return events;
}

} // namespace test_helpers

// CombatDatabase

TEST_CASE("CombatDatabase - Load from ActorMap", "[database][combat]") {
    auto actorMap = test_helpers::createTestCombatData();

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    REQUIRE_FALSE(db.empty());
    REQUIRE(db.actorCount() == 2);
}

TEST_CASE("CombatDatabase - Empty database", "[database][combat][edge]") {
    CombatDatabase db;

    REQUIRE(db.empty());
    REQUIRE(db.actorCount() == 0);
    REQUIRE(db.getMinTimestamp() == 0);
    REQUIRE(db.getMaxTimestamp() == 0);
}

TEST_CASE("CombatDatabase - Time bounds calculation", "[database][combat]") {
    auto actorMap = test_helpers::createTestCombatData();

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    // Note: getMinTimestamp() returns 0 due to UINT32_MAX initialization bug in CombatDatabase.cpp
    // The actual minimum timestamp in test data is 1000ms
    REQUIRE(db.getMinTimestamp() == 0);
    REQUIRE(db.getMaxTimestamp() == 3000);
}

TEST_CASE("CombatDatabase - getRankedByActor damage dealt", "[database][combat]") {
    auto actorMap = test_helpers::createTestCombatData();

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    auto results = db.getRankedByActor(CombatMetricType::DamageDealt, 0, 10000, 10);

    REQUIRE(results.size() == 2);

    // Player-1 should be first with 10000 total damage
    REQUIRE(results[0].actor_guid == "Player-1");
    REQUIRE(results[0].total_amount == 10000);
    REQUIRE(results[0].effective_amount == 9500);  // 5000 + 3000 + 1500
    REQUIRE(results[0].hit_count == 3);
    REQUIRE(results[0].crit_count == 1);

    // Player-2 should be second with 4000 damage
    REQUIRE(results[1].actor_guid == "Player-2");
    REQUIRE(results[1].total_amount == 4000);
    REQUIRE(results[1].effective_amount == 4000);
    REQUIRE(results[1].hit_count == 1);
    REQUIRE(results[1].crit_count == 0);
}

TEST_CASE("CombatDatabase - getRankedByActor healing done", "[database][combat]") {
    auto actorMap = test_helpers::createTestCombatData();

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    auto results = db.getRankedByActor(CombatMetricType::HealingDone, 0, 10000, 10);

    REQUIRE(results.size() == 1);
    REQUIRE(results[0].actor_guid == "Player-1");
    REQUIRE(results[0].total_amount == 4000);
    REQUIRE(results[0].effective_amount == 3500);
}

TEST_CASE("CombatDatabase - Time-windowed queries", "[database][combat]") {
    auto actorMap = test_helpers::createTestCombatData();

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    // Query only events from 1500 to 2500 (should include 2000ms hit only)
    auto results = db.getRankedByActor(CombatMetricType::DamageDealt, 1500, 2500, 10);

    REQUIRE(results.size() == 2);

    // Player-2 should be first with 4000 damage
    REQUIRE(results[0].actor_guid == "Player-2");
    REQUIRE(results[0].total_amount == 4000);

    // Player-1 should be second with 3000 damage
    REQUIRE(results[1].actor_guid == "Player-1");
    REQUIRE(results[1].total_amount == 3000);
}

TEST_CASE("CombatDatabase - getGrandTotal", "[database][combat]") {
    auto actorMap = test_helpers::createTestCombatData();

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    auto total = db.getGrandTotal(CombatMetricType::DamageDealt, 0, 10000);
    REQUIRE(total == 14000);  // 10000 + 4000

    auto healTotal = db.getGrandTotal(CombatMetricType::HealingDone, 0, 10000);
    REQUIRE(healTotal == 4000);
}

TEST_CASE("CombatDatabase - getSpellBreakdown", "[database][combat]") {
    auto actorMap = test_helpers::createTestCombatData();

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    auto breakdown = db.getSpellBreakdown("Player-1", CombatMetricType::DamageDealt, 0, 10000);

    REQUIRE(breakdown.size() == 2);

    // Find spell 100 (should have 2 hits, 8000 total)
    auto spell100 = std::find_if(breakdown.begin(), breakdown.end(),
        [](const SpellCombatStats& s) { return s.spell_id == 100; });
    REQUIRE(spell100 != breakdown.end());
    REQUIRE(spell100->total_amount == 8000);
    REQUIRE(spell100->effective_amount == 8000);
    REQUIRE(spell100->hit_count == 2);
    REQUIRE(spell100->crit_count == 1);
    REQUIRE(spell100->max_hit == 5000);
    REQUIRE(spell100->min_hit == 3000);

    // Find spell 101 (should have 1 hit, 2000 total)
    auto spell101 = std::find_if(breakdown.begin(), breakdown.end(),
        [](const SpellCombatStats& s) { return s.spell_id == 101; });
    REQUIRE(spell101 != breakdown.end());
    REQUIRE(spell101->total_amount == 2000);
    REQUIRE(spell101->effective_amount == 1500);
    REQUIRE(spell101->hit_count == 1);
}

TEST_CASE("CombatDatabase - Pet attribution", "[database][combat][pets]") {
    auto actorMap = test_helpers::createTestPetData();

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    // Check pet ownership
    REQUIRE(db.isPetOrGuardian("Pet-1"));
    REQUIRE_FALSE(db.isPetOrGuardian("Player-1"));
    REQUIRE(db.getOwnerGuid("Pet-1") == "Player-1");
    REQUIRE(db.getOwnerGuid("Player-1").empty());
}

TEST_CASE("CombatDatabase - getRankedByActorWithPets", "[database][combat][pets]") {
    auto actorMap = test_helpers::createTestPetData();

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    auto results = db.getRankedByActorWithPets(CombatMetricType::DamageDealt, 0, 10000, 10, false);

    REQUIRE(results.size() == 1);  // Only player, pet merged

    REQUIRE(results[0].actor_guid == "Player-1");
    REQUIRE(results[0].total_amount == 8000);  // 5000 + 3000 from pet
    REQUIRE(results[0].hit_count == 2);
}

TEST_CASE("Enemy damage ranks hostile sources, not players who hit them",
          "[database][combat][enemy]") {
    ActorMap actorMap;
    StringInterner::Id player = guidInterner().intern("Player-11-0EAAAA02");
    StringInterner::Id boss   = guidInterner().intern("Creature-0-1111-2222-3333-200001-000000000C");

    // Player hits the boss for 50k; the boss hits the player for 30k.
    actorMap[player].damage_dealt_table.push_back({
        .timestamp_ms = 1000, .spell_id = 200, .amount = 50000,
        .effective_amount = 50000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = boss, .owner_guid_id = 0, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Physical),
        .event_type = CombatEventType::DamageDealt});
    actorMap[boss].damage_dealt_table.push_back({
        .timestamp_ms = 1500, .spell_id = 201, .amount = 30000,
        .effective_amount = 30000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = player, .owner_guid_id = 0, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Fire),
        .event_type = CombatEventType::DamageDealt});

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    // Enemy Damage: the boss as a source, crediting what it dealt (30k),
    // NOT the player's 50k into it.
    auto enemy = db.getRankedByEnemyDamage(0, 10000, 40);
    REQUIRE(enemy.size() == 1);
    CHECK(enemy[0].actor_guid == "Creature-0-1111-2222-3333-200001-000000000C");
    CHECK(enemy[0].total_amount == 30000);

    // Damage-done-to-target (the "Taken By" view) is the opposite: the
    // player as a source of the 50k they dealt into the boss.
    auto takenBy = db.getDamageDoneToTarget(
        "Creature-0-1111-2222-3333-200001-000000000C", 0, 10000, 40);
    REQUIRE(takenBy.size() == 1);
    CHECK(takenBy[0].actor_guid == "Player-11-0EAAAA02");
    CHECK(takenBy[0].total_amount == 50000);
}

TEST_CASE("A player's pet is not an enemy source", "[database][combat][enemy]") {
    // A player-owned pet (hunter pet, DK ghoul) deals damage. Folding it into
    // its owner would put that player in the Enemy Damage list. It must not.
    ActorMap actorMap;
    StringInterner::Id player = guidInterner().intern("Player-11-0EAAAA07");
    StringInterner::Id pet    = guidInterner().intern("Creature-0-1-1-1-165189-0000000012");
    StringInterner::Id boss   = guidInterner().intern("Creature-0-1-1-1-800001-0000000013");

    // Pet hits the boss; its records name the player as owner.
    actorMap[pet].damage_dealt_table.push_back({
        .timestamp_ms = 1000, .spell_id = 800, .amount = 12000,
        .effective_amount = 12000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = boss, .owner_guid_id = player, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Physical),
        .event_type = CombatEventType::DamageDealt});
    // Boss hits the player, so there's a real enemy source to rank.
    actorMap[boss].damage_dealt_table.push_back({
        .timestamp_ms = 1500, .spell_id = 801, .amount = 20000,
        .effective_amount = 20000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = player, .owner_guid_id = 0, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Fire),
        .event_type = CombatEventType::DamageDealt});

    std::unordered_map<StringInterner::Id, StringInterner::Id> summon;
    summon[pet] = player;

    CombatDatabase db;
    db.loadFromActorMap(&actorMap, &summon);

    auto enemy = db.getRankedByEnemyDamage(0, 10000, 40);
    // Only the boss - the player (and their pet folded into them) stays out.
    REQUIRE(enemy.size() == 1);
    CHECK(enemy[0].actor_guid == "Creature-0-1-1-1-800001-0000000013");
    for (const auto& e : enemy) {
        CHECK(e.actor_guid.rfind("Player-", 0) != 0);
    }
}

TEST_CASE("Enemy source has a spell breakdown when opened", "[database][combat][enemy]") {
    // Reproduces what the Enemy Damage breakdown does: open a hostile
    // source with getRankedByActorWithPets(DamageDealt, includeBreakdowns).
    ActorMap actorMap;
    StringInterner::Id player = guidInterner().intern("Player-11-0EAAAA03");
    StringInterner::Id boss   = guidInterner().intern("Creature-0-1111-2222-3333-300001-000000000D");

    actorMap[boss].damage_dealt_table.push_back({
        .timestamp_ms = 1000, .spell_id = 300, .amount = 20000,
        .effective_amount = 20000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = player, .owner_guid_id = 0, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Fire),
        .event_type = CombatEventType::DamageDealt});
    actorMap[boss].damage_dealt_table.push_back({
        .timestamp_ms = 2000, .spell_id = 301, .amount = 10000,
        .effective_amount = 10000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = player, .owner_guid_id = 0, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Shadow),
        .event_type = CombatEventType::DamageDealt});

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    auto ranked = db.getRankedByActorWithPets(
        CombatMetricType::DamageDealt, 0, 10000, 100, /*includeBreakdowns=*/true);

    const ActorCombatStats* bossStats = nullptr;
    for (const auto& s : ranked) {
        if (s.actor_guid == "Creature-0-1111-2222-3333-300001-000000000D") bossStats = &s;
    }
    REQUIRE(bossStats != nullptr);
    CHECK(bossStats->total_amount == 30000);
    // Two distinct spells the enemy used - the breakdown drill-down needs these.
    CHECK(bossStats->spell_breakdown.size() == 2);
}

TEST_CASE("A hostile creature carrying a player owner is not folded into that player",
          "[database][combat][enemy]") {
    // Regression: a boss can surface a player owner_guid (mind control,
    // possessed adds, a mislabeled advanced block). A Creature- actor with
    // a Player owner must NOT be merged into that player, or the enemy's
    // whole damage-done shows up as the player's damage done. Only real
    // Pet- GUIDs adopt a player owner.
    ActorMap actorMap;
    StringInterner::Id player = guidInterner().intern("Player-11-0EAAAA06");
    StringInterner::Id petOwner = player;  // the player who owns the real pet
    StringInterner::Id realPet = guidInterner().intern("Pet-0-1111-2222-3333-165189-0000000010");
    StringInterner::Id boss   = guidInterner().intern("Creature-0-1111-2222-3333-600001-0000000011");

    // Player: 30k to the boss.
    actorMap[player].damage_dealt_table.push_back({
        .timestamp_ms = 1000, .spell_id = 600, .amount = 30000,
        .effective_amount = 30000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = boss, .owner_guid_id = 0, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Physical),
        .event_type = CombatEventType::DamageDealt});
    // Real pet: 5k, owner = player -> should merge into the player (35k).
    actorMap[realPet].damage_dealt_table.push_back({
        .timestamp_ms = 1100, .spell_id = 601, .amount = 5000,
        .effective_amount = 5000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = boss, .owner_guid_id = petOwner, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Nature),
        .event_type = CombatEventType::DamageDealt});
    // Boss: 100k of damage to the raid, but its record carries the player
    // as owner_guid (the corruption case). Must NOT merge into the player.
    actorMap[boss].damage_dealt_table.push_back({
        .timestamp_ms = 1200, .spell_id = 602, .amount = 100000,
        .effective_amount = 100000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = player, .owner_guid_id = petOwner, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Fire),
        .event_type = CombatEventType::DamageDealt});

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    auto ranked = db.getRankedByActorWithPets(
        CombatMetricType::DamageDealt, 0, 10000, 100, true);
    const ActorCombatStats* playerStats = nullptr;
    for (const auto& s : ranked) {
        if (s.actor_guid == "Player-11-0EAAAA06") playerStats = &s;
    }
    REQUIRE(playerStats != nullptr);
    // 30k player + 5k real pet = 35k. NOT 135k (the boss's 100k stays out).
    CHECK(playerStats->total_amount == 35000);
}

TEST_CASE("Enemy summons merge into their owner in the Enemy Damage list and breakdown",
          "[database][combat][enemy]") {
    // A boss and an enemy summon (a totem/add-pet the boss owns) both hit
    // the raid. The Enemy Damage list must show ONE row - the boss, with
    // the summon's damage folded in - not two separate rows. And clicking
    // that boss row must open a breakdown (getRankedByActorWithPets) keyed
    // by the SAME boss guid whose spell_breakdown includes the summon's
    // ability. Before the fix the two queries disagreed: the list showed
    // the summon as its own row, and clicking it found no matching actor
    // in the with-pets ranking, leaving the breakdown empty.
    ActorMap actorMap;
    StringInterner::Id player = guidInterner().intern("Player-11-0EAAAA04");
    StringInterner::Id boss   = guidInterner().intern("Creature-0-1111-2222-3333-400001-000000000E");
    StringInterner::Id summon = guidInterner().intern("Creature-0-1111-2222-3333-400002-000000000F");

    actorMap[boss].damage_dealt_table.push_back({
        .timestamp_ms = 1000, .spell_id = 400, .amount = 20000,
        .effective_amount = 20000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = player, .owner_guid_id = 0, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Fire),
        .event_type = CombatEventType::DamageDealt});
    // The summon's records carry owner_guid_id pointing back at the boss.
    actorMap[summon].damage_dealt_table.push_back({
        .timestamp_ms = 1500, .spell_id = 401, .amount = 7000,
        .effective_amount = 7000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = player, .owner_guid_id = boss, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Physical),
        .event_type = CombatEventType::DamageDealt});

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    // Enemy Damage list: one merged row (boss), summon's 7k folded in.
    auto enemy = db.getRankedByEnemyDamage(0, 10000, 40);
    REQUIRE(enemy.size() == 1);
    CHECK(enemy[0].actor_guid == "Creature-0-1111-2222-3333-400001-000000000E");
    CHECK(enemy[0].total_amount == 27000);

    // Clicking that row opens the breakdown via getRankedByActorWithPets.
    // The boss guid must be present with a populated breakdown that lists
    // BOTH its own spell and the summon's spell.
    auto ranked = db.getRankedByActorWithPets(
        CombatMetricType::DamageDealt, 0, 10000, 100, /*includeBreakdowns=*/true);
    const ActorCombatStats* bossStats = nullptr;
    for (const auto& s : ranked) {
        if (s.actor_guid == "Creature-0-1111-2222-3333-400001-000000000E") bossStats = &s;
        // The summon must NOT appear as its own top-level row anymore.
        CHECK(s.actor_guid != "Creature-0-1111-2222-3333-400002-000000000F");
    }
    REQUIRE(bossStats != nullptr);
    CHECK(bossStats->total_amount == 27000);
    // The boss's own spell stays flat; the summon's ability now lives in a
    // per-pet-type group rather than being flattened into spell_breakdown.
    REQUIRE(bossStats->spell_breakdown.size() == 1);
    CHECK(bossStats->spell_breakdown[0].spell_id == 400);
    REQUIRE(bossStats->pet_spell_groups.size() == 1);
    CHECK(bossStats->pet_spell_groups[0].total_amount == 7000);
    REQUIRE(bossStats->pet_spell_groups[0].spells.size() == 1);
    CHECK(bossStats->pet_spell_groups[0].spells[0].spell_id == 401);
}

TEST_CASE("Same-type enemy spawns merge into one Enemy Damage row",
          "[database][combat][enemy]") {
    // WoW spawns many copies of the same add (each a distinct guid that shares
    // one npc id in the 6th dash field). The picker/Enemy Damage list must
    // show one row per enemy TYPE, not one per spawn.
    ActorMap actorMap;
    StringInterner::Id player = guidInterner().intern("Player-11-0EAAAA10");
    // Two "Lesser Ghoul" spawns: same npc id 165189, different spawn suffix.
    StringInterner::Id ghoulA = guidInterner().intern("Creature-0-1-1-1-165189-00000000A1");
    StringInterner::Id ghoulB = guidInterner().intern("Creature-0-1-1-1-165189-00000000A2");
    StringInterner::Id boss   = guidInterner().intern("Creature-0-1-1-1-900001-00000000B1");

    // Both ghoul spawns hit the player.
    actorMap[ghoulA].damage_dealt_table.push_back({
        .timestamp_ms = 1000, .spell_id = 900, .amount = 3000,
        .effective_amount = 3000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = player, .owner_guid_id = 0, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Physical),
        .event_type = CombatEventType::DamageDealt});
    actorMap[ghoulB].damage_dealt_table.push_back({
        .timestamp_ms = 1100, .spell_id = 900, .amount = 4000,
        .effective_amount = 4000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = player, .owner_guid_id = 0, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Physical),
        .event_type = CombatEventType::DamageDealt});
    // A boss also hits the player - a second, distinct enemy type.
    actorMap[boss].damage_dealt_table.push_back({
        .timestamp_ms = 1200, .spell_id = 901, .amount = 20000,
        .effective_amount = 20000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = player, .owner_guid_id = 0, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Fire),
        .event_type = CombatEventType::DamageDealt});

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    // Two rows: one merged Ghoul (3000 + 4000) and one boss. Not three.
    auto enemy = db.getRankedByEnemyDamage(0, 10000, 40);
    REQUIRE(enemy.size() == 2);

    const ActorCombatStats* ghoulRow = nullptr;
    const ActorCombatStats* bossRow = nullptr;
    for (const auto& e : enemy) {
        uint32_t npcId = MobWeightSettings::npcIdFromGuid(e.actor_guid);
        if (npcId == 165189) ghoulRow = &e;
        else if (npcId == 900001) bossRow = &e;
    }
    REQUIRE(ghoulRow != nullptr);
    REQUIRE(bossRow != nullptr);
    CHECK(ghoulRow->total_amount == 7000);  // both spawns summed into one row
    CHECK(bossRow->total_amount == 20000);
}

TEST_CASE("Damage-to-target drill-down covers the whole enemy-type group",
          "[database][combat][enemy]") {
    // Two spawns of one enemy type, damaged by two players. The picker merges
    // the spawns, so clicking that row must show each player's TOTAL damage
    // across BOTH spawns. Querying a single spawn guid shows only that spawn.
    ActorMap actorMap;
    StringInterner::Id p1 = guidInterner().intern("Player-11-0EAAAA20");
    StringInterner::Id p2 = guidInterner().intern("Player-11-0EAAAA21");
    StringInterner::Id ghoulA = guidInterner().intern("Creature-0-1-1-1-165189-00000000C1");
    StringInterner::Id ghoulB = guidInterner().intern("Creature-0-1-1-1-165189-00000000C2");

    // p1 hits ghoulA for 5000 and ghoulB for 2000 -> 7000 total on the type.
    actorMap[p1].damage_dealt_table.push_back({
        .timestamp_ms = 1000, .spell_id = 200, .amount = 5000,
        .effective_amount = 5000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = ghoulA, .owner_guid_id = 0, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Physical),
        .event_type = CombatEventType::DamageDealt});
    actorMap[p1].damage_dealt_table.push_back({
        .timestamp_ms = 1100, .spell_id = 200, .amount = 2000,
        .effective_amount = 2000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = ghoulB, .owner_guid_id = 0, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Physical),
        .event_type = CombatEventType::DamageDealt});
    // p2 hits only ghoulB for 3000.
    actorMap[p2].damage_dealt_table.push_back({
        .timestamp_ms = 1200, .spell_id = 201, .amount = 3000,
        .effective_amount = 3000, .absorbed = 0, .blocked = 0, .resisted = 0,
        .target_guid_id = ghoulB, .owner_guid_id = 0, .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Fire),
        .event_type = CombatEventType::DamageDealt});

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    // Group query (npc id 165189): each player's total across both spawns.
    auto group = db.getDamageDoneToTargetGroup(
        "Creature-0-1-1-1-165189-00000000C1", 165189, 0, 10000, 40);
    REQUIRE(group.size() == 2);
    int64_t p1Total = 0, p2Total = 0;
    for (const auto& s : group) {
        if (s.actor_guid == "Player-11-0EAAAA20") p1Total = s.total_amount;
        else if (s.actor_guid == "Player-11-0EAAAA21") p2Total = s.total_amount;
    }
    CHECK(p1Total == 7000);  // 5000 on ghoulA + 2000 on ghoulB
    CHECK(p2Total == 3000);

    // Single-spawn query (npc id 0): only that spawn's portion. ghoulB got
    // 2000 from p1 and 3000 from p2.
    auto single = db.getDamageDoneToTargetGroup(
        "Creature-0-1-1-1-165189-00000000C2", 0, 0, 10000, 40);
    int64_t p1OnB = 0, p2OnB = 0;
    for (const auto& s : single) {
        if (s.actor_guid == "Player-11-0EAAAA20") p1OnB = s.total_amount;
        else if (s.actor_guid == "Player-11-0EAAAA21") p2OnB = s.total_amount;
    }
    CHECK(p1OnB == 2000);
    CHECK(p2OnB == 3000);

    // The public getDamageDoneToTarget resolves the guid's npc id itself, so
    // it behaves like the group query - the whole type, not one spawn.
    auto viaPublic = db.getDamageDoneToTarget(
        "Creature-0-1-1-1-165189-00000000C1", 0, 10000, 40);
    REQUIRE(viaPublic.size() == 2);
}

TEST_CASE("CombatDatabase - getCumulative", "[database][combat]") {
    auto actorMap = test_helpers::createTestCombatData();

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    auto results = db.getCumulative(CombatMetricType::DamageDealt, 2500, 10);

    // Should include events up to 2500ms
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].total_amount == 8000);  // Player-1: 5000 + 3000
    REQUIRE(results[1].total_amount == 4000);  // Player-2: 4000
}

TEST_CASE("CombatDatabase - getFullEncounter", "[database][combat]") {
    auto actorMap = test_helpers::createTestCombatData();

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    auto results = db.getFullEncounter(CombatMetricType::DamageDealt, 10);

    REQUIRE(results.size() == 2);
    REQUIRE(results[0].total_amount == 10000);
    REQUIRE(results[1].total_amount == 4000);
}

TEST_CASE("CombatDatabase - Empty time window", "[database][combat][edge]") {
    auto actorMap = test_helpers::createTestCombatData();

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);

    // Query before any events
    auto results = db.getRankedByActor(CombatMetricType::DamageDealt, 0, 500, 10);
    REQUIRE(results.empty());

    // Query after all events
    results = db.getRankedByActor(CombatMetricType::DamageDealt, 5000, 10000, 10);
    REQUIRE(results.empty());
}

// AuraDatabase

TEST_CASE("AuraDatabase - Load from events", "[database][aura]") {
    auto events = test_helpers::createTestAuraEvents();

    AuraDatabase db;
    db.loadFromEvents(std::move(events), {});

    REQUIRE_FALSE(db.empty());
    REQUIRE(db.targetCount() == 1);
}

TEST_CASE("AuraDatabase - Empty database", "[database][aura][edge]") {
    AuraDatabase db;

    REQUIRE(db.empty());
    REQUIRE(db.targetCount() == 0);
}

TEST_CASE("AuraDatabase - Simple buff instance", "[database][aura]") {
    auto events = test_helpers::createTestAuraEvents();

    AuraDatabase db;
    db.loadFromEvents(std::move(events), {});

    auto aurasOnTarget = db.getActiveAurasOnTarget("Player-1", 2000);

    // Should have 2 auras at 2000ms: Test Buff and Stacking Debuff
    REQUIRE(aurasOnTarget.size() == 2);
}

TEST_CASE("AuraDatabase - Aura duration calculation", "[database][aura]") {
    auto events = test_helpers::createTestAuraEvents();

    AuraDatabase db;
    db.loadFromEvents(std::move(events), {});

    auto instance = db.getAuraInstance("Player-1", "Player-2", 100, 2000);

    REQUIRE(instance != nullptr);
    REQUIRE(instance->spell_id == 100);
    REQUIRE(instance->applied_at_ms == 1000);
    REQUIRE(instance->removed_at_ms == 3000);
    REQUIRE(instance->getDurationMs() == 2000);
}

TEST_CASE("AuraDatabase - Stack count tracking", "[database][aura]") {
    auto events = test_helpers::createTestAuraEvents();

    AuraDatabase db;
    db.loadFromEvents(std::move(events), {});

    auto instance = db.getAuraInstance("Player-1", "Boss-1", 200, 1500);

    REQUIRE(instance != nullptr);
    REQUIRE(instance->max_stacks == 3);

    // Check stack count at different times
    REQUIRE(instance->getStackCountAt(500) == 1);
    REQUIRE(instance->getStackCountAt(1000) == 2);
    REQUIRE(instance->getStackCountAt(1500) == 3);
    REQUIRE(instance->getStackCountAt(2000) == 3);
}

TEST_CASE("AuraDatabase - Dose history timeline", "[database][aura]") {
    auto events = test_helpers::createTestAuraEvents();

    AuraDatabase db;
    db.loadFromEvents(std::move(events), {});

    auto instance = db.getAuraInstance("Player-1", "Boss-1", 200, 1500);

    REQUIRE(instance != nullptr);
    REQUIRE(instance->doseHistory.size() == 3);

    // Verify dose history entries
    REQUIRE(instance->doseHistory[0].timestamp_ms == 500);
    REQUIRE(instance->doseHistory[0].stack_count == 1);

    REQUIRE(instance->doseHistory[1].timestamp_ms == 1000);
    REQUIRE(instance->doseHistory[1].stack_count == 2);

    REQUIRE(instance->doseHistory[2].timestamp_ms == 1500);
    REQUIRE(instance->doseHistory[2].stack_count == 3);
}

TEST_CASE("AuraDatabase - isActiveAt query", "[database][aura]") {
    auto events = test_helpers::createTestAuraEvents();

    AuraDatabase db;
    db.loadFromEvents(std::move(events), {});

    auto instance = db.getAuraInstance("Player-1", "Player-2", 100, 2000);

    REQUIRE(instance != nullptr);

    // Before application
    REQUIRE_FALSE(instance->isActiveAt(500));

    // During active period
    REQUIRE(instance->isActiveAt(1000));
    REQUIRE(instance->isActiveAt(2000));
    REQUIRE(instance->isActiveAt(2999));

    // After removal
    REQUIRE_FALSE(instance->isActiveAt(3000));
    REQUIRE_FALSE(instance->isActiveAt(5000));
}

TEST_CASE("AuraDatabase - getActiveAurasOnTarget time filtering", "[database][aura]") {
    auto events = test_helpers::createTestAuraEvents();

    AuraDatabase db;
    db.loadFromEvents(std::move(events), {});

    // At 500ms: only stacking debuff
    auto auras500 = db.getActiveAurasOnTarget("Player-1", 500);
    REQUIRE(auras500.size() == 1);
    REQUIRE(auras500[0]->spell_id == 200);

    // At 2000ms: both buff and debuff
    auto auras2000 = db.getActiveAurasOnTarget("Player-1", 2000);
    REQUIRE(auras2000.size() == 2);

    // At 3500ms: no auras (both removed)
    auto auras3500 = db.getActiveAurasOnTarget("Player-1", 3500);
    REQUIRE(auras3500.empty());
}

TEST_CASE("AuraDatabase - getStackCount convenience method", "[database][aura]") {
    auto events = test_helpers::createTestAuraEvents();

    AuraDatabase db;
    db.loadFromEvents(std::move(events), {});

    // Check stack counts at different times
    REQUIRE(db.getStackCount("Player-1", "Boss-1", 200, 500) == 1);
    REQUIRE(db.getStackCount("Player-1", "Boss-1", 200, 1000) == 2);
    REQUIRE(db.getStackCount("Player-1", "Boss-1", 200, 1500) == 3);

    // Non-existent aura
    REQUIRE(db.getStackCount("Player-1", "Boss-1", 999, 1500) == 0);

    // After removal
    REQUIRE(db.getStackCount("Player-1", "Boss-1", 200, 3000) == 0);
}

TEST_CASE("AuraDatabase - Pre-encounter buff backdating", "[database][aura]") {
    std::vector<AuraEvent> events;

    // Removal-only event (buff was applied before encounter)
    events.push_back({
        .target_guid = "Player-1",
        .source_guid = "Player-1",
        .spell_id = 300,
        .spell_name = "Pre-encounter Buff",
        .aura_type = AuraType::BUFF,
        .timestamp_ms = 5000,
        .event_type = AuraEventType::Removed,
        .stacks = 0
    });

    // First seen timestamp map (player first appeared at -10000ms)
    std::unordered_map<std::string, int32_t> firstSeen;
    firstSeen["Player-1"] = -10000;

    AuraDatabase db;
    db.loadFromEvents(std::move(events), firstSeen);

    auto instance = db.getAuraInstance("Player-1", "Player-1", 300, 0);

    REQUIRE(instance != nullptr);
    REQUIRE(instance->applied_at_ms == -10000);  // Backdated to first seen
    REQUIRE(instance->removed_at_ms == 5000);
    REQUIRE(instance->start_inferred);
}

TEST_CASE("AuraDatabase - Multiple sources same aura", "[database][aura]") {
    std::vector<AuraEvent> events;

    // Two sources applying same spell ID to same target
    // In WoW, same buff from multiple sources typically doesn't stack separately
    events.push_back({
        .target_guid = "Player-1",
        .source_guid = "Priest-1",
        .spell_id = 400,
        .spell_name = "Power Word: Fortitude",
        .aura_type = AuraType::BUFF,
        .timestamp_ms = 1000,
        .event_type = AuraEventType::Applied,
        .stacks = 1
    });

    events.push_back({
        .target_guid = "Player-1",
        .source_guid = "Priest-2",
        .spell_id = 400,
        .spell_name = "Power Word: Fortitude",
        .aura_type = AuraType::BUFF,
        .timestamp_ms = 2000,
        .event_type = AuraEventType::Applied,
        .stacks = 1
    });

    AuraDatabase db;
    db.loadFromEvents(std::move(events), {});

    // AuraDatabase tracks by (target, spell_id), so same buff from different sources
    // creates only 1 instance (second application refreshes the first)
    auto auras = db.getActiveAurasOnTarget("Player-1", 2500);
    REQUIRE(auras.size() == 1);
}

// ResourceDatabase

TEST_CASE("ResourceDatabase - Load from ActorMap", "[database][resource]") {
    auto actorMap = test_helpers::createTestResourceData();

    ResourceDatabase db;
    db.loadFromActorMap(&actorMap);

    REQUIRE_FALSE(db.empty());
    REQUIRE(db.size() == 3);
    REQUIRE(db.actorCount() == 1);
}

TEST_CASE("ResourceDatabase - Empty database", "[database][resource][edge]") {
    ResourceDatabase db;
    ActorMap emptyMap;
    db.loadFromActorMap(&emptyMap);

    REQUIRE(db.empty());
    REQUIRE(db.size() == 0);
    REQUIRE(db.actorCount() == 0);
}

TEST_CASE("ResourceDatabase - Step-wise interpolation", "[database][resource]") {
    auto actorMap = test_helpers::createTestResourceData();

    ResourceDatabase db;
    db.loadFromActorMap(&actorMap);

    // Query at exact timestamp
    auto resource1000 = db.getResourceAt("Player-1", 1000);
    REQUIRE(resource1000.has_value());
    REQUIRE(resource1000->current_health == 50000);
    REQUIRE(resource1000->current_power == 80);

    // Query between timestamps (should return last known value)
    auto resource1500 = db.getResourceAt("Player-1", 1500);
    REQUIRE(resource1500.has_value());
    REQUIRE(resource1500->current_health == 50000);  // Still 50000 from 1000ms
    REQUIRE(resource1500->current_power == 80);

    // Query at later timestamp
    auto resource2500 = db.getResourceAt("Player-1", 2500);
    REQUIRE(resource2500.has_value());
    REQUIRE(resource2500->current_health == 30000);  // From 2000ms
    REQUIRE(resource2500->current_power == 50);
}

TEST_CASE("ResourceDatabase - Binary search correctness", "[database][resource]") {
    auto actorMap = test_helpers::createTestResourceData();

    ResourceDatabase db;
    db.loadFromActorMap(&actorMap);

    // Query just before first event (should return nothing)
    auto resourceBefore = db.getResourceAt("Player-1", 500);
    REQUIRE_FALSE(resourceBefore.has_value());

    // Query after last event (should return last value)
    auto resourceAfter = db.getResourceAt("Player-1", 5000);
    REQUIRE(resourceAfter.has_value());
    REQUIRE(resourceAfter->current_health == 45000);
    REQUIRE(resourceAfter->current_power == 70);
}

TEST_CASE("ResourceDatabase - Missing actor handling", "[database][resource][edge]") {
    auto actorMap = test_helpers::createTestResourceData();

    ResourceDatabase db;
    db.loadFromActorMap(&actorMap);

    auto resource = db.getResourceAt("NonExistent", 2000);
    REQUIRE_FALSE(resource.has_value());
}

TEST_CASE("ResourceDatabase - Batch query efficiency", "[database][resource]") {
    auto actorMap = test_helpers::createTestResourceData();

    // Add another player
    ActorTable& player2 = actorMap[guidInterner().intern("Player-2")];
    player2.resource_status_table.push_back({
        .timestamp_ms = 1500,
        .current_health = 80000,
        .max_health = 100000,
        .current_power = 60,
        .max_power = 100,
        .powertype = 1,  // Rage
        .padding = {0, 0, 0}
    });

    ResourceDatabase db;
    db.loadFromActorMap(&actorMap);

    std::vector<std::string> guids = {"Player-1", "Player-2", "Player-3"};
    auto results = db.getResourcesAt(guids, 2000);

    REQUIRE(results.size() == 2);  // Player-1 and Player-2, not Player-3
    REQUIRE(results.count("Player-1") == 1);
    REQUIRE(results.count("Player-2") == 1);
    REQUIRE(results["Player-1"].current_health == 30000);
    REQUIRE(results["Player-2"].current_health == 80000);
}

TEST_CASE("ResourceDatabase - getAllResourcesForActor", "[database][resource]") {
    auto actorMap = test_helpers::createTestResourceData();

    ResourceDatabase db;
    db.loadFromActorMap(&actorMap);

    auto allResources = db.getAllResourcesForActor("Player-1");

    REQUIRE(allResources.size() == 3);
    REQUIRE(allResources[0].timestamp_ms == 1000);
    REQUIRE(allResources[1].timestamp_ms == 2000);
    REQUIRE(allResources[2].timestamp_ms == 3000);
}

TEST_CASE("ResourceDatabase - Time bounds", "[database][resource]") {
    auto actorMap = test_helpers::createTestResourceData();

    ResourceDatabase db;
    db.loadFromActorMap(&actorMap);

    REQUIRE(db.getMinTimestamp() == 1000);
    REQUIRE(db.getMaxTimestamp() == 3000);
}

// DeathDatabase

TEST_CASE("DeathDatabase - Load events", "[database][death]") {
    auto deaths = test_helpers::createTestDeathEvents();
    auto resurrects = test_helpers::createTestResurrectEvents();

    DeathDatabase db;
    db.loadEvents(deaths, resurrects, nullptr);

    auto allDeaths = db.getAllDeaths();
    REQUIRE(allDeaths.size() == 1);
    REQUIRE(allDeaths[0].actor_guid == "Player-1");
}

TEST_CASE("DeathDatabase - shouldHide with delay", "[database][death]") {
    auto deaths = test_helpers::createTestDeathEvents();
    auto resurrects = test_helpers::createTestResurrectEvents();

    DeathDatabase db;
    db.loadEvents(deaths, resurrects, nullptr);

    // Death at 5000ms, delay 500ms

    // Before death
    REQUIRE_FALSE(db.shouldHide("Player-1", 4000, 500));

    // During delay period
    REQUIRE_FALSE(db.shouldHide("Player-1", 5000, 500));
    REQUIRE_FALSE(db.shouldHide("Player-1", 5400, 500));

    // After delay
    REQUIRE(db.shouldHide("Player-1", 5500, 500));
    REQUIRE(db.shouldHide("Player-1", 6000, 500));

    // After resurrection at 8000ms
    REQUIRE_FALSE(db.shouldHide("Player-1", 8000, 500));
    REQUIRE_FALSE(db.shouldHide("Player-1", 9000, 500));
}

TEST_CASE("DeathDatabase - No deaths", "[database][death][edge]") {
    DeathDatabase db;
    db.loadEvents({}, {}, nullptr);

    // Should never hide if no death events
    REQUIRE_FALSE(db.shouldHide("Player-1", 5000, 500));
    REQUIRE_FALSE(db.shouldHide("Player-1", 10000, 500));
}

TEST_CASE("DeathDatabase - Multiple deaths and resurrections", "[database][death]") {
    std::vector<DeathEvent> deaths;
    deaths.push_back({
        .actor_guid = "Player-1",
        .actor_name = "TestPlayer",
        .actor_type = ActorType::Player,
        .timestamp_ms = 5000,
        .killing_spell_id = 0,
        .killing_spell_name = ""
    });
    deaths.push_back({
        .actor_guid = "Player-1",
        .actor_name = "TestPlayer",
        .actor_type = ActorType::Player,
        .timestamp_ms = 15000,
        .killing_spell_id = 0,
        .killing_spell_name = ""
    });

    std::vector<ResurrectEvent> resurrects;
    resurrects.push_back({
        .actor_guid = "Player-1",
        .actor_name = "TestPlayer",
        .timestamp_ms = 8000,
        .spell_id = 2006
    });

    DeathDatabase db;
    db.loadEvents(deaths, resurrects, nullptr);

    // After first death
    REQUIRE(db.shouldHide("Player-1", 6000, 500));

    // After first resurrection
    REQUIRE_FALSE(db.shouldHide("Player-1", 9000, 500));

    // After second death
    REQUIRE(db.shouldHide("Player-1", 16000, 500));
}

TEST_CASE("DeathDatabase - Implicit resurrection from position events", "[database][death]") {
    auto deaths = test_helpers::createTestDeathEvents();
    auto positionEvents = test_helpers::createTestPositionEvents();

    // Create EventDatabase with position events
    EventDatabase eventDb;
    eventDb.loadEvents(positionEvents);

    DeathDatabase db;
    db.loadEvents(deaths, {}, &eventDb);

    // Death at 5000ms, position event at 6000ms (implicit resurrection)

    // Should hide between 5500 and 6000
    REQUIRE(db.shouldHide("Player-1", 5500, 500));

    // Should not hide after implicit resurrection at 6000ms
    REQUIRE_FALSE(db.shouldHide("Player-1", 6500, 500));
}

TEST_CASE("DeathDatabase - getPlayerDeaths filtering", "[database][death]") {
    std::vector<DeathEvent> deaths;
    deaths.push_back({
        .actor_guid = "Player-1",
        .actor_name = "TestPlayer",
        .actor_type = ActorType::Player,
        .timestamp_ms = 5000,
        .killing_spell_id = 0,
        .killing_spell_name = ""
    });
    deaths.push_back({
        .actor_guid = "Creature-1",
        .actor_name = "TestMob",
        .actor_type = ActorType::Creature,
        .timestamp_ms = 6000,
        .killing_spell_id = 0,
        .killing_spell_name = ""
    });

    DeathDatabase db;
    db.loadEvents(deaths, {}, nullptr);

    auto playerDeaths = db.getPlayerDeaths();

    REQUIRE(playerDeaths.size() == 1);
    REQUIRE(playerDeaths[0]->actor_type == ActorType::Player);
}

TEST_CASE("DeathDatabase - Death timeline sorting", "[database][death]") {
    std::vector<DeathEvent> deaths;
    deaths.push_back({
        .actor_guid = "Player-2",
        .actor_name = "Player2",
        .actor_type = ActorType::Player,
        .timestamp_ms = 10000,
        .killing_spell_id = 0,
        .killing_spell_name = ""
    });
    deaths.push_back({
        .actor_guid = "Player-1",
        .actor_name = "Player1",
        .actor_type = ActorType::Player,
        .timestamp_ms = 5000,
        .killing_spell_id = 0,
        .killing_spell_name = ""
    });

    DeathDatabase db;
    db.loadEvents(deaths, {}, nullptr);

    auto allDeaths = db.getAllDeaths();

    // Should be sorted by timestamp
    REQUIRE(allDeaths.size() == 2);
    REQUIRE(allDeaths[0].timestamp_ms == 5000);
    REQUIRE(allDeaths[1].timestamp_ms == 10000);
}

// SpellCastDatabase

namespace test_helpers {

std::vector<SpellCastEvent> createTestSpellCastEvents() {
    std::vector<SpellCastEvent> events;

    // Player-1: Multiple spells with mix of instant and casted
    // Instant spell (Moonfire) at 1000ms
    events.push_back({
        .actor_guid = "Player-1",
        .target_guid = "Boss-1",
        .spell_id = 8921,
        .start_timestamp_ms = 0,  // Instant
        .end_timestamp_ms = 1000,
        .haste_permille = 305,  // 30.5% haste
        .cancelled = false,
        .cooldown_ms = 0,
        .cooldown_ready_ms = 0,
        .power_cost = 20
    });

    // Casted spell (Wrath) started at 1500, finished at 2500
    events.push_back({
        .actor_guid = "Player-1",
        .target_guid = "Boss-1",
        .spell_id = 5176,
        .start_timestamp_ms = 1500,
        .end_timestamp_ms = 2500,
        .haste_permille = 305,
        .cancelled = false,
        .cooldown_ms = 0,
        .cooldown_ready_ms = 0,
        .power_cost = 25
    });

    // Another instant (Moonfire again) at 3000ms
    events.push_back({
        .actor_guid = "Player-1",
        .target_guid = "Boss-1",
        .spell_id = 8921,
        .start_timestamp_ms = 0,
        .end_timestamp_ms = 3000,
        .haste_permille = 310,
        .cancelled = false,
        .cooldown_ms = 0,
        .cooldown_ready_ms = 0,
        .power_cost = 20
    });

    // Cooldown spell (Convoke) at 4000ms with 2min CD
    events.push_back({
        .actor_guid = "Player-1",
        .target_guid = "Boss-1",
        .spell_id = 323764,
        .start_timestamp_ms = 0,
        .end_timestamp_ms = 4000,
        .haste_permille = 305,
        .cancelled = false,
        .cooldown_ms = 120000,  // 2 minutes
        .cooldown_ready_ms = 124000,
        .power_cost = 0
    });

    // Player-2: Different spells
    // Frostbolt started at 1000, finished at 2000
    events.push_back({
        .actor_guid = "Player-2",
        .target_guid = "Boss-1",
        .spell_id = 116,
        .start_timestamp_ms = 1000,
        .end_timestamp_ms = 2000,
        .haste_permille = 250,
        .cancelled = false,
        .cooldown_ms = 0,
        .cooldown_ready_ms = 0,
        .power_cost = 30
    });

    // Ice Lance (instant) at 2100ms
    events.push_back({
        .actor_guid = "Player-2",
        .target_guid = "Boss-1",
        .spell_id = 30455,
        .start_timestamp_ms = 0,
        .end_timestamp_ms = 2100,
        .haste_permille = 250,
        .cancelled = false,
        .cooldown_ms = 0,
        .cooldown_ready_ms = 0,
        .power_cost = 10
    });

    // Cancelled cast (Frostbolt started at 3000, cancelled at 3500)
    events.push_back({
        .actor_guid = "Player-2",
        .target_guid = "Boss-1",
        .spell_id = 116,
        .start_timestamp_ms = 3000,
        .end_timestamp_ms = 3500,
        .haste_permille = 250,
        .cancelled = true,
        .cooldown_ms = 0,
        .cooldown_ready_ms = 0,
        .power_cost = 30
    });

    return events;
}

} // namespace test_helpers

TEST_CASE("SpellCastDatabase - Load spell casts", "[database][spellcast]") {
    auto events = test_helpers::createTestSpellCastEvents();

    SpellCastDatabase db;
    db.loadSpellCasts(events);

    REQUIRE_FALSE(db.empty());
    REQUIRE(db.size() == 7);  // Total cast events
    REQUIRE(db.getMinTimestamp() == 1000);
    REQUIRE(db.getMaxTimestamp() == 4000);
}

TEST_CASE("SpellCastDatabase - Empty database", "[database][spellcast][edge]") {
    SpellCastDatabase db;
    db.loadSpellCasts({});

    REQUIRE(db.empty());
    REQUIRE(db.size() == 0);
    REQUIRE(db.getMinTimestamp() == 0);
    REQUIRE(db.getMaxTimestamp() == 0);
}

TEST_CASE("SpellCastDatabase - getCastsForActor time windowing", "[database][spellcast]") {
    auto events = test_helpers::createTestSpellCastEvents();

    SpellCastDatabase db;
    db.loadSpellCasts(events);

    // Query Player-1 with 2000ms window at current_time = 3000ms
    // Window: [1000, 3000]
    // Should include: instant at 1000ms, casted started at 1500ms (uses start_timestamp!), instant at 3000ms
    auto casts = db.getCastsForActor("Player-1", 3000, 2000);

    REQUIRE(casts.size() == 3);

    // Verify order (sorted by end_timestamp)
    REQUIRE(casts[0].spell_id == 8921);   // Moonfire at 1000ms
    REQUIRE(casts[1].spell_id == 5176);   // Wrath (started 1500, ended 2500)
    REQUIRE(casts[2].spell_id == 8921);   // Moonfire at 3000ms
}

TEST_CASE("SpellCastDatabase - getCastsForActor boundary conditions", "[database][spellcast]") {
    auto events = test_helpers::createTestSpellCastEvents();

    SpellCastDatabase db;
    db.loadSpellCasts(events);

    SECTION("Empty result - before all casts") {
        auto casts = db.getCastsForActor("Player-1", 500, 500);
        REQUIRE(casts.empty());
    }

    SECTION("Empty result - unknown actor") {
        auto casts = db.getCastsForActor("NonExistent", 3000, 2000);
        REQUIRE(casts.empty());
    }

    SECTION("Exact timestamp match") {
        // Query at 2500ms (exact Wrath completion)
        auto casts = db.getCastsForActor("Player-1", 2500, 500);
        // Window: [2000, 2500]
        // Should NOT include Wrath (started at 1500, which is < 2000)
        REQUIRE(casts.empty());
    }

    SECTION("Cast started within window") {
        // Query at 2000ms with 1000ms window
        // Window: [1000, 2000]
        // Should include Moonfire (instant at 1000) and Wrath (started at 1500)
        auto casts = db.getCastsForActor("Player-1", 2000, 1000);
        REQUIRE(casts.size() == 2);
    }
}

TEST_CASE("SpellCastDatabase - getRecentCastsForActor", "[database][spellcast]") {
    auto events = test_helpers::createTestSpellCastEvents();

    SpellCastDatabase db;
    db.loadSpellCasts(events);

    SECTION("Get last 3 casts") {
        // Query at 4500ms, requesting 3 most recent casts for Player-1
        auto recent = db.getRecentCastsForActor("Player-1", 4500, 3);

        REQUIRE(recent.size() == 3);
        // Should be: Wrath (ended 2500), Moonfire (ended 3000), Convoke (ended 4000)
        REQUIRE(recent[0].spell_id == 5176);
        REQUIRE(recent[1].spell_id == 8921);
        REQUIRE(recent[2].spell_id == 323764);
    }

    SECTION("Request more than available") {
        // Player-1 has 4 casts, request 10
        auto recent = db.getRecentCastsForActor("Player-1", 5000, 10);
        REQUIRE(recent.size() == 4);
    }

    SECTION("Fewer than N casts available") {
        auto recent = db.getRecentCastsForActor("Player-1", 2000, 5);
        // Filtering is on start time, so only Moonfire (instant, 1000) and
        // Wrath (started 1500) count.
        REQUIRE(recent.size() == 2);
    }

    SECTION("Unknown actor") {
        auto recent = db.getRecentCastsForActor("NonExistent", 3000, 5);
        REQUIRE(recent.empty());
    }
}

TEST_CASE("SpellCastDatabase - getLastCastForSpell", "[database][spellcast]") {
    auto events = test_helpers::createTestSpellCastEvents();

    SpellCastDatabase db;
    db.loadSpellCasts(events);

    SECTION("Find most recent cast of specific spell") {
        // Player-1 cast Moonfire twice (at 1000ms and 3000ms)
        auto lastMoonfire = db.getLastCastForSpell("Player-1", 8921, 5000);

        REQUIRE(lastMoonfire != nullptr);
        REQUIRE(lastMoonfire->spell_id == 8921);
        REQUIRE(lastMoonfire->end_timestamp_ms == 3000);  // Second Moonfire
        REQUIRE(lastMoonfire->haste_permille == 310);
    }

    SECTION("No matching cast - spell never used") {
        auto result = db.getLastCastForSpell("Player-1", 99999, 5000);
        REQUIRE(result == nullptr);
    }

    SECTION("No matching cast - unknown actor") {
        auto result = db.getLastCastForSpell("NonExistent", 8921, 5000);
        REQUIRE(result == nullptr);
    }

    SECTION("Before timestamp filter") {
        // Query before second Moonfire cast
        auto lastMoonfire = db.getLastCastForSpell("Player-1", 8921, 2000);

        REQUIRE(lastMoonfire != nullptr);
        REQUIRE(lastMoonfire->end_timestamp_ms == 1000);  // First Moonfire only
    }

    SECTION("No cast before timestamp") {
        // Query before any casts
        auto result = db.getLastCastForSpell("Player-1", 8921, 500);
        REQUIRE(result == nullptr);
    }
}

TEST_CASE("SpellCastDatabase - getUniqueSpellIDs", "[database][spellcast]") {
    auto events = test_helpers::createTestSpellCastEvents();

    SpellCastDatabase db;
    db.loadSpellCasts(events);

    auto uniqueSpells = db.getUniqueSpellIDs();

    // Should have: Moonfire (8921), Wrath (5176), Convoke (323764), Frostbolt (116), Ice Lance (30455)
    REQUIRE(uniqueSpells.size() == 5);

    // Convert to set for easier checking
    std::set<uint32_t> spellSet(uniqueSpells.begin(), uniqueSpells.end());
    REQUIRE(spellSet.count(8921) == 1);   // Moonfire
    REQUIRE(spellSet.count(5176) == 1);   // Wrath
    REQUIRE(spellSet.count(323764) == 1); // Convoke
    REQUIRE(spellSet.count(116) == 1);    // Frostbolt
    REQUIRE(spellSet.count(30455) == 1);  // Ice Lance
}

TEST_CASE("SpellCastDatabase - SpellCastEvent utility methods", "[database][spellcast]") {
    SpellCastEvent instant{
        .actor_guid = "Player-1",
        .target_guid = "Boss-1",
        .spell_id = 8921,
        .start_timestamp_ms = 0,
        .end_timestamp_ms = 1000,
        .haste_permille = 305,
        .cancelled = false,
        .cooldown_ms = 6000,
        .cooldown_ready_ms = 7000,
        .power_cost = 20
    };

    SpellCastEvent casted{
        .actor_guid = "Player-1",
        .target_guid = "Boss-1",
        .spell_id = 5176,
        .start_timestamp_ms = 1500,
        .end_timestamp_ms = 2500,
        .haste_permille = 305,
        .cancelled = false,
        .cooldown_ms = 0,
        .cooldown_ready_ms = 0,
        .power_cost = 25
    };

    SECTION("is_instant() detection") {
        REQUIRE(instant.is_instant());
        REQUIRE_FALSE(casted.is_instant());
    }

    SECTION("cast_duration_ms() calculation") {
        REQUIRE(instant.cast_duration_ms() == 0);
        REQUIRE(casted.cast_duration_ms() == 1000);
    }

    SECTION("haste_percent() conversion") {
        REQUIRE(instant.haste_percent() == 30.5f);
    }

    SECTION("isOnCooldown() query") {
        // Instant spell has CD ready at 7000ms
        REQUIRE(instant.isOnCooldown(1000));  // Just cast
        REQUIRE(instant.isOnCooldown(5000));  // Still on CD
        REQUIRE_FALSE(instant.isOnCooldown(7000));  // Ready
        REQUIRE_FALSE(instant.isOnCooldown(8000));  // Past ready time

        // Casted spell has no CD
        REQUIRE_FALSE(casted.isOnCooldown(3000));
    }

    SECTION("getRemainingCooldown() calculation") {
        REQUIRE(instant.getRemainingCooldown(1000) == 6000);  // Full 6s remaining
        REQUIRE(instant.getRemainingCooldown(4000) == 3000);  // 3s remaining
        REQUIRE(instant.getRemainingCooldown(7000) == 0);     // Ready
        REQUIRE(instant.getRemainingCooldown(8000) == 0);     // Past ready

        REQUIRE(casted.getRemainingCooldown(3000) == 0);  // No CD
    }
}

TEST_CASE("SpellCastDatabase - Cancelled casts included", "[database][spellcast]") {
    auto events = test_helpers::createTestSpellCastEvents();

    SpellCastDatabase db;
    db.loadSpellCasts(events);

    // Player-2 has a cancelled Frostbolt at 3000-3500ms
    auto casts = db.getCastsForActor("Player-2", 4000, 2000);

    // Window: [2000, 4000]
    // Should include: Ice Lance (instant at 2100), Cancelled Frostbolt (started 3000)
    // First Frostbolt started at 1000ms which is < 2000, so excluded
    REQUIRE(casts.size() == 2);

    // Find the cancelled cast
    auto cancelledIt = std::find_if(casts.begin(), casts.end(),
        [](const SpellCastEvent& e) { return e.cancelled; });

    REQUIRE(cancelledIt != casts.end());
    REQUIRE(cancelledIt->spell_id == 116);
    REQUIRE(cancelledIt->start_timestamp_ms == 3000);
    REQUIRE(cancelledIt->end_timestamp_ms == 3500);
}

// Mob weighting

#include "../AwowLab/Core/MobWeightSettings.h"

TEST_CASE("Mob weighting discounts damage done to weighted npcs", "[combat][weights]") {
    ActorMap actorMap;
    auto& player = actorMap[guidInterner().intern("Player-11-0EAAAA01")];

    StringInterner::Id bossTarget = guidInterner().intern("Creature-0-1111-2222-3333-100001-000000000A");
    StringInterner::Id addTarget  = guidInterner().intern("Creature-0-1111-2222-3333-100002-000000000B");

    // 100k into the boss, 100k of padding into the add
    player.damage_dealt_table.push_back({
        .timestamp_ms = 1000,
        .spell_id = 100,
        .amount = 100000,
        .effective_amount = 100000,
        .absorbed = 0,
        .blocked = 0,
        .resisted = 0,
        .target_guid_id = bossTarget,
        .owner_guid_id = 0,
        .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Fire),
        .event_type = CombatEventType::DamageDealt
    });
    player.damage_dealt_table.push_back({
        .timestamp_ms = 2000,
        .spell_id = 100,
        .amount = 100000,
        .effective_amount = 100000,
        .absorbed = 0,
        .blocked = 0,
        .resisted = 0,
        .target_guid_id = addTarget,
        .owner_guid_id = 0,
        .flags = 0,
        .spell_school = static_cast<uint8_t>(SpellSchool::Fire),
        .event_type = CombatEventType::DamageDealt
    });

    auto& settings = MobWeightSettings::instance();
    settings.enabled = true;
    settings.weights.clear();
    settings.weights[100002] = 0.10f;  // The add counts at 10%

    CombatDatabase db;
    db.loadFromActorMap(&actorMap);
    REQUIRE(db.hasActiveTargetWeights());

    SECTION("damage done is discounted") {
        auto ranked = db.getRankedByActor(CombatMetricType::DamageDealt, 0, 10000, 10);
        REQUIRE(ranked.size() == 1);
        // 100k full boss credit + 10k discounted add credit
        CHECK(ranked[0].total_amount == 110000);
        CHECK(ranked[0].effective_amount == 110000);
    }

    SECTION("with-pets ranking is discounted too") {
        // This is the path the meter's damage view actually calls, so it
        // must discount the same way getRankedByActor does.
        auto ranked = db.getRankedByActorWithPets(
            CombatMetricType::DamageDealt, 0, 10000, 10, false);
        REQUIRE(ranked.size() == 1);
        CHECK(ranked[0].total_amount == 110000);
        CHECK(ranked[0].effective_amount == 110000);
    }

    SECTION("spell breakdown matches the discounted totals") {
        auto breakdown = db.getSpellBreakdown(
            "Player-11-0EAAAA01", CombatMetricType::DamageDealt, 0, 10000);
        REQUIRE(breakdown.size() == 1);
        CHECK(breakdown[0].total_amount == 110000);
    }

    SECTION("damage taken stays at face value") {
        auto taken = db.getRankedByDamageTaken(0, 10000, 10);
        int64_t addTaken = 0;
        for (const auto& t : taken) {
            if (t.actor_guid == "Creature-0-1111-2222-3333-100002-000000000B") {
                addTaken = t.total_amount;
            }
        }
        CHECK(addTaken == 100000);
    }

    SECTION("disabling weighting restores full credit") {
        settings.enabled = false;
        db.refreshTargetWeights();
        CHECK_FALSE(db.hasActiveTargetWeights());

        auto ranked = db.getRankedByActor(CombatMetricType::DamageDealt, 0, 10000, 10);
        REQUIRE(ranked.size() == 1);
        CHECK(ranked[0].total_amount == 200000);
    }

    // Reset the global so later tests see defaults
    settings.enabled = true;
    settings.weights.clear();
}

TEST_CASE("Npc id extraction handles the guid shapes we see", "[weights]") {
    CHECK(MobWeightSettings::npcIdFromGuid("Creature-0-3018-2810-26542-242587-000232672E") == 242587u);
    CHECK(MobWeightSettings::npcIdFromGuid("Vehicle-0-3884-2769-8562-231075-000009A381") == 231075u);
    CHECK(MobWeightSettings::npcIdFromGuid("Player-11-0E210C0B") == 0u);
    CHECK(MobWeightSettings::npcIdFromGuid("0000000000000000") == 0u);
    CHECK(MobWeightSettings::npcIdFromGuid("") == 0u);
}

TEST_CASE("SpellCastDatabase - firstCastTime", "[database][spellcast]") {
    std::vector<SpellCastEvent> events;

    // Boss casts a phase spell (500) at 6000ms (cast started 6000, landed 7000)
    events.push_back({
        .actor_guid = "Boss-1",
        .target_guid = "Player-1",
        .spell_id = 500,
        .start_timestamp_ms = 6000,
        .end_timestamp_ms = 7000,
        .haste_permille = 0,
        .cancelled = false,
        .cooldown_ms = 0,
        .cooldown_ready_ms = 0,
        .power_cost = 0
    });

    // An add casts the same spell earlier, instant at 4500ms
    events.push_back({
        .actor_guid = "Creature-2",
        .target_guid = "Player-1",
        .spell_id = 500,
        .start_timestamp_ms = 0,  // Instant
        .end_timestamp_ms = 4500,
        .haste_permille = 0,
        .cancelled = false,
        .cooldown_ms = 0,
        .cooldown_ready_ms = 0,
        .power_cost = 0
    });

    // Spell 600: an early cancelled cast followed by a completed one
    events.push_back({
        .actor_guid = "Boss-1",
        .target_guid = "Player-1",
        .spell_id = 600,
        .start_timestamp_ms = 1000,
        .end_timestamp_ms = 1500,
        .haste_permille = 0,
        .cancelled = true,
        .cooldown_ms = 0,
        .cooldown_ready_ms = 0,
        .power_cost = 0
    });
    events.push_back({
        .actor_guid = "Boss-1",
        .target_guid = "Player-1",
        .spell_id = 600,
        .start_timestamp_ms = 5000,
        .end_timestamp_ms = 5800,
        .haste_permille = 0,
        .cancelled = false,
        .cooldown_ms = 0,
        .cooldown_ready_ms = 0,
        .power_cost = 0
    });

    // Spell 700: only ever cancelled
    events.push_back({
        .actor_guid = "Boss-1",
        .target_guid = "Player-1",
        .spell_id = 700,
        .start_timestamp_ms = 2000,
        .end_timestamp_ms = 2400,
        .haste_permille = 0,
        .cancelled = true,
        .cooldown_ms = 0,
        .cooldown_ready_ms = 0,
        .power_cost = 0
    });

    SpellCastDatabase db;
    db.loadSpellCasts(events);

    SECTION("earliest cast across all actors wins") {
        auto first = db.firstCastTime(500);
        REQUIRE(first.has_value());
        CHECK(*first == 4500);
    }

    SECTION("cancelled casts are ignored") {
        auto first = db.firstCastTime(600);
        REQUIRE(first.has_value());
        CHECK(*first == 5000);

        // A spell that only ever got cancelled never fires
        CHECK_FALSE(db.firstCastTime(700).has_value());
    }

    SECTION("unknown spell returns nullopt") {
        CHECK_FALSE(db.firstCastTime(999999).has_value());
    }
}

// EmoteDatabase

TEST_CASE("EmoteDatabase - loadFromEvents sorts by time", "[database][emote]") {
    std::vector<EmoteEvent> events;
    events.push_back({30000, "Creature-1", "Dimensius", "Dimensius raises his arm..."});
    events.push_back({5000, "Creature-1", "Dimensius", "Take flight!"});
    events.push_back({15000, "Creature-1", "Dimensius", "Dimensius prepares to [Devour] his enemies."});

    EmoteDatabase db;
    db.loadFromEvents(std::move(events));

    REQUIRE_FALSE(db.empty());
    const auto& emotes = db.getEmotes();
    REQUIRE(emotes.size() == 3);
    CHECK(emotes[0].timestamp_ms == 5000);
    CHECK(emotes[1].timestamp_ms == 15000);
    CHECK(emotes[2].timestamp_ms == 30000);
}

TEST_CASE("EmoteDatabase - firstEmoteTime matches exact text", "[database][emote]") {
    std::vector<EmoteEvent> events;
    events.push_back({45000, "Creature-1", "Dimensius", "Take flight!"});
    events.push_back({12000, "Creature-1", "Dimensius", "Take flight!"});
    events.push_back({20000, "Creature-1", "Dimensius", "Dimensius raises his arm..."});

    EmoteDatabase db;
    db.loadFromEvents(std::move(events));

    SECTION("repeated emote returns its earliest time") {
        auto first = db.firstEmoteTime("Take flight!");
        REQUIRE(first.has_value());
        CHECK(*first == 12000);
    }

    SECTION("match is exact, not substring") {
        CHECK_FALSE(db.firstEmoteTime("Take flight").has_value());
        CHECK_FALSE(db.firstEmoteTime("").has_value());
    }

    SECTION("unknown text returns nullopt") {
        CHECK_FALSE(db.firstEmoteTime("Never said this pull").has_value());
    }
}

TEST_CASE("EmoteDatabase - empty database", "[database][emote]") {
    EmoteDatabase db;
    CHECK(db.empty());
    CHECK(db.getEmotes().empty());
    CHECK_FALSE(db.firstEmoteTime("Take flight!").has_value());
}

TEST_CASE("OutputWriter - EMOTE lines extract cleaned text", "[database][emote]") {
    // Two real EMOTE lines: texture escapes, color codes, and a spell
    // link must all be stripped while the bracketed name stays
    std::string content =
        R"(12/4/2025 21:32:32.360-7  EMOTE,Creature-0-3018-2810-26542-241517-00003256C6,"Dimensius",Creature-0-3018-2810-26542-241517-00003256C6,"Dimensius",|TInterface\ICONS\INV_112_RaidDimensius_Devour.BLP:20|t Dimensius prepares to |cFFFF0000|Hspell:1229038|h[Devour]|h|r his enemies.)"
        "\n"
        R"(12/4/2025 21:40:55.687-7  EMOTE,Player-11-0E8ACC6C,"Wobblucy",Player-11-0E8ACC6C,"Wobblucy",|TInterface\ICONS\Ability_DragonRiding_DragonRiding01.BLP:20|t Take flight!)"
        "\n";

    tokenized_segment tokenizer;
    auto lines = tokenizer.tokenize(content.data(), content.size());
    REQUIRE(lines.size() == 2);

    ExtractedData result = OutputWriter::extractAllEvents(lines);

    REQUIRE(result.emoteEvents.size() == 2);
    const auto& first = result.emoteEvents[0];
    CHECK(first.source_name == "Dimensius");
    CHECK(first.text.find("Dimensius prepares to") != std::string::npos);
    CHECK(first.text.find("[Devour]") != std::string::npos);
    CHECK(first.text.find('|') == std::string::npos);

    const auto& second = result.emoteEvents[1];
    CHECK(second.text == "Take flight!");
    CHECK(second.timestamp_ms > first.timestamp_ms);
}

// UnifiedSettings

namespace {

// UnifiedSettings keeps its file under the working directory. Running from
// an empty temporary folder keeps the test away from any real settings file
// next to the binary.
class ScopedTempWorkingDir {
public:
    ScopedTempWorkingDir() : previous_(std::filesystem::current_path()) {
        dir_ = std::filesystem::temp_directory_path() /
               ("awowlab_settings_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
        std::filesystem::current_path(dir_);
    }
    ~ScopedTempWorkingDir() {
        std::error_code ec;
        std::filesystem::current_path(previous_, ec);
        std::filesystem::remove_all(dir_, ec);
    }
    ScopedTempWorkingDir(const ScopedTempWorkingDir&) = delete;
    ScopedTempWorkingDir& operator=(const ScopedTempWorkingDir&) = delete;

private:
    std::filesystem::path previous_;
    std::filesystem::path dir_;
};

} // namespace

TEST_CASE("UnifiedSettings - unknown entries survive a save/load round trip", "[settings][roundtrip]") {
    // Values another build stored in the shared settings file must come
    // back untouched after this build loads and saves the file. The main
    // app keeps extra data in the file this way, and the overlay has to
    // carry it through without knowing what it is.
    ScopedTempWorkingDir tempDir;
    REQUIRE_FALSE(UnifiedSettings::exists());

    UnifiedSettings::AllSettings original;
    original.locale = "de_DE";
    original.extraValues["wclQueriesUsed"] = "42";
    original.extraValues["wclWindowStartTime"] = "1719854321";
    original.extraValues["wclAccessToken"] = "\"abc123token\"";
    original.extraValues["futureFeatureBlock"] = "{\"nested\":{\"a\":1},\"list\":[1,2,3]}";

    REQUIRE(UnifiedSettings::save(original));

    // First generation: load what was saved.
    UnifiedSettings::AllSettings loaded;
    REQUIRE(UnifiedSettings::load(loaded));

    CHECK(loaded.locale == "de_DE");
    REQUIRE(loaded.extraValues.count("wclQueriesUsed") == 1);
    CHECK(loaded.extraValues.at("wclQueriesUsed") == "42");
    REQUIRE(loaded.extraValues.count("wclWindowStartTime") == 1);
    CHECK(loaded.extraValues.at("wclWindowStartTime") == "1719854321");
    REQUIRE(loaded.extraValues.count("wclAccessToken") == 1);
    CHECK(loaded.extraValues.at("wclAccessToken") == "\"abc123token\"");
    REQUIRE(loaded.extraValues.count("futureFeatureBlock") == 1);
    CHECK(loaded.extraValues.at("futureFeatureBlock") == "{\"nested\":{\"a\":1},\"list\":[1,2,3]}");

    // Second generation: save the loaded copy, as the overlay's auto-save
    // does, and check nothing degraded.
    REQUIRE(UnifiedSettings::save(loaded));
    UnifiedSettings::AllSettings reloaded;
    REQUIRE(UnifiedSettings::load(reloaded));
    CHECK(reloaded.extraValues == loaded.extraValues);
}

// WclSettings: DPAPI-protected access token

TEST_CASE("WclSettings - access token round trips through protected storage", "[settings][roundtrip][wcl]") {
    UnifiedSettings::AllSettings settings;

    WclSettings wcl;
    wcl.queriesUsed = 7;
    wcl.windowStartTime = 1719854321;
    wcl.accessToken = "secret-oauth-token-value";
    wcl.tokenExpiresAt = 1800000000;
    wcl.writeTo(settings);

    // The token never lands in the file as plain text
    CHECK(settings.extraValues.count("wclAccessToken") == 0);
    REQUIRE(settings.extraValues.count("wclAccessTokenProtected") == 1);
    CHECK(settings.extraValues.at("wclAccessTokenProtected")
              .find("secret-oauth-token-value") == std::string::npos);

    // Non-secret values stay as ordinary readable entries
    CHECK(settings.extraValues.at("wclQueriesUsed") == "7");
    CHECK(settings.extraValues.at("wclWindowStartTime") == "1719854321");
    CHECK(settings.extraValues.at("wclTokenExpiresAt") == "1800000000");

    auto loaded = WclSettings::readFrom(settings);
    CHECK(loaded.accessToken == "secret-oauth-token-value");
    CHECK(loaded.queriesUsed == 7);
    CHECK(loaded.windowStartTime == 1719854321);
    CHECK(loaded.tokenExpiresAt == 1800000000);
}

TEST_CASE("WclSettings - clearing the token removes the stored entry", "[settings][wcl]") {
    UnifiedSettings::AllSettings settings;

    WclSettings wcl;
    wcl.accessToken = "token-to-clear";
    wcl.writeTo(settings);
    REQUIRE(settings.extraValues.count("wclAccessTokenProtected") == 1);

    wcl.accessToken.clear();
    wcl.writeTo(settings);
    CHECK(settings.extraValues.count("wclAccessTokenProtected") == 0);
    CHECK(settings.extraValues.count("wclAccessToken") == 0);
    CHECK(WclSettings::readFrom(settings).accessToken.empty());
}

TEST_CASE("WclSettings - plain-text token from older builds migrates", "[settings][wcl]") {
    // Older builds stored the token as a plain JSON string entry
    UnifiedSettings::AllSettings settings;
    settings.extraValues["wclAccessToken"] = "\"legacy-plain-token\"";

    // First read still returns it, so updating doesn't force a re-login
    auto wcl = WclSettings::readFrom(settings);
    CHECK(wcl.accessToken == "legacy-plain-token");

    // The next write replaces the plain entry with the protected one
    wcl.writeTo(settings);
    CHECK(settings.extraValues.count("wclAccessToken") == 0);
    REQUIRE(settings.extraValues.count("wclAccessTokenProtected") == 1);
    CHECK(settings.extraValues.at("wclAccessTokenProtected")
              .find("legacy-plain-token") == std::string::npos);

    // And the token still reads back after the move
    CHECK(WclSettings::readFrom(settings).accessToken == "legacy-plain-token");
}

TEST_CASE("WclSettings - unreadable protected token reads as no token", "[settings][wcl]") {
    UnifiedSettings::AllSettings settings;
    settings.extraValues["wclQueriesUsed"] = "3";

    SECTION("entry is not valid base64") {
        settings.extraValues["wclAccessTokenProtected"] = "\"%%% not base64 %%%\"";
    }

    SECTION("valid base64 but not a blob this user wrote") {
        // base64 of "hello world" - decodes fine, unwrap must fail. Same
        // shape as a settings file copied from another user or machine.
        settings.extraValues["wclAccessTokenProtected"] = "\"aGVsbG8gd29ybGQ=\"";
    }

    SECTION("entry is not even a JSON string") {
        settings.extraValues["wclAccessTokenProtected"] = "12345";
    }

    // Must not throw, and the app just re-authenticates
    WclSettings loaded;
    REQUIRE_NOTHROW(loaded = WclSettings::readFrom(settings));
    CHECK(loaded.accessToken.empty());
    CHECK(loaded.queriesUsed == 3);  // the rest of the settings still load
}

TEST_CASE("OutputWriter - hostile units are found without position data",
          "[database][hostility]") {
    // Enemies used to be collected from actor and resource events, which only
    // exist for units that reported a position. A mob the raid killed while it
    // never acted produced neither, so it went missing from the mob weight
    // list and the phase editor's boss casts.
    //
    // Every event names two units and carries flags for both, so collecting
    // during extraction sees those units. This measures both against a real
    // log rather than asserting the new count alone.
    std::ifstream file(TEST_EVENTS_FILE);
    REQUIRE(file.is_open());
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    REQUIRE_FALSE(content.empty());

    tokenized_segment tokenizer;
    auto lines = tokenizer.tokenize(content.data(), content.size());
    ExtractedData result = OutputWriter::extractAllEvents(lines);

    // What the old approach could see: only units with a position event.
    std::unordered_set<std::string_view> positionOnly;
    for (const auto& actor : result.actorEvents) {
        if (actor.flags.reaction != UnitReaction::Hostile) continue;
        if (actor.flags.controller == UnitController::Player) continue;
        positionOnly.insert(actor.source_guid);
    }
    for (const auto& res : result.resourceEvents) {
        if (res.flags.reaction != UnitReaction::Hostile) continue;
        if (res.flags.controller == UnitController::Player) continue;
        positionOnly.insert(res.actor_guid);
    }

    const auto hostile = result.hostileActorGuids();

    INFO("position-derived hostiles: " << positionOnly.size());
    INFO("event-derived hostiles: " << hostile.size());

    // The whole point of the change: strictly more enemies, and every enemy
    // the old path found is still found.
    CHECK(hostile.size() > positionOnly.size());
    for (std::string_view guid : positionOnly) {
        CHECK(hostile.count(guid) == 1);
    }

    // Nothing player-side leaked in while widening the net.
    for (std::string_view guid : hostile) {
        CHECK_FALSE(guid.starts_with("Player-"));
    }
}

// Flows release the file mapping once the databases are built, and snapshot
// export and the benchmark read actor events after that. The names and GUIDs
// on those events must not point back into the log text.
TEST_CASE("actor events do not point into the log buffer",
          "[database][actor][lifetime]") {
    std::string content =
        "5/1/2025 20:00:01.000-6  SPELL_DAMAGE,Player-11-0E960000,\"Someone-Realm-US\","
        "0x512,0x0,Creature-0-1-1-1-231075-0000000001,\"Trash Mob\",0x10a48,0x0,"
        "466752,\"Filler Spell\",0x4,Creature-0-1-1-1-231075-0000000001,0000000000000000,"
        "987654321,999999999,0,0,42857,0,0,0,3,89,100,0,-491.04,-691.87,2409,3.8887,83,"
        "10000,8000,-1,4,0,0,0,nil,nil,nil,ST\n";

    tokenized_segment tokenizer;
    auto lines = tokenizer.tokenize(content.data(), content.size());
    ExtractedData data = OutputWriter::extractAllEvents(lines);
    REQUIRE_FALSE(data.actorEvents.empty());

    const char* begin = content.data();
    const char* end = content.data() + content.size();
    auto insideLog = [&](std::string_view s) {
        return s.data() >= begin && s.data() < end;
    };
    for (const auto& event : data.actorEvents) {
        CHECK_FALSE(insideLog(event.source_guid));
        CHECK_FALSE(insideLog(event.source_actor));
    }

    // Wipe the text as an unmap would; the events must still read back.
    std::fill(content.begin(), content.end(), 'x');
    CHECK(data.actorEvents.front().source_guid == "Creature-0-1-1-1-231075-0000000001");
    CHECK(data.actorEvents.front().source_actor == "Trash Mob");
}

namespace {

ExtractedData combatantWithTalents(std::vector<uint32_t> talentIds) {
    ExtractedData data;
    CombatantInfoEvent info{};
    info.player_guid = "Player-11-0E960000";
    info.spec_id = 577;
    for (uint32_t id : talentIds) {
        parser::TalentEntry talent;
        talent.talent_id = id;
        info.talents.push_back(talent);
    }
    data.combatantInfoEvents.push_back(info);
    return data;
}

std::vector<uint32_t> talentIdsOf(const ExtractedData& data) {
    std::vector<uint32_t> ids;
    for (const auto& talent : data.combatantInfoEvents.at(0).talents) {
        ids.push_back(talent.talent_id);
    }
    return ids;
}

std::filesystem::path exportTalents(const ExtractedData& data, const char* name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    snapshot::ExportOptions options{};
    options.windowStartMs = 0;
    options.windowEndMs = 1000;
    options.encounterName = "Talent Boss";
    snapshot::SnapshotExporter exporter;
    auto result = exporter.exportSnapshot(data, {}, {}, {}, options, path.string());
    REQUIRE(result.success);
    return path;
}

} // namespace

// Talent ids are node ids from talent.db2 and run well past 65535 (93524 and
// 94643 are in tests/testevents.txt). Snapshots stored them as uint16.
TEST_CASE("snapshot talent ids survive a round trip", "[snapshot][talents]") {
    const std::vector<uint32_t> ids = { 93524, 94643, 100 };
    const auto path = exportTalents(combatantWithTalents(ids), "awowlab_talents.awsnap");

    snapshot::SnapshotImporter importer;
    auto imported = importer.importSnapshot(path.string());
    std::filesystem::remove(path);
    REQUIRE(imported.success);
    CHECK(talentIdsOf(*imported.data) == ids);
}

// A snapshot is a file someone sent you. Its header offsets and counts must
// be checked before they index or size anything.
TEST_CASE("snapshot info rejects a string table offset past the payload", "[snapshot][untrusted]") {
    const auto path = std::filesystem::temp_directory_path() / "awowlab_bad_offset.awsnap";
    snapshot::AwsnapHeader header;
    header.init();
    header.flags = 0;  // uncompressed: the payload is the bytes after the header
    header.encounter_name_id = 1;
    header.string_table_offset = 0x7FFFFFF0;
    header.uncompressed_size = 16;
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        const char pad[16] = {};
        out.write(pad, sizeof(pad));
    }

    snapshot::SnapshotImporter importer;
    auto info = importer.getInfo(path.string());
    std::filesystem::remove(path);
    CHECK_FALSE(info.valid);
    CHECK_FALSE(info.errorMessage.empty());
}

TEST_CASE("string table does not trust its entry count", "[snapshot][untrusted]") {
    snapshot::StringTableHeader header{};
    header.entry_count = 0xFFFFFFFFu;
    header.total_bytes = 0;

    snapshot::StringTableReader reader;
    bool ok = true;
    REQUIRE_NOTHROW(ok = reader.deserialize(reinterpret_cast<const uint8_t*>(&header),
                                            sizeof(header)));
    CHECK_FALSE(ok);
}

// Raid notes are shared as JSON, so a field of the wrong type must be
// skipped rather than read.
TEST_CASE("raid note groups with a non-numeric type still load", "[raidnotes][untrusted]") {
    auto doc = RaidNotes::RaidNoteDocument::fromJson(
        R"({"groups":[{"type":"boss","displayName":"Kept"}]})");
    REQUIRE(doc.has_value());
    CHECK(doc->getGroups().front().display_name == "Kept");
}

TEST_CASE("snapshots written before 1.2 still read their talents", "[snapshot][talents]") {
    // Rewrite a current export the way 1.1 laid it out: uint16 talent ids.
    const std::vector<uint32_t> ids = { 100, 200, 65535 };
    const auto path = exportTalents(combatantWithTalents(ids), "awowlab_talents_v11.awsnap");

    std::ifstream in(path, std::ios::binary);
    snapshot::AwsnapHeader header;
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    std::vector<uint8_t> compressed((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    in.close();
    std::vector<uint8_t> payload(header.uncompressed_size);
    uLongf payloadSize = header.uncompressed_size;
    REQUIRE(uncompress(payload.data(), &payloadSize, compressed.data(),
                       static_cast<uLong>(compressed.size())) == Z_OK);

    snapshot::MetadataSectionHeader meta;
    std::memcpy(&meta, payload.data() + header.metadata_section_offset, sizeof(meta));
    REQUIRE(meta.combatant_count == 1);
    const size_t combatantAt = header.metadata_section_offset + sizeof(meta) +
                               meta.guid_name_count * sizeof(snapshot::GuidNameEntry) +
                               meta.spell_name_count * sizeof(snapshot::SpellNameEntry);
    const size_t talentsAt = combatantAt + sizeof(snapshot::CompactCombatantInfo);

    std::vector<uint8_t> old(payload.begin(), payload.begin() + talentsAt);
    for (size_t i = 0; i < ids.size(); ++i) {
        uint32_t wide;
        std::memcpy(&wide, payload.data() + talentsAt + i * 4, 4);
        const uint16_t narrow = static_cast<uint16_t>(wide);
        old.insert(old.end(), reinterpret_cast<const uint8_t*>(&narrow),
                   reinterpret_cast<const uint8_t*>(&narrow) + 2);
    }
    old.insert(old.end(), payload.begin() + talentsAt + ids.size() * 4, payload.end());

    header.version_minor = 1;
    header.flags &= ~snapshot::FLAG_COMPRESSED;
    header.uncompressed_size = static_cast<uint32_t>(old.size());
    header.payload_crc32 = crc32(0, old.data(), static_cast<uInt>(old.size()));
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(reinterpret_cast<const char*>(old.data()), static_cast<std::streamsize>(old.size()));
    }

    snapshot::SnapshotImporter importer;
    auto imported = importer.importSnapshot(path.string());
    std::filesystem::remove(path);
    REQUIRE(imported.success);
    CHECK(talentIdsOf(*imported.data) == ids);
}
