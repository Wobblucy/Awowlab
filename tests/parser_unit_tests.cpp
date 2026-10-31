#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <string_view>
#include <vector>
#include <cmath>

// Include parsers under test
#include "../AwowLab/Parser/Include/Parser/Parser_Damage.h"
#include "../AwowLab/Parser/Include/Parser/Parser_Heal.h"
#include "../AwowLab/Parser/Include/Parser/Parser_Missed.h"
#include "../AwowLab/Parser/Include/Parser/Parser_Aura.h"
#include "../AwowLab/Parser/Include/Parser/Parser_Absorb.h"
#include "../AwowLab/Parser/Include/Parser/Parser_Cast.h"
#include "../AwowLab/Parser/Include/Parser/Parser_Resource.h"
#include "../AwowLab/Parser/Include/Parser/Parser_Dispel.h"
#include "../AwowLab/Parser/Include/Parser/Parser_Encounter.h"
#include "../AwowLab/Parser/Include/Parser/Parser_ChallengeMode.h"
#include "../AwowLab/Parser/Include/Parser/Parser_Unit.h"
#include "../AwowLab/Parser/Include/Parser/Parser_Support.h"
#include "../AwowLab/Parser/Include/Parser/Parser_Meta.h"
#include "../AwowLab/Parser/Include/Parser/Parser_Combatant_Info.h"
#include "../AwowLab/Parser/Include/Structures/CombatTypes.h"
#include "../AwowLab/Parser/Include/Structures/Records.h"
#include "../AwowLab/Parser/Include/Encounter/EncounterScanner.h"
#include "../AwowLab/Txttokenizer/include/Txttokenizer/Txttokenizer.h"
#include <string>

// Parser_Damage.h Tests

TEST_CASE("SPELL_DAMAGE parses all fields correctly", "[parser][damage]") {
    // Combat log never contains double quotes - real format example
    const std::vector<std::string_view> tokens = {
        // Base (11): date, time, event, sourceGUID, sourceName, sourceFlags, sourceRaidFlags,
        //            destGUID, destName, destFlags, destRaidFlags
        "1/15/2026", "14:23:45.123", "SPELL_DAMAGE",
        "Player-1-00000001", "TestPlayer", "0x511", "0x0",
        "Creature-0-00000002", "TestBoss", "0xa48", "0x0",

        // Spell (3): spellID, spellName, spellSchool
        "123456", "Fireball", "4",

        // Advanced (19): infoGUID, ownerGUID, currentHP, maxHP, attackPower, spellPower,
        //                armor, absorb, unknown1, unknown2, powerType, currentPower, maxPower,
        //                powerCost, posX, posY, mapID, facing, level
        "Player-1-00000001", "0000000000000000",
        "50000", "100000", "1000", "2000", "500", "0",
        "0", "0", "0", "5000", "10000", "100",
        "1234.56", "-5678.90", "2222", "1.5708", "70",

        // Damage suffix (11): amount, baseAmount, overkill, school, resisted, blocked,
        //                     absorbed, critical, glancing, crushing, isOffHand
        "10000", "12000", "-1", "4", "0", "0", "2000", "1", "nil", "nil", "nil"
    };

    auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);

    // Verify base event data
    REQUIRE(data.source_guid == "Player-1-00000001");
    REQUIRE(data.source_name == "TestPlayer");
    REQUIRE(data.dest_guid == "Creature-0-00000002");
    REQUIRE(data.dest_name == "TestBoss");

    // Verify spell info
    REQUIRE(data.spell.spell_id == 123456);
    REQUIRE(data.spell.spell_name == "Fireball");
    REQUIRE(data.spell.spell_school == 0x4);

    // Verify advanced info
    REQUIRE(data.advanced_info.current_hp == 50000);
    REQUIRE(data.advanced_info.max_hp == 100000);
    REQUIRE(data.advanced_info.attack_power == 1000);
    REQUIRE(data.advanced_info.spell_power == 2000);
    REQUIRE(data.advanced_info.armor == 500);

    // Verify position data
    REQUIRE(data.advanced_info.position.valid);
    REQUIRE(data.advanced_info.position.x_loc == Catch::Approx(1234.56f));
    REQUIRE(data.advanced_info.position.y_loc == Catch::Approx(-5678.90f));
    REQUIRE(data.advanced_info.position.map_id == 2222);
    REQUIRE(data.advanced_info.position.facing == static_cast<uint16_t>(std::lround(1.5708 * 10000.0f)));
    REQUIRE(data.advanced_info.level == 70);

    // Verify damage suffix - mitigated_amount is at position 0, amount (unmitigated) at position 1
    REQUIRE(data.mitigated_amount == 10000);  // Post-mitigation
    REQUIRE(data.amount == 12000);             // Unmitigated base amount
    REQUIRE(data.overkill == -1);
    REQUIRE(data.damage_school == 4);
    REQUIRE(data.absorbed == 2000);
    REQUIRE(data.critical == true);
    REQUIRE(data.glancing == false);
    REQUIRE(data.crushing == false);
    REQUIRE(data.is_off_hand == false);
}

TEST_CASE("SPELL_DAMAGE critical hit flag extraction", "[parser][damage]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_DAMAGE",
        "Player-1-00000001", "TestPlayer", "0x511", "0x0",
        "Creature-0-00000002", "TestBoss", "0xa48", "0x0",
        "123456", "Pyroblast", "4",
        "Player-1-00000001", "0000000000000000",
        "50000", "100000", "1000", "2000", "500", "0",
        "0", "0", "0", "5000", "10000", "100",
        "1234.56", "-5678.90", "2222", "1.5708", "70",
        "20000", "25000", "5000", "4", "0", "0", "0", "1", "nil", "nil", "nil"
    };

    auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);

    REQUIRE(data.critical == true);
    REQUIRE(data.amount == 25000);
    REQUIRE(data.overkill == 5000);
}

TEST_CASE("SPELL_DAMAGE glancing and crushing flags", "[parser][damage]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_DAMAGE",
        "Player-1-00000001", "Warrior", "0x511", "0x0",
        "Creature-0-00000002", "Dummy", "0xa48", "0x0",
        "1", "Melee", "1",
        "Player-1-00000001", "0000000000000000",
        "50000", "100000", "5000", "100", "1000", "0",
        "0", "0", "0", "5000", "10000", "0",
        "100.0", "200.0", "2222", "0.0", "70",
        "800", "1000", "-1", "1", "0", "0", "0", "nil", "1", "nil", "nil"
    };

    auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);

    REQUIRE(data.glancing == true);
    REQUIRE(data.critical == false);
    REQUIRE(data.crushing == false);
}

TEST_CASE("SPELL_PERIODIC_DAMAGE parsing", "[parser][damage][periodic]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_PERIODIC_DAMAGE",
        "Player-1-00000001", "Warlock", "0x511", "0x0",
        "Creature-0-00000002", "Target", "0xa48", "0x0",
        "172", "Corruption", "32",
        "Player-1-00000001", "0000000000000000",
        "50000", "100000", "1000", "2500", "500", "0",
        "0", "0", "0", "5000", "10000", "0",
        "500.0", "600.0", "2222", "3.14", "70",
        "1500", "1500", "-1", "32", "0", "0", "0", "nil", "nil", "nil", "nil"
    };

    auto data = parser::EventParser<parser::SpellPeriodicDamageTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 172);
    REQUIRE(data.spell.spell_name == "Corruption");
    REQUIRE(data.amount == 1500);
    REQUIRE(data.damage_school == 32);  // Shadow
}

TEST_CASE("SWING_DAMAGE_LANDED parsing with synthetic spell alignment", "[parser][damage][swing]") {
    // SWING events have 40 tokens (no spell prefix)
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SWING_DAMAGE_LANDED",
        "Player-1-00000001", "Rogue", "0x511", "0x0",
        "Creature-0-00000002", "Target", "0xa48", "0x0",
        // No spell prefix - advanced info starts at token 11 (not 14)
        "Player-1-00000001", "0000000000000000",
        "50000", "100000", "3000", "500", "1500", "0",
        "0", "0", "0", "5000", "10000", "0",
        "100.0", "200.0", "2222", "0.0", "70",
        // Damage suffix starts at token 30 (not 33) - only 10 fields (no isOffHand)
        "2500", "3000", "-1", "1", "0", "500", "0", "1", "nil", "nil"
    };

    auto data = parser::EventParser<parser::SwingDamageLandedTag>::parse_and_return(tokens);

    // Verify synthetic spell info
    REQUIRE(data.spell.spell_id == 1);
    REQUIRE(data.spell.spell_name == "Melee");
    REQUIRE(data.spell.spell_school == 1);  // Physical

    // Verify damage
    REQUIRE(data.mitigated_amount == 2500);
    REQUIRE(data.amount == 3000);
    REQUIRE(data.blocked == 500);
    REQUIRE(data.critical == true);
    REQUIRE(data.is_off_hand == false);  // SWING events don't have this field
}

TEST_CASE("SWING_DAMAGE_LANDED position data extraction", "[parser][damage][swing]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SWING_DAMAGE_LANDED",
        "Player-1-00000001", "Warrior", "0x511", "0x0",
        "Creature-0-00000002", "Boss", "0xa48", "0x0",
        "Player-1-00000001", "0000000000000000",
        "80000", "100000", "4000", "500", "2000", "0",
        "0", "0", "0", "8000", "10000", "0",
        "-1234.5", "5678.9", "1234", "4.7124", "80",
        "5000", "5000", "-1", "1", "0", "0", "0", "nil", "nil", "nil"
    };

    auto data = parser::EventParser<parser::SwingDamageLandedTag>::parse_and_return(tokens);

    REQUIRE(data.advanced_info.position.valid);
    REQUIRE(data.advanced_info.position.x_loc == Catch::Approx(-1234.5f));
    REQUIRE(data.advanced_info.position.y_loc == Catch::Approx(5678.9f));
    REQUIRE(data.advanced_info.position.map_id == 1234);
    REQUIRE(data.advanced_info.position.facing == static_cast<uint16_t>(std::lround(4.7124 * 10000.0f)));
}

TEST_CASE("DAMAGE_SPLIT parsing", "[parser][damage][split]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "DAMAGE_SPLIT",
        "Player-1-00000001", "Tank", "0x511", "0x0",
        "Player-1-00000002", "Ally", "0x514", "0x0",
        "114018", "Avengers Shield", "2",
        "Player-1-00000001", "0000000000000000",
        "90000", "100000", "3000", "1500", "3000", "0",
        "0", "0", "0", "8000", "10000", "0",
        "100.0", "200.0", "2222", "1.57", "70",
        "3000", "3000", "-1", "2", "0", "0", "0", "nil", "nil", "nil", "nil"
    };

    auto data = parser::EventParser<parser::DamageSplitTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 114018);
    REQUIRE(data.amount == 3000);
    REQUIRE(data.dest_name == "Ally");
}

// Parser_Heal.h Tests

TEST_CASE("SPELL_HEAL parses all fields correctly", "[parser][heal]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_HEAL",
        "Player-1-00000001", "Priest", "0x511", "0x0",
        "Player-1-00000002", "Warrior", "0x514", "0x0",
        "2061", "Flash Heal", "2",
        "Player-1-00000001", "0000000000000000",
        "95000", "100000", "1000", "3500", "500", "0",
        "0", "0", "0", "8000", "10000", "500",
        "100.0", "200.0", "2222", "0.0", "70",
        // Heal suffix (5): amount, baseAmount, overhealing, absorbed, critical
        "15000", "14000", "5000", "0", "1"
    };

    auto data = parser::EventParser<parser::SpellHealTag>::parse_and_return(tokens);

    // Verify spell info
    REQUIRE(data.spell.spell_id == 2061);
    REQUIRE(data.spell.spell_name == "Flash Heal");

    // Verify heal data
    REQUIRE(data.amount == 15000);
    REQUIRE(data.overhealing == 5000);
    REQUIRE(data.absorbed == 0);
    REQUIRE(data.critical == true);
}

TEST_CASE("SPELL_HEAL with overhealing calculation", "[parser][heal]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_HEAL",
        "Player-1-00000001", "Paladin", "0x511", "0x0",
        "Player-1-00000002", "Rogue", "0x514", "0x0",
        "19750", "Flash of Light", "2",
        "Player-1-00000001", "0000000000000000",
        "95000", "100000", "1500", "2800", "500", "0",
        "0", "0", "0", "8000", "10000", "200",
        "100.0", "200.0", "2222", "0.0", "70",
        "10000", "9500", "8000", "0", "nil"  // 80% overhealing
    };

    auto data = parser::EventParser<parser::SpellHealTag>::parse_and_return(tokens);

    REQUIRE(data.amount == 10000);
    REQUIRE(data.overhealing == 8000);
    REQUIRE(data.critical == false);
}

TEST_CASE("SPELL_PERIODIC_HEAL parsing", "[parser][heal][periodic]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_PERIODIC_HEAL",
        "Player-1-00000001", "Druid", "0x511", "0x0",
        "Player-1-00000002", "Tank", "0x514", "0x0",
        "774", "Rejuvenation", "8",
        "Player-1-00000001", "0000000000000000",
        "85000", "100000", "1000", "3000", "500", "0",
        "0", "0", "0", "8000", "10000", "0",
        "100.0", "200.0", "2222", "0.0", "70",
        "3500", "3500", "0", "0", "nil"
    };

    auto data = parser::EventParser<parser::SpellPeriodicHealTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 774);
    REQUIRE(data.spell.spell_name == "Rejuvenation");
    REQUIRE(data.amount == 3500);
    REQUIRE(data.overhealing == 0);
}

TEST_CASE("SPELL_HEAL absorbed healing tracking", "[parser][heal]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_HEAL",
        "Player-1-00000001", "Monk", "0x511", "0x0",
        "Player-1-00000002", "Hunter", "0x514", "0x0",
        "116670", "Vivify", "8",
        "Player-1-00000001", "0000000000000000",
        "80000", "100000", "1200", "2500", "500", "0",
        "0", "0", "0", "7000", "10000", "300",
        "100.0", "200.0", "2222", "0.0", "70",
        "12000", "12000", "2000", "1000", "nil"  // 1000 absorbed
    };

    auto data = parser::EventParser<parser::SpellHealTag>::parse_and_return(tokens);

    REQUIRE(data.amount == 12000);
    REQUIRE(data.absorbed == 1000);
    REQUIRE(data.overhealing == 2000);
}

// Parser_Missed.h Tests

TEST_CASE("SPELL_MISSED with DODGE miss type", "[parser][missed]") {
    // 17 tokens: base(11) + spell(3) + missType(1) + isOffHand(1) + ST(1)
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_MISSED",
        "Player-1-00000001", "Rogue", "0x511", "0x0",
        "Creature-0-00000002", "Boss", "0xa48", "0x0",
        "53", "Backstab", "1",
        "DODGE", "nil", "nil"
    };

    auto data = parser::EventParser<parser::SpellMissedTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 53);
    REQUIRE(data.spell.spell_name == "Backstab");
    REQUIRE(data.miss_type == MissType::DODGE);
    REQUIRE(data.amount_missed == 0);
    REQUIRE(data.has_position == false);
}

TEST_CASE("SPELL_MISSED with PARRY miss type", "[parser][missed]") {
    // 17 tokens: base(11) + spell(3) + missType(1) + isOffHand(1) + ST(1)
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_MISSED",
        "Player-1-00000001", "Warrior", "0x511", "0x0",
        "Creature-0-00000002", "Boss", "0xa48", "0x0",
        "23922", "Shield Slam", "1",
        "PARRY", "nil", "nil"
    };

    auto data = parser::EventParser<parser::SpellMissedTag>::parse_and_return(tokens);

    REQUIRE(data.miss_type == MissType::PARRY);
}

TEST_CASE("SPELL_MISSED with ABSORB and amount", "[parser][missed]") {
    // 20 tokens: base(11) + spell(3) + missType(1) + isOffHand(1) + amountMissed(1) + totalPending(1) + critical(1) + AOE(1)
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_MISSED",
        "Player-1-00000001", "Mage", "0x511", "0x0",
        "Creature-0-00000002", "Boss", "0xa48", "0x0",
        "133", "Fireball", "4",
        "ABSORB", "nil", "15000", "15000", "1", "nil"
    };

    auto data = parser::EventParser<parser::SpellMissedTag>::parse_and_return(tokens);

    REQUIRE(data.miss_type == MissType::ABSORB);
    REQUIRE(data.amount_missed == 15000);
    REQUIRE(data.total_pending == 15000);
    REQUIRE(data.critical == true);
}

TEST_CASE("SPELL_MISSED with BLOCK and amount", "[parser][missed]") {
    // 20 tokens: base(11) + spell(3) + missType(1) + isOffHand(1) + amountMissed(1) + totalPending(1) + critical(1) + AOE(1)
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_MISSED",
        "Player-1-00000001", "Warrior", "0x511", "0x0",
        "Creature-0-00000002", "Boss", "0xa48", "0x0",
        "6572", "Revenge", "1",
        "BLOCK", "nil", "3000", "10000", "nil", "nil"
    };

    auto data = parser::EventParser<parser::SpellMissedTag>::parse_and_return(tokens);

    REQUIRE(data.miss_type == MissType::BLOCK);
    REQUIRE(data.amount_missed == 3000);
}

TEST_CASE("SWING_MISSED with DODGE", "[parser][missed][swing]") {
    // SWING_MISSED has 13 tokens (no spell prefix)
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SWING_MISSED",
        "Player-1-00000001", "Rogue", "0x511", "0x0",
        "Creature-0-00000002", "Boss", "0xa48", "0x0",
        "DODGE", "nil"
    };

    auto data = parser::EventParser<parser::SwingMissedTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 0);
    REQUIRE(data.spell.spell_name == "Melee");
    REQUIRE(data.miss_type == MissType::DODGE);
}

TEST_CASE("SWING_MISSED with ABSORB amount", "[parser][missed][swing]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SWING_MISSED",
        "Player-1-00000001", "Warrior", "0x511", "0x0",
        "Creature-0-00000002", "Boss", "0xa48", "0x0",
        "ABSORB", "nil", "5000", "nil"  // 14 tokens with amount
    };

    auto data = parser::EventParser<parser::SwingMissedTag>::parse_and_return(tokens);

    REQUIRE(data.miss_type == MissType::ABSORB);
    REQUIRE(data.amount_missed == 5000);
}

TEST_CASE("SPELL_MISSED with IMMUNE miss type", "[parser][missed]") {
    // 17 tokens: base(11) + spell(3) + missType(1) + isOffHand(1) + ST(1)
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_MISSED",
        "Player-1-00000001", "Paladin", "0x511", "0x0",
        "Creature-0-00000002", "Boss", "0xa48", "0x0",
        "853", "Hammer of Justice", "2",
        "IMMUNE", "nil", "nil"
    };

    auto data = parser::EventParser<parser::SpellMissedTag>::parse_and_return(tokens);

    REQUIRE(data.miss_type == MissType::IMMUNE);
}

// Parser_Aura.h Tests

TEST_CASE("SPELL_AURA_APPLIED BUFF parsing", "[parser][aura]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_AURA_APPLIED",
        "Player-1-00000001", "Priest", "0x511", "0x0",
        "Player-1-00000002", "Warrior", "0x514", "0x0",
        "21562", "Power Word: Fortitude", "2",
        "BUFF"
    };

    auto data = parser::EventParser<parser::SpellAuraAppliedTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 21562);
    REQUIRE(data.spell.spell_name == "Power Word: Fortitude");
    REQUIRE(data.aura_type == AuraType::BUFF);
    REQUIRE(data.has_position == false);
}

TEST_CASE("SPELL_AURA_APPLIED DEBUFF parsing", "[parser][aura]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_AURA_APPLIED",
        "Player-1-00000001", "Warlock", "0x511", "0x0",
        "Creature-0-00000002", "Boss", "0xa48", "0x0",
        "172", "Corruption", "32",
        "DEBUFF"
    };

    auto data = parser::EventParser<parser::SpellAuraAppliedTag>::parse_and_return(tokens);

    REQUIRE(data.aura_type == AuraType::DEBUFF);
}

TEST_CASE("SPELL_AURA_REMOVED parsing", "[parser][aura]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_AURA_REMOVED",
        "Player-1-00000001", "Mage", "0x511", "0x0",
        "Player-1-00000001", "Mage", "0x511", "0x0",
        "12472", "Icy Veins", "16",
        "BUFF"
    };

    auto data = parser::EventParser<parser::SpellAuraRemovedTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 12472);
    REQUIRE(data.aura_type == AuraType::BUFF);
}

TEST_CASE("SPELL_AURA_REFRESH parsing", "[parser][aura]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_AURA_REFRESH",
        "Player-1-00000001", "Druid", "0x511", "0x0",
        "Player-1-00000002", "Tank", "0x514", "0x0",
        "774", "Rejuvenation", "8",
        "BUFF"
    };

    auto data = parser::EventParser<parser::SpellAuraRefreshTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 774);
    REQUIRE(data.spell.spell_name == "Rejuvenation");
}

TEST_CASE("SPELL_AURA_APPLIED_DOSE with stack count", "[parser][aura]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_AURA_APPLIED_DOSE",
        "Player-1-00000001", "Warlock", "0x511", "0x0",
        "Creature-0-00000002", "Boss", "0xa48", "0x0",
        "980", "Agony", "32",
        "DEBUFF", "5"  // 5 stacks
    };

    auto data = parser::EventParser<parser::SpellAuraAppliedDoseTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 980);
    REQUIRE(data.aura_type == AuraType::DEBUFF);
    REQUIRE(data.amount == 5);
}

TEST_CASE("SPELL_AURA_REMOVED_DOSE with stack count", "[parser][aura]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_AURA_REMOVED_DOSE",
        "Player-1-00000001", "Monk", "0x511", "0x0",
        "Creature-0-00000002", "Boss", "0xa48", "0x0",
        "113746", "Mystic Touch", "1",
        "DEBUFF", "3"
    };

    auto data = parser::EventParser<parser::SpellAuraRemovedDoseTag>::parse_and_return(tokens);

    REQUIRE(data.amount == 3);
}

TEST_CASE("SPELL_AURA_BROKEN_SPELL with extra spell info", "[parser][aura]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_AURA_BROKEN_SPELL",
        "Player-1-00000001", "Rogue", "0x511", "0x0",
        "Creature-0-00000002", "Enemy", "0xa48", "0x0",
        "1776", "Gouge", "1",  // The aura that was broken
        "5308", "Execute", "1",  // The spell that broke it
        "DEBUFF"
    };

    auto data = parser::EventParser<parser::SpellAuraBrokenSpellTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 1776);  // Aura that was broken
    REQUIRE(data.extra_spell_id == 5308);  // Spell that broke it
    REQUIRE(data.extra_spell_name == "Execute");
    REQUIRE(data.aura_type == AuraType::DEBUFF);
}

// Parser_Absorb.h Tests

TEST_CASE("SPELL_ABSORBED from spell attack", "[parser][absorb]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_ABSORBED",
        "Player-1-00000001", "Mage", "0x511", "0x0",
        "Player-1-00000002", "Target", "0x514", "0x0",
        // Absorbed spell (the attack)
        "133", "Fireball", "4",
        // Absorber info (4 tokens)
        "Player-1-00000002", "Target", "0x514", "0x0",
        // Absorb spell (the shield)
        "11426", "Ice Barrier", "16",
        "10000"  // Amount absorbed
    };

    auto data = parser::EventParser<parser::SpellAbsorbedTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 133);  // Fireball
    REQUIRE(data.absorber_name == "Target");
    REQUIRE(data.absorb_spell_id == 11426);  // Ice Barrier
    REQUIRE(data.absorb_spell_name == "Ice Barrier");
    REQUIRE(data.amount == 10000);
    REQUIRE(data.has_position == false);
}

TEST_CASE("SPELL_ABSORBED from swing attack", "[parser][absorb]") {
    // SWING absorb has 19 tokens (no spell prefix for the attack)
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_ABSORBED",
        "Creature-0-00000001", "Boss", "0xa48", "0x0",
        "Player-1-00000002", "Tank", "0x514", "0x0",
        // No absorbed spell - goes directly to absorber
        "Player-1-00000002", "Tank", "0x514", "0x0",
        "17", "Power Word: Shield", "2",
        "5000"
    };

    auto data = parser::EventParser<parser::SpellAbsorbedTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 0);  // No spell (melee)
    REQUIRE(data.spell.spell_name == "Melee");
    REQUIRE(data.absorb_spell_id == 17);
    REQUIRE(data.amount == 5000);
}

TEST_CASE("SPELL_ABSORBED_SUPPORT with supporter GUID", "[parser][absorb]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_ABSORBED_SUPPORT",
        "Player-1-00000001", "DPS", "0x511", "0x0",
        "Creature-0-00000002", "Boss", "0xa48", "0x0",
        "123456", "Big Attack", "1",
        "Player-1-00000003", "Augmentation", "0x514", "0x0",
        "17", "Power Word: Shield", "2",
        "8000", "Player-1-00000003"  // Supporter GUID at end
    };

    auto data = parser::EventParser<parser::SpellAbsorbedSupportTag>::parse_and_return(tokens);

    REQUIRE(data.amount == 8000);
    REQUIRE(data.supporter_guid == "Player-1-00000003");
}

TEST_CASE("Parser handles nil values in optional fields", "[parser][edge-case]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_DAMAGE",
        "Player-1-00000001", "TestPlayer", "0x511", "0x0",
        "Creature-0-00000002", "TestBoss", "0xa48", "0x0",
        "123456", "Fireball", "4",
        "Player-1-00000001", "0000000000000000",
        "50000", "100000", "1000", "2000", "500", "0",
        "0", "0", "0", "5000", "10000", "100",
        "1234.56", "-5678.90", "2222", "1.5708", "70",
        "10000", "12000", "-1", "4", "nil", "nil", "nil", "nil", "nil", "nil", "nil"
    };

    auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);

    // Nil values should parse as 0 or false
    REQUIRE(data.resisted == 0);
    REQUIRE(data.blocked == 0);
    REQUIRE(data.absorbed == 0);
    REQUIRE(data.critical == false);
    REQUIRE(data.glancing == false);
    REQUIRE(data.crushing == false);
}

// Parser_Cast.h Tests

TEST_CASE("SPELL_CAST_SUCCESS with full advanced data", "[parser][cast]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_CAST_SUCCESS",
        "Player-1-00000001", "Mage", "0x511", "0x0",
        "Creature-0-00000002", "Boss", "0xa48", "0x0",
        "133", "Fireball", "4",
        "Player-1-00000001", "0000000000000000",
        "50000", "100000", "1000", "2500", "500", "0",
        "0", "0", "0", "5000", "10000", "0",
        "1234.56", "-5678.90", "2222", "1.5708", "70"
    };

    auto data = parser::EventParser<parser::SpellCastSuccessTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 133);
    REQUIRE(data.spell.spell_name == "Fireball");
    REQUIRE(data.spell.spell_school == 4);
    REQUIRE(data.source_name == "Mage");
    REQUIRE(data.advanced_info.position.valid);
    REQUIRE(data.advanced_info.position.x_loc == Catch::Approx(1234.56f));
    REQUIRE(data.advanced_info.position.y_loc == Catch::Approx(-5678.90f));
}

TEST_CASE("SPELL_CAST_START basic parsing", "[parser][cast]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_CAST_START",
        "Player-1-00000001", "Warlock", "0x511", "0x0",
        "Creature-0-00000002", "Target", "0xa48", "0x0",
        "686", "Shadow Bolt", "32"
    };

    auto data = parser::EventParser<parser::SpellCastStartTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 686);
    REQUIRE(data.spell.spell_name == "Shadow Bolt");
    REQUIRE(data.spell.spell_school == 32);
    REQUIRE(data.advanced_info.position.valid == false);
}

TEST_CASE("SPELL_CAST_FAILED with fail reason", "[parser][cast]") {
    // From tests/testevents.txt: the spell school sits at 13, the reason at 14
    // 12/4/2025 21:37:15.548-7  SPELL_CAST_FAILED,Player-11-0E8ACC6C,"Wobblucy-Tichondrius-US",
    //   0x511,0x80000000,0000000000000000,nil,0x80000000,0x80000000,1079,"Rip",0x1,"Not yet recovered"
    const std::vector<std::string_view> tokens = {
        "12/4/2025", "21:37:15.548-7", "SPELL_CAST_FAILED",
        "Player-11-0E8ACC6C", "Wobblucy-Tichondrius-US", "0x511", "0x80000000",
        "0000000000000000", "nil", "0x80000000", "0x80000000",
        "1079", "Rip", "0x1", "Not yet recovered"
    };

    auto data = parser::EventParser<parser::SpellCastFailedTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 1079);
    REQUIRE(data.fail_reason == "Not yet recovered");
    REQUIRE(data.advanced_info.position.valid == false);
}

TEST_CASE("SPELL_EMPOWER_START basic parsing", "[parser][cast][empower]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_EMPOWER_START",
        "Player-1-00000001", "Evoker", "0x511", "0x0",
        "Creature-0-00000002", "Target", "0xa48", "0x0",
        "382266", "Fire Breath", "4"
    };

    auto data = parser::EventParser<parser::SpellEmpowerStartTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 382266);
    REQUIRE(data.spell.spell_name == "Fire Breath");
    REQUIRE(data.spell.spell_school == 4);
}

TEST_CASE("SPELL_EMPOWER_END basic parsing", "[parser][cast][empower]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.623", "SPELL_EMPOWER_END",
        "Player-1-00000001", "Evoker", "0x511", "0x0",
        "Creature-0-00000002", "Target", "0xa48", "0x0",
        "382266", "Fire Breath", "4",
        "3"
    };

    auto data = parser::EventParser<parser::SpellEmpowerEndTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 382266);
    REQUIRE(data.spell.spell_name == "Fire Breath");
}

TEST_CASE("SPELL_EMPOWER_INTERRUPT parsing", "[parser][cast][empower]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.323", "SPELL_EMPOWER_INTERRUPT",
        "Player-1-00000001", "Evoker", "0x511", "0x0",
        "Creature-0-00000002", "Target", "0xa48", "0x0",
        "382266", "Fire Breath", "4",
        "2"
    };

    auto data = parser::EventParser<parser::SpellEmpowerInterruptTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 382266);
}

// Parser_Resource.h Tests

TEST_CASE("SPELL_ENERGIZE with full data", "[parser][resource]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_ENERGIZE",
        "Player-1-00000001", "Rogue", "0x511", "0x0",
        "Player-1-00000001", "Rogue", "0x511", "0x0",
        "1752", "Sinister Strike", "1",
        "Player-1-00000001", "0000000000000000",
        "50000", "100000", "3000", "500", "1500", "0",
        "0", "0", "3", "80", "100", "35",
        "100.0", "200.0", "2222", "0.0", "70",
        "35", "0", "3", "nil"  // amount, overEnergize, powerType, altPowerType
    };

    auto data = parser::EventParser<parser::SpellEnergizeTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 1752);
    REQUIRE(data.spell.spell_name == "Sinister Strike");
    REQUIRE(data.amount == 35);
    REQUIRE(data.over_energize == 0);
    REQUIRE(data.power_type == static_cast<PowerType>(3));  // Energy
    REQUIRE(data.advanced_info.position.valid);
}

TEST_CASE("SPELL_PERIODIC_ENERGIZE parsing", "[parser][resource][periodic]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_PERIODIC_ENERGIZE",
        "Player-1-00000001", "Druid", "0x511", "0x0",
        "Player-1-00000001", "Druid", "0x511", "0x0",
        "5229", "Enrage", "1",
        "Player-1-00000001", "0000000000000000",
        "50000", "100000", "2000", "1500", "1000", "0",
        "0", "0", "1", "60", "100", "0",
        "100.0", "200.0", "2222", "0.0", "70",
        "20", "0", "1", "nil"  // Rage gain
    };

    auto data = parser::EventParser<parser::SpellPeriodicEnergizeTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 5229);
    REQUIRE(data.amount == 20);
    REQUIRE(data.power_type == static_cast<PowerType>(1));  // Rage
}

TEST_CASE("SPELL_DRAIN with power drain", "[parser][resource]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_DRAIN",
        "Player-1-00000001", "Warlock", "0x511", "0x0",
        "Creature-0-00000002", "Target", "0xa48", "0x0",
        "5138", "Drain Mana", "32",
        "Player-1-00000001", "0000000000000000",
        "50000", "100000", "1000", "2500", "500", "0",
        "0", "0", "0", "5000", "10000", "0",
        "100.0", "200.0", "2222", "0.0", "70",
        "500", "0", "0", "nil"  // Mana drain
    };

    auto data = parser::EventParser<parser::SpellDrainTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 5138);
    REQUIRE(data.amount == 500);
    REQUIRE(data.power_type == static_cast<PowerType>(0));  // Mana
}

TEST_CASE("SPELL_LEECH with resource gain", "[parser][resource]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_LEECH",
        "Player-1-00000001", "DeathKnight", "0x511", "0x0",
        "Creature-0-00000002", "Target", "0xa48", "0x0",
        "195182", "Marrowrend", "32",
        "Player-1-00000001", "0000000000000000",
        "50000", "100000", "3500", "1000", "2000", "0",
        "0", "0", "5", "80", "100", "0",
        "100.0", "200.0", "2222", "0.0", "70",
        "30", "0", "5", "nil"  // Runic Power gain
    };

    auto data = parser::EventParser<parser::SpellLeechTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 195182);
    REQUIRE(data.amount == 30);
    REQUIRE(data.power_type == static_cast<PowerType>(5));
}

// Parser_Dispel.h Tests

TEST_CASE("SPELL_DISPEL with BUFF removal", "[parser][dispel]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_DISPEL",
        "Player-1-00000001", "Priest", "0x511", "0x0",
        "Creature-0-00000002", "Enemy", "0xa48", "0x0",
        "527", "Purify", "2",
        "21562", "Power Word: Fortitude", "2",
        "BUFF"
    };

    auto data = parser::EventParser<parser::SpellDispelTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 527);  // Dispel spell
    REQUIRE(data.spell.spell_name == "Purify");
    REQUIRE(data.extra_spell_id == 21562);  // Spell that was dispelled
    REQUIRE(data.extra_spell_name == "Power Word: Fortitude");
    REQUIRE(data.aura_type == AuraType::BUFF);
}

TEST_CASE("SPELL_DISPEL with DEBUFF removal", "[parser][dispel]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_DISPEL",
        "Player-1-00000001", "Paladin", "0x511", "0x0",
        "Player-1-00000002", "Ally", "0x514", "0x0",
        "4987", "Cleanse", "2",
        "589", "Shadow Word: Pain", "32",
        "DEBUFF"
    };

    auto data = parser::EventParser<parser::SpellDispelTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 4987);
    REQUIRE(data.extra_spell_id == 589);
    REQUIRE(data.extra_spell_name == "Shadow Word: Pain");
    REQUIRE(data.aura_type == AuraType::DEBUFF);
}

TEST_CASE("SPELL_INTERRUPT basic parsing", "[parser][dispel]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_INTERRUPT",
        "Player-1-00000001", "Rogue", "0x511", "0x0",
        "Creature-0-00000002", "Caster", "0xa48", "0x0",
        "1766", "Kick", "1",
        "133", "Fireball", "4"
    };

    auto data = parser::EventParser<parser::SpellInterruptTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 1766);  // Kick
    REQUIRE(data.extra_spell_id == 133);   // Fireball (interrupted)
    REQUIRE(data.extra_spell_name == "Fireball");
}

TEST_CASE("SPELL_STOLEN buff theft", "[parser][dispel]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_STOLEN",
        "Player-1-00000001", "Mage", "0x511", "0x0",
        "Creature-0-00000002", "Enemy", "0xa48", "0x0",
        "30449", "Spellsteal", "64",
        "12472", "Icy Veins", "16",
        "BUFF"
    };

    auto data = parser::EventParser<parser::SpellStolenTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 30449);  // Spellsteal
    REQUIRE(data.extra_spell_id == 12472);  // Icy Veins (stolen)
    REQUIRE(data.extra_spell_name == "Icy Veins");
    REQUIRE(data.aura_type == AuraType::BUFF);
}

TEST_CASE("SPELL_EXTRA_ATTACKS parsing", "[parser][dispel]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_EXTRA_ATTACKS",
        "Player-1-00000001", "Shaman", "0x511", "0x0",
        "Creature-0-00000002", "Target", "0xa48", "0x0",
        "25504", "Windfury Attack", "8",
        "2"  // 2 extra attacks
    };

    auto data = parser::EventParser<parser::SpellExtraAttacksTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 25504);
    REQUIRE(data.spell.spell_name == "Windfury Attack");
    REQUIRE(data.amount == 2);
}

// Parser_Encounter.h Tests

TEST_CASE("ENCOUNTER_START basic parsing", "[parser][encounter]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "ENCOUNTER_START",
        "2499", "Raszageth the Storm-Eater", "16", "20"
    };

    auto data = parser::EventParser<parser::EncounterStartTag>::parse_and_return(tokens);

    REQUIRE(data.encounterId == 2499);
    REQUIRE(data.encounterName == "Raszageth the Storm-Eater");
    REQUIRE(data.difficultyId == 16);  // Mythic
    REQUIRE(data.groupSize == 20);
}

TEST_CASE("ENCOUNTER_END with success", "[parser][encounter]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:35:22.456", "ENCOUNTER_END",
        "2499", "Raszageth the Storm-Eater", "16", "20", "1"
    };

    auto data = parser::EventParser<parser::EncounterEndTag>::parse_and_return(tokens);

    REQUIRE(data.encounterId == 2499);
    REQUIRE(data.encounterName == "Raszageth the Storm-Eater");
    REQUIRE(data.difficultyId == 16);
    REQUIRE(data.groupSize == 20);
    REQUIRE(data.success == 1);  // Kill
}

TEST_CASE("ENCOUNTER_END with wipe", "[parser][encounter]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:30:15.789", "ENCOUNTER_END",
        "2499", "Raszageth the Storm-Eater", "16", "20", "0"
    };

    auto data = parser::EventParser<parser::EncounterEndTag>::parse_and_return(tokens);

    REQUIRE(data.success == 0);  // Wipe
}

// Parser_ChallengeMode.h Tests

TEST_CASE("CHALLENGE_MODE_START basic parsing", "[parser][mythicplus]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "CHALLENGE_MODE_START",
        "Mists of Tirna Scithe", "2290", "375", "15",
        "[9,122,4,121]"
    };

    auto data = parser::EventParser<parser::ChallengeModeStartTag>::parse_and_return(tokens);

    REQUIRE(data.zoneName == "Mists of Tirna Scithe");
    REQUIRE(data.instanceId == 2290);
    REQUIRE(data.challengeModeId == 375);
    REQUIRE(data.keystoneLevel == 15);
    REQUIRE(data.affixIds.size() == 4);
    REQUIRE(data.affixIds[0] == 9);
    REQUIRE(data.affixIds[1] == 122);
}

TEST_CASE("CHALLENGE_MODE_END with completion", "[parser][mythicplus]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:45:30.456", "CHALLENGE_MODE_END",
        "2290", "1", "15", "1245678"
    };

    auto data = parser::EventParser<parser::ChallengeModeEndTag>::parse_and_return(tokens);

    REQUIRE(data.instanceId == 2290);
    REQUIRE(data.success == 1);  // Completed in time
    REQUIRE(data.keystoneLevel == 15);
    REQUIRE(data.totalTime_ms == 1245678);
}

TEST_CASE("CHALLENGE_MODE_END with failure", "[parser][mythicplus]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "15:00:00.000", "CHALLENGE_MODE_END",
        "2290", "0", "15", "1800000"
    };

    auto data = parser::EventParser<parser::ChallengeModeEndTag>::parse_and_return(tokens);

    REQUIRE(data.success == 0);  // Failed to time
}

// Parser_Unit.h Tests

TEST_CASE("UNIT_DIED basic parsing", "[parser][unit]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "UNIT_DIED",
        "nil", "nil", "0x0", "0x0",
        "Player-1-00000001", "Unlucky", "0x511", "0x0",
        "12345"
    };

    auto data = parser::EventParser<parser::UnitDiedTag>::parse_and_return(tokens);

    REQUIRE(data.dest_name == "Unlucky");
    REQUIRE(data.dest_guid == "Player-1-00000001");
    REQUIRE(data.recap_id == 12345);
    REQUIRE(data.unconscious_on_death == false);
}

TEST_CASE("PARTY_KILL parsing", "[parser][unit]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "PARTY_KILL",
        "Player-1-00000001", "Warrior", "0x511", "0x0",
        "Creature-0-00000002", "Boss", "0xa48", "0x0",
        "67890"
    };

    auto data = parser::EventParser<parser::PartyKillTag>::parse_and_return(tokens);

    REQUIRE(data.source_name == "Warrior");
    REQUIRE(data.dest_name == "Boss");
    REQUIRE(data.recap_id == 67890);
}

TEST_CASE("SPELL_SUMMON parsing", "[parser][unit]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_SUMMON",
        "Player-1-00000001", "Warlock", "0x511", "0x0",
        "Pet-0-00000003", "Felhunter", "0x1114", "0x0",
        "691", "Summon Felhunter", "32"
    };

    auto data = parser::EventParser<parser::SpellSummonTag>::parse_and_return(tokens);

    REQUIRE(data.source_name == "Warlock");
    REQUIRE(data.dest_name == "Felhunter");
    REQUIRE(data.spell.spell_id == 691);
    REQUIRE(data.spell.spell_name == "Summon Felhunter");
}

TEST_CASE("SPELL_CREATE parsing", "[parser][unit]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_CREATE",
        "Player-1-00000001", "Mage", "0x511", "0x0",
        "GameObject-0-00000002", "Mana Gem", "0xa48", "0x0",
        "759", "Conjure Mana Gem", "64"
    };

    auto data = parser::EventParser<parser::SpellCreateTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 759);
    REQUIRE(data.spell.spell_name == "Conjure Mana Gem");
}

TEST_CASE("SPELL_RESURRECT parsing", "[parser][unit]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_RESURRECT",
        "Player-1-00000001", "Priest", "0x511", "0x0",
        "Player-1-00000002", "DeadGuy", "0x514", "0x0",
        "2006", "Resurrection", "2"
    };

    auto data = parser::EventParser<parser::SpellResurrectTag>::parse_and_return(tokens);

    REQUIRE(data.source_name == "Priest");
    REQUIRE(data.dest_name == "DeadGuy");
    REQUIRE(data.spell.spell_id == 2006);
    REQUIRE(data.spell.spell_name == "Resurrection");
}

TEST_CASE("ENVIRONMENTAL_DAMAGE from Falling", "[parser][unit]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "ENVIRONMENTAL_DAMAGE",
        "nil", "nil", "0x0", "0x0",
        "Player-1-00000001", "Clumsy", "0x511", "0x0",
        "Falling", "15000", "0", "1", "0", "0", "0", "nil"
    };

    auto data = parser::EventParser<parser::EnvironmentalDamageTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_name == "Falling");
    REQUIRE(data.amount == 15000);
    REQUIRE(data.dest_name == "Clumsy");
    REQUIRE(data.spell.spell_school == SpellSchool::Physical);
}

TEST_CASE("ENVIRONMENTAL_DAMAGE from Fire", "[parser][unit]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "ENVIRONMENTAL_DAMAGE",
        "nil", "nil", "0x0", "0x0",
        "Player-1-00000001", "Burned", "0x511", "0x0",
        "Fire", "3000", "0", "4", "0", "0", "0", "nil"
    };

    auto data = parser::EventParser<parser::EnvironmentalDamageTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_name == "Fire");
    REQUIRE(data.amount == 3000);
    REQUIRE(data.spell.spell_school == SpellSchool::Fire);
}

TEST_CASE("ENVIRONMENTAL_DAMAGE with advanced logging", "[parser][unit]") {
    // From tests/testevents.txt. The advanced unit block comes before the
    // type, as on SWING_DAMAGE, so the type is not at 11 here.
    const std::string line =
        "12/4/2025 21:32:50.917-7  ENVIRONMENTAL_DAMAGE,0000000000000000,nil,0x80000000,0x80000000,"
        "Player-11-0E96012E,\"C-Tichondrius-US\",0x514,0x80000000,Player-11-0E96012E,0000000000000000,"
        "15804142,18572241,34385,132084,125542,1746,300,0,11,0,125,0,2045.82,3611.50,2467,4.7341,728,"
        "Falling,1109968,1109968,0,1,0,0,0,nil,nil,nil\n";
    tokenized_segment tokenizer;
    auto lines = tokenizer.tokenize(line.data(), line.size());
    REQUIRE(lines.size() == 1);

    auto data = parser::EventParser<parser::EnvironmentalDamageTag>::parse_and_return(lines[0]);

    CHECK(data.spell.spell_name == "Falling");
    CHECK(data.amount == 1109968);
    CHECK(data.mitigated_amount == 1109968);
    CHECK(data.overkill == 0);
    CHECK(data.dest_guid == "Player-11-0E96012E");
    CHECK(data.advanced_info.current_hp == 15804142);
    CHECK(data.advanced_info.max_hp == 18572241);
    REQUIRE(data.advanced_info.position.valid);
    CHECK(data.advanced_info.position.x_loc == Catch::Approx(2045.82f));
    CHECK(data.advanced_info.position.map_id == 2467);
}

// Parser_Support.h Tests

TEST_CASE("SPELL_DAMAGE_SUPPORT with augmentation tracking", "[parser][support]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_DAMAGE_SUPPORT",
        "Player-1-00000001", "DPS", "0x511", "0x0",
        "Creature-0-00000002", "Boss", "0xa48", "0x0",
        "133", "Fireball", "4",
        "Player-1-00000001", "0000000000000000",
        "50000", "100000", "1000", "2500", "500", "0",
        "0", "0", "0", "5000", "10000", "0",
        "100.0", "200.0", "2222", "0.0", "70",
        "12000", "15000", "-1", "4", "0", "0", "0", "1", "nil", "nil",
        "Player-1-00000003"  // Augmentation Evoker GUID
    };

    auto data = parser::EventParser<parser::SpellDamageSupportTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 133);
    REQUIRE(data.mitigated_amount == 12000);
    REQUIRE(data.amount == 15000);
    REQUIRE(data.critical == true);
    REQUIRE(data.supporter_guid == "Player-1-00000003");
}

TEST_CASE("SPELL_HEAL_SUPPORT with augmentation tracking", "[parser][support]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "SPELL_HEAL_SUPPORT",
        "Player-1-00000001", "Healer", "0x511", "0x0",
        "Player-1-00000002", "Tank", "0x514", "0x0",
        "2061", "Flash Heal", "2",
        "Player-1-00000001", "0000000000000000",
        "50000", "100000", "1000", "3500", "500", "0",
        "0", "0", "0", "8000", "10000", "500",
        "100.0", "200.0", "2222", "0.0", "70",
        "18000", "17000", "2000", "0", "1",
        "Player-1-00000003"  // Supporter
    };

    auto data = parser::EventParser<parser::SpellHealSupportTag>::parse_and_return(tokens);

    REQUIRE(data.spell.spell_id == 2061);
    REQUIRE(data.amount == 18000);
    REQUIRE(data.overhealing == 2000);
    REQUIRE(data.critical == true);
    REQUIRE(data.supporter_guid == "Player-1-00000003");
}

// Parser_Meta.h Tests

TEST_CASE("COMBAT_LOG_VERSION parsing", "[parser][meta]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "COMBAT_LOG_VERSION",
        "23", "1", "11.0.5.57212", "1"
    };

    auto data = parser::EventParser<parser::CombatLogVersionTag>::parse_and_return(tokens);

    REQUIRE(data.version == 23);
    REQUIRE(data.advanced_log_enabled == 1);
    REQUIRE(data.build_version == "11.0.5.57212");
    REQUIRE(data.project_id == 1);
}

TEST_CASE("ZONE_CHANGE parsing", "[parser][meta]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "ZONE_CHANGE",
        "2522", "Vault of the Incarnates", "16"
    };

    auto data = parser::EventParser<parser::ZoneChangeTag>::parse_and_return(tokens);

    REQUIRE(data.zone_id == 2522);
    REQUIRE(data.zone_name == "Vault of the Incarnates");
    REQUIRE(data.difficulty_id == 16);  // Mythic
}

TEST_CASE("MAP_CHANGE parsing", "[parser][meta]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "MAP_CHANGE",
        "2522", "Vault of the Incarnates",
        "-1234.56", "5678.90", "9876.54", "-3210.12"
    };

    auto data = parser::EventParser<parser::MapChangeTag>::parse_and_return(tokens);

    REQUIRE(data.map_id == 2522);
    REQUIRE(data.map_name == "Vault of the Incarnates");
    REQUIRE(data.x1 == Catch::Approx(-1234.56f));
    REQUIRE(data.y1 == Catch::Approx(5678.90f));
    REQUIRE(data.x2 == Catch::Approx(9876.54f));
    REQUIRE(data.y2 == Catch::Approx(-3210.12f));
}

// Parser_Combatant_Info.h Tests

TEST_CASE("COMBATANT_INFO WoW 11.x format basic parsing", "[parser][combatant]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "COMBATANT_INFO",
        "Player-1-00000001",  // playerGUID
        "1",  // faction
        // 22 stats
        "5000", "3000", "8000", "2500",  // Str, Agi, Sta, Int
        "200", "150", "100",              // Dodge, Parry, Block
        "2500", "2500", "2500",           // Crit Melee/Ranged/Spell
        "100",                            // Speed
        "50",                             // Lifesteal
        "1500", "1500", "1500",           // Haste Melee/Ranged/Spell
        "150",                            // Avoidance
        "800",                            // Mastery
        "1000", "1000", "500",            // Versatility DD/HD/DT
        "3500",                           // Armor
        "577",  // specID (Havoc DH)
        "[(117,198793,1),(118,258920,1)]"  // Talents
    };

    auto data = parser::EventParser<parser::CombatantInfoTag>::parse_and_return(tokens);

    REQUIRE(data.player_guid == "Player-1-00000001");
    REQUIRE(data.spec_id == 577);  // Havoc Demon Hunter
    REQUIRE(data.haste_melee == 1500);
    REQUIRE(data.haste_ranged == 1500);
    REQUIRE(data.haste_spell == 1500);
    REQUIRE(data.talents.size() == 2);
    REQUIRE(data.talents[0].talent_id == 117);
    REQUIRE(data.talents[0].spell_id == 198793);
    REQUIRE(data.talents[0].rank == 1);
}

TEST_CASE("COMBATANT_INFO WoW 12.0 format with extra stat field", "[parser][combatant]") {
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "COMBATANT_INFO",
        "Player-1-00000001",  // playerGUID
        "1",  // faction
        // 23 stats (12.0 added UnknownField12_0 after Armor)
        "5000", "3000", "8000", "2500",
        "200", "150", "100",
        "2500", "2500", "2500",
        "100",
        "50",
        "1500", "1500", "1500",
        "150",
        "800",
        "1000", "1000", "500",
        "3500",                           // Armor
        "0",                              // UnknownField12_0 (new in 12.0)
        "62",  // specID (Arcane Mage)
        "[(100,1449,1)]"  // Talents
    };

    auto data = parser::EventParser<parser::CombatantInfoTag>::parse_and_return(tokens);

    REQUIRE(data.player_guid == "Player-1-00000001");
    REQUIRE(data.spec_id == 62);  // Arcane Mage
    REQUIRE(data.haste_spell == 1500);
    REQUIRE(data.talents.size() == 1);
    REQUIRE(data.talents[0].talent_id == 100);
}

TEST_CASE("COMBATANT_INFO spec id is the field before the talents", "[parser][combatant]") {
    // An armor value that is also a valid spec id (64, Frost Mage) must not
    // be taken for the spec.
    const std::vector<std::string_view> tokens = {
        "1/15/2026", "14:23:45.123", "COMBATANT_INFO",
        "Player-1-00000001", "1",
        "5000", "3000", "8000", "2500",
        "200", "150", "100",
        "2500", "2500", "2500",
        "100",
        "50",
        "1500", "1500", "1500",
        "150",
        "800",
        "1000", "1000", "500",
        "64",                             // Armor
        "0",                              // UnknownField12_0
        "62",                             // specID (Arcane Mage)
        "[(100,1449,1)]"
    };
    CHECK(parser::EventParser<parser::CombatantInfoTag>::extract_spec_id(tokens) == 62);

    // Real lines from tests/testevents.txt, talent arrays cut short.
    struct RealLine { std::string text; uint16_t spec; };
    const std::vector<RealLine> lines = {
        { "12/4/2025 21:32:21.665-7  COMBATANT_INFO,Player-11-0E745E05,1,7763,12001,822363,"
          "139892,0,0,0,4706,4706,4706,0,1618,37960,37960,37960,3348,13931,6147,6147,6147,"
          "30271,63,[(62085,80141,1)]\n", 63 },
        { "12/4/2025 21:33:42.236-7  COMBATANT_INFO,Player-11-0E1C48C3,0,105801,14649,925641,"
          "9811,0,0,0,15676,15676,15676,3086,0,16652,16652,16652,0,24496,2874,2874,2874,"
          "105701,251,[(76033,96161,2)]\n", 251 },
        { "12/4/2025 21:33:42.236-7  COMBATANT_INFO,Player-11-0BD1D46D,0,18353,6178,876882,"
          "157083,0,0,0,10858,10858,10858,1612,1709,20758,20758,20758,0,16525,11679,11679,11679,"
          "186171,65,[(81496,102465,1)]\n", 65 },
    };
    for (const auto& real : lines) {
        tokenized_segment tokenizer;
        auto parsed = tokenizer.tokenize(real.text.data(), real.text.size());
        REQUIRE(parsed.size() == 1);
        CHECK(parser::EventParser<parser::CombatantInfoTag>::parse_and_return(parsed[0]).spec_id
              == real.spec);
    }
}

// Multi-resource power parsing
// Advanced logs pack every resource the spec has into four pipe-separated
// fields. Patterns below are taken from real 12.1 logs.

namespace {

// SPELL_DAMAGE tokens with the four power fields swapped in, so each test
// only has to state the part it cares about.
std::vector<std::string_view> damageTokensWithPower(std::string_view types,
                                                    std::string_view current,
                                                    std::string_view max,
                                                    std::string_view cost) {
    return {
        "1/15/2026", "14:23:45.123", "SPELL_DAMAGE",
        "Player-1-00000001", "TestPlayer", "0x511", "0x0",
        "Creature-0-00000002", "TestBoss", "0xa48", "0x0",
        "123456", "Fireball", "4",
        "Player-1-00000001", "0000000000000000",
        "50000", "100000", "1000", "2000", "500", "0",
        "0", "0", types, current, max, cost,
        "1234.56", "-5678.90", "2222", "1.5708", "70",
        "10000", "12000", "-1", "4", "0", "0", "2000", "1", "nil", "nil", "nil"
    };
}

} // namespace

TEST_CASE("single resource still parses into the flat fields", "[parser][power]") {
    auto tokens = damageTokensWithPower("0", "5000", "10000", "100");
    auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);
    const auto& info = data.advanced_info;

    REQUIRE(info.power_count == 1);
    CHECK(info.power_type == 0);
    CHECK(info.current_power == 5000);
    CHECK(info.max_power == 10000);
    CHECK(info.power_cost == 100);

    CHECK(info.powers[0].type == 0);
    CHECK(info.powers[0].current == 5000);
    CHECK(info.powers[0].max == 10000);
    CHECK(info.powers[0].cost == 100);
}

TEST_CASE("energy and combo points both survive", "[parser][power]") {
    // Real rogue/feral pattern: "3|4,85|7,100|7,30|7"
    auto tokens = damageTokensWithPower("3|4", "85|7", "100|7", "30|7");
    auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);
    const auto& info = data.advanced_info;

    REQUIRE(info.power_count == 2);

    // Entry 0 is Energy, and the flat fields must still mirror it - the
    // cooldown reduction lookup keys on these
    CHECK(info.power_type == 3);
    CHECK(info.current_power == 85);
    CHECK(info.max_power == 100);
    CHECK(info.power_cost == 30);

    // Entry 1 is the combo points that used to be thrown away
    CHECK(info.powers[1].type == 4);
    CHECK(info.powers[1].current == 7);
    CHECK(info.powers[1].max == 7);
    CHECK(info.powers[1].cost == 7);
}

TEST_CASE("holy power and mana both survive", "[parser][power]") {
    // Real paladin pattern: "9|0"
    auto tokens = damageTokensWithPower("9|0", "3|48000", "5|50000", "0|2400");
    auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);
    const auto& info = data.advanced_info;

    REQUIRE(info.power_count == 2);
    CHECK(info.power_type == 9);
    CHECK(info.current_power == 3);
    CHECK(info.powers[1].type == 0);
    CHECK(info.powers[1].current == 48000);
    CHECK(info.powers[1].cost == 2400);
}

TEST_CASE("power entries beyond the cap are dropped, not overflowed", "[parser][power]") {
    // Five resources against MAX_POWER_TYPES of 4
    auto tokens = damageTokensWithPower("0|1|3|4|6", "1|2|3|4|5",
                                        "10|20|30|40|50", "0|0|0|0|0");
    auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);
    const auto& info = data.advanced_info;

    REQUIRE(info.power_count == AdvancedUnitInfo::MAX_POWER_TYPES);
    CHECK(info.powers[3].type == 4);
    CHECK(info.powers[3].current == 4);
}

TEST_CASE("ragged power fields do not read past their end", "[parser][power]") {
    // Two types but only one value in the others - a malformed line should
    // yield zeros for the missing parts rather than reusing entry 0
    auto tokens = damageTokensWithPower("3|4", "85", "100", "30");
    auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);
    const auto& info = data.advanced_info;

    REQUIRE(info.power_count == 2);
    CHECK(info.powers[0].current == 85);
    CHECK(info.powers[1].type == 4);
    CHECK(info.powers[1].current == 0);
    CHECK(info.powers[1].max == 0);
}

TEST_CASE("empty power field yields no entries", "[parser][power]") {
    auto tokens = damageTokensWithPower("", "", "", "");
    auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);
    const auto& info = data.advanced_info;

    CHECK(info.power_count == 0);
    // Flat fields keep their defaults when there is nothing to mirror
    CHECK(info.power_type == -1);
    CHECK(info.current_power == 0);
}

TEST_CASE("negative power type is preserved", "[parser][power]") {
    // The game writes -1 for units with no resource
    auto tokens = damageTokensWithPower("-1", "0", "0", "0");
    auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);

    REQUIRE(data.advanced_info.power_count == 1);
    CHECK(data.advanced_info.power_type == -1);
}

// EncounterScanner: keys with no CHALLENGE_MODE_END

namespace {

// WoW often writes no CHALLENGE_MODE_END for a key that was left. The raid
// pull that follows must still be listed, and so must the key.
const std::string kAbandonedKeyThenRaid =
    "5/1/2025 20:00:00.000-6  CHALLENGE_MODE_START,\"Mists of Tirna Scithe\",2290,375,11,[9,122]\n"
    "5/1/2025 20:01:00.000-6  ENCOUNTER_START,2397,\"Ingra Maloch\",8,5,2290\n"
    "5/1/2025 20:02:00.000-6  ENCOUNTER_END,2397,\"Ingra Maloch\",8,5,1,60000\n"
    "5/1/2025 21:00:00.000-6  ENCOUNTER_START,3009,\"Raid Boss\",16,20,2769\n"
    "5/1/2025 21:05:00.000-6  ENCOUNTER_END,3009,\"Raid Boss\",16,20,1,300000\n";

void checkKeyThenRaid(const std::vector<EncounterSegment>& segments) {
    REQUIRE(segments.size() == 2);
    CHECK(segments[0].segmentType == SegmentType::MythicPlusDungeon);
    CHECK(segments[0].encounterName == "Mists of Tirna Scithe");
    CHECK_FALSE(segments[0].isSuccess);
    CHECK(segments[0].endLineIndex == 2);
    CHECK(segments[1].segmentType == SegmentType::BossEncounter);
    CHECK(segments[1].encounterName == "Raid Boss");
    CHECK(segments[1].isSuccess);
}

} // namespace

TEST_CASE("an abandoned key does not hide the raid pull after it", "[scanner][mythicplus]") {
    const std::string& log = kAbandonedKeyThenRaid;

    SECTION("lightweight scan") {
        auto segments = EncounterScanner::scanForEncountersLightweight(log.data(), log.size());
        checkKeyThenRaid(segments);
        // The key's byte window stops where the raid pull starts.
        CHECK(segments[0].startByteOffset == 0);
        CHECK(segments[0].endByteOffset == log.find("5/1/2025 21:00:00"));
        CHECK(segments[0].endTimestamp_ms == 60 * 60 * 1000);
    }
    SECTION("tokenized scan") {
        tokenized_segment tokenizer;
        auto lines = tokenizer.tokenize(log.data(), log.size());
        checkKeyThenRaid(EncounterScanner::scanForEncounters(lines));
    }
}

TEST_CASE("a zone change closes an open key", "[scanner][mythicplus]") {
    const std::string log =
        "5/1/2025 20:00:00.000-6  CHALLENGE_MODE_START,\"Mists of Tirna Scithe\",2290,375,11,[9,122]\n"
        "5/1/2025 20:05:00.000-6  ZONE_CHANGE,2290,\"Mists of Tirna Scithe\",8\n"
        "5/1/2025 20:10:00.000-6  ZONE_CHANGE,2552,\"Khaz Algar\",0\n"
        "5/1/2025 20:20:00.000-6  CHALLENGE_MODE_END,2290,1,11,1200000\n";
    auto segments = EncounterScanner::scanForEncountersLightweight(log.data(), log.size());
    REQUIRE(segments.size() == 1);
    CHECK(segments[0].segmentType == SegmentType::MythicPlusDungeon);
    CHECK_FALSE(segments[0].isSuccess);
    // The zone change into the same dungeon kept it open; the one out closed it.
    CHECK(segments[0].endByteOffset == log.find("5/1/2025 20:10:00"));
    CHECK(segments[0].endTimestamp_ms == 10 * 60 * 1000);
}

TEST_CASE("a key still open at the end of the log is listed", "[scanner][mythicplus]") {
    const std::string log =
        "5/1/2025 20:00:00.000-6  CHALLENGE_MODE_START,\"Mists of Tirna Scithe\",2290,375,11,[9,122]\n"
        "5/1/2025 20:01:00.000-6  ENCOUNTER_START,2397,\"Ingra Maloch\",8,5,2290\n"
        "5/1/2025 20:02:00.000-6  ENCOUNTER_END,2397,\"Ingra Maloch\",8,5,1,60000\n"
        "5/1/2025 20:07:30.000-6  SPELL_CAST_SUCCESS,Player-1-AAAA,\"P\",0x511,0x0,"
        "0000000000000000,nil,0x80000000,0x80000000,2061,\"Flash Heal\",0x2\n";

    SECTION("lightweight scan") {
        auto segments = EncounterScanner::scanForEncountersLightweight(log.data(), log.size());
        REQUIRE(segments.size() == 1);
        CHECK(segments[0].segmentType == SegmentType::MythicPlusDungeon);
        CHECK_FALSE(segments[0].isSuccess);
        CHECK(segments[0].endByteOffset == log.size());
        CHECK(segments[0].endTimestamp_ms == (7 * 60 + 30) * 1000);
    }
    SECTION("tokenized scan") {
        tokenized_segment tokenizer;
        auto lines = tokenizer.tokenize(log.data(), log.size());
        auto segments = EncounterScanner::scanForEncounters(lines);
        REQUIRE(segments.size() == 1);
        CHECK(segments[0].segmentType == SegmentType::MythicPlusDungeon);
        CHECK(segments[0].endLineIndex == 3);
    }
}

TEST_CASE("Arena and world marker events anchor the log start", "[parser][meta]") {
    // When one of these is the first event of the log, the start is taken
    // from it, so its own relative time is 0 rather than a truncated
    // absolute time.
    auto& log_start = parser::get_log_start_timestamp_ms();
    const uint64_t saved = log_start;
    log_start = 0;

    const std::vector<std::string_view> marker = {
        "1/15/2026", "14:23:45.123", "WORLD_MARKER_PLACED", "1", "100.5", "-200.25"
    };
    auto placed = parser::EventParser<parser::WorldMarkerPlacedTag>::parse_and_return(marker);
    CHECK(placed.timestamp_ms == 0);
    CHECK(log_start != 0);

    const std::vector<std::string_view> arena = {
        "1/15/2026", "14:23:50.123", "ARENA_MATCH_START", "1672", "Blade's Edge Arena", "3", "1"
    };
    auto start = parser::EventParser<parser::ArenaMatchStartTag>::parse_and_return(arena);
    CHECK(start.timestamp_ms == 5000);

    const std::vector<std::string_view> removed = {
        "1/15/2026", "14:23:51.123", "WORLD_MARKER_REMOVED", "1"
    };
    CHECK(parser::EventParser<parser::WorldMarkerRemovedTag>::parse_and_return(removed).timestamp_ms == 6000);

    const std::vector<std::string_view> arenaEnd = {
        "1/15/2026", "14:23:52.123", "ARENA_MATCH_END", "0", "120", "1500", "1480"
    };
    CHECK(parser::EventParser<parser::ArenaMatchEndTag>::parse_and_return(arenaEnd).timestamp_ms == 7000);

    log_start = saved;
}

TEST_CASE("PowerType names match the ids the log writes", "[parser][resource]") {
    // Parsers cast the log's id straight into the enum, so each name has to
    // carry its log id or code comparing against a name reads the wrong one.
    STATIC_REQUIRE(static_cast<int>(PowerType::HEALTH) == -2);
    STATIC_REQUIRE(static_cast<int>(PowerType::MANA) == 0);
    STATIC_REQUIRE(static_cast<int>(PowerType::RAGE) == 1);
    STATIC_REQUIRE(static_cast<int>(PowerType::ENERGY) == 3);
    STATIC_REQUIRE(static_cast<int>(PowerType::HOLY_POWER) == 9);
    STATIC_REQUIRE(static_cast<int>(PowerType::FURY) == 17);
    STATIC_REQUIRE(static_cast<int>(PowerType::ESSENCE) == 19);
    STATIC_REQUIRE(static_cast<int>(PowerType::ALL) == 127);

    EnergizeEventData data{};
    CHECK(static_cast<int>(data.power_type) == 0);
}
