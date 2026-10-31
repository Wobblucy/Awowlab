#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "../AwowLab/Core/Utils/ColorUtils.h"
#include "../AwowLab/Parser/Include/Structures/Flags.h"

using Catch::Approx;

// hsvToRgb Tests

TEST_CASE("hsvToRgb - Pure red (h=0)", "[color][hsvToRgb]") {
    auto color = awow::hsvToRgb(0.0f, 1.0f, 1.0f);

    REQUIRE(color.r == Approx(1.0f).margin(0.001f));
    REQUIRE(color.g == Approx(0.0f).margin(0.001f));
    REQUIRE(color.b == Approx(0.0f).margin(0.001f));
}

TEST_CASE("hsvToRgb - Pure green (h=0.333)", "[color][hsvToRgb]") {
    auto color = awow::hsvToRgb(1.0f / 3.0f, 1.0f, 1.0f);

    REQUIRE(color.r == Approx(0.0f).margin(0.001f));
    REQUIRE(color.g == Approx(1.0f).margin(0.001f));
    REQUIRE(color.b == Approx(0.0f).margin(0.001f));
}

TEST_CASE("hsvToRgb - Pure blue (h=0.666)", "[color][hsvToRgb]") {
    auto color = awow::hsvToRgb(2.0f / 3.0f, 1.0f, 1.0f);

    REQUIRE(color.r == Approx(0.0f).margin(0.001f));
    REQUIRE(color.g == Approx(0.0f).margin(0.001f));
    REQUIRE(color.b == Approx(1.0f).margin(0.001f));
}

TEST_CASE("hsvToRgb - Yellow (h=0.166)", "[color][hsvToRgb]") {
    auto color = awow::hsvToRgb(1.0f / 6.0f, 1.0f, 1.0f);

    REQUIRE(color.r == Approx(1.0f).margin(0.001f));
    REQUIRE(color.g == Approx(1.0f).margin(0.001f));
    REQUIRE(color.b == Approx(0.0f).margin(0.001f));
}

TEST_CASE("hsvToRgb - Cyan (h=0.5)", "[color][hsvToRgb]") {
    auto color = awow::hsvToRgb(0.5f, 1.0f, 1.0f);

    REQUIRE(color.r == Approx(0.0f).margin(0.001f));
    REQUIRE(color.g == Approx(1.0f).margin(0.001f));
    REQUIRE(color.b == Approx(1.0f).margin(0.001f));
}

TEST_CASE("hsvToRgb - Magenta (h=0.833)", "[color][hsvToRgb]") {
    auto color = awow::hsvToRgb(5.0f / 6.0f, 1.0f, 1.0f);

    REQUIRE(color.r == Approx(1.0f).margin(0.001f));
    REQUIRE(color.g == Approx(0.0f).margin(0.001f));
    REQUIRE(color.b == Approx(1.0f).margin(0.001f));
}

TEST_CASE("hsvToRgb - Black (v=0)", "[color][hsvToRgb]") {
    auto color = awow::hsvToRgb(0.5f, 1.0f, 0.0f);

    REQUIRE(color.r == Approx(0.0f).margin(0.001f));
    REQUIRE(color.g == Approx(0.0f).margin(0.001f));
    REQUIRE(color.b == Approx(0.0f).margin(0.001f));
}

TEST_CASE("hsvToRgb - White (s=0, v=1)", "[color][hsvToRgb]") {
    auto color = awow::hsvToRgb(0.0f, 0.0f, 1.0f);

    REQUIRE(color.r == Approx(1.0f).margin(0.001f));
    REQUIRE(color.g == Approx(1.0f).margin(0.001f));
    REQUIRE(color.b == Approx(1.0f).margin(0.001f));
}

TEST_CASE("hsvToRgb - Gray (s=0, v=0.5)", "[color][hsvToRgb]") {
    auto color = awow::hsvToRgb(0.7f, 0.0f, 0.5f);

    // When saturation is 0, hue doesn't matter - should get gray
    REQUIRE(color.r == Approx(0.5f).margin(0.001f));
    REQUIRE(color.g == Approx(0.5f).margin(0.001f));
    REQUIRE(color.b == Approx(0.5f).margin(0.001f));
}

TEST_CASE("hsvToRgb - Desaturated red (s=0.5)", "[color][hsvToRgb]") {
    auto color = awow::hsvToRgb(0.0f, 0.5f, 1.0f);

    // Partial saturation should produce lighter red
    REQUIRE(color.r == Approx(1.0f).margin(0.001f));
    REQUIRE(color.g == Approx(0.5f).margin(0.001f));
    REQUIRE(color.b == Approx(0.5f).margin(0.001f));
}

TEST_CASE("hsvToRgb - Dark green (v=0.5)", "[color][hsvToRgb]") {
    auto color = awow::hsvToRgb(1.0f / 3.0f, 1.0f, 0.5f);

    REQUIRE(color.r == Approx(0.0f).margin(0.001f));
    REQUIRE(color.g == Approx(0.5f).margin(0.001f));
    REQUIRE(color.b == Approx(0.0f).margin(0.001f));
}

// parseSpawnIndex Tests

TEST_CASE("parseSpawnIndex - Valid creature GUID", "[color][parseSpawnIndex]") {
    std::string_view guid = "Creature-0-3886-2660-16035-216336-00104282A4";

    // Extract last segment after last dash: "00104282A4"
    // Take first 5 chars: "00104"
    // Parse as hex: 0x00104 = 260
    // Apply masking: (260 & 0xFFFF8) >> 3 = (256 & 0xFFFF8) >> 3 = 256 >> 3 = 32
    uint32_t index = awow::parseSpawnIndex(guid);

    REQUIRE(index == 32);
}

TEST_CASE("parseSpawnIndex - Different spawn UID", "[color][parseSpawnIndex]") {
    std::string_view guid = "Creature-0-3886-2660-16035-216336-ABCDEF1234";

    // First 5 chars of "ABCDEF1234": "ABCDE"
    // Parse as hex: 0xABCDE = 703710
    // Apply masking: (703710 & 0xFFFF8) >> 3 = 703704 >> 3 = 87963
    uint32_t index = awow::parseSpawnIndex(guid);

    REQUIRE(index == 87963);
}

TEST_CASE("parseSpawnIndex - Empty string returns 0", "[color][parseSpawnIndex]") {
    std::string_view guid = "";
    uint32_t index = awow::parseSpawnIndex(guid);

    REQUIRE(index == 0);
}

TEST_CASE("parseSpawnIndex - No dashes returns 0", "[color][parseSpawnIndex]") {
    std::string_view guid = "InvalidGUID";
    uint32_t index = awow::parseSpawnIndex(guid);

    REQUIRE(index == 0);
}

TEST_CASE("parseSpawnIndex - Too short after last dash returns 0", "[color][parseSpawnIndex]") {
    std::string_view guid = "Creature-0-3886-ABC";
    uint32_t index = awow::parseSpawnIndex(guid);

    REQUIRE(index == 0);
}

// getActorTypeFromGuid Tests

TEST_CASE("getActorTypeFromGuid - Player GUID", "[color][getActorTypeFromGuid]") {
    std::string_view guid = "Player-11-0E90EFD2";
    ActorType type = awow::getActorTypeFromGuid(guid);

    REQUIRE(type == ActorType::Player);
}

TEST_CASE("getActorTypeFromGuid - Creature GUID", "[color][getActorTypeFromGuid]") {
    std::string_view guid = "Creature-0-3886-2660-16035-216336-00104282A4";
    ActorType type = awow::getActorTypeFromGuid(guid);

    REQUIRE(type == ActorType::Creature);
}

TEST_CASE("getActorTypeFromGuid - Vehicle GUID", "[color][getActorTypeFromGuid]") {
    std::string_view guid = "Vehicle-0-1234-5678-9ABC";
    ActorType type = awow::getActorTypeFromGuid(guid);

    REQUIRE(type == ActorType::Vehicle);
}

TEST_CASE("getActorTypeFromGuid - Pet GUID", "[color][getActorTypeFromGuid]") {
    std::string_view guid = "Pet-0-ABCD-EFGH";
    ActorType type = awow::getActorTypeFromGuid(guid);

    REQUIRE(type == ActorType::Pet);
}

TEST_CASE("getActorTypeFromGuid - Unknown GUID", "[color][getActorTypeFromGuid]") {
    std::string_view guid = "Unknown-Type-12345";
    ActorType type = awow::getActorTypeFromGuid(guid);

    REQUIRE(type == ActorType::Unknown);
}

TEST_CASE("getActorTypeFromGuid - Empty string returns Unknown", "[color][getActorTypeFromGuid]") {
    std::string_view guid = "";
    ActorType type = awow::getActorTypeFromGuid(guid);

    REQUIRE(type == ActorType::Unknown);
}

TEST_CASE("getActorTypeFromGuid - Too short for any prefix", "[color][getActorTypeFromGuid]") {
    std::string_view guid = "Pla";
    ActorType type = awow::getActorTypeFromGuid(guid);

    REQUIRE(type == ActorType::Unknown);
}

// generateActorColor Tests

TEST_CASE("generateActorColor - Player returns consistent color", "[color][generateActorColor]") {
    std::string_view guid = "Player-11-0E90EFD2";
    UnitFlags flags = UnitFlags::parse("0x511");  // Friendly player

    ActorColor color1 = awow::generateActorColor(ActorType::Player, flags, guid);
    ActorColor color2 = awow::generateActorColor(ActorType::Player, flags, guid);

    // Same GUID should produce same color (deterministic)
    REQUIRE(color1.r == Approx(color2.r).margin(0.001f));
    REQUIRE(color1.g == Approx(color2.g).margin(0.001f));
    REQUIRE(color1.b == Approx(color2.b).margin(0.001f));

    // Color should be bright for players
    REQUIRE(color1.r >= 0.0f);
    REQUIRE(color1.g >= 0.0f);
    REQUIRE(color1.b >= 0.0f);
}

TEST_CASE("generateActorColor - Different GUIDs produce different colors", "[color][generateActorColor]") {
    std::string_view guid1 = "Player-11-0E90EFD2";
    std::string_view guid2 = "Player-11-12345678";
    UnitFlags flags = UnitFlags::parse("0x511");

    ActorColor color1 = awow::generateActorColor(ActorType::Player, flags, guid1);
    ActorColor color2 = awow::generateActorColor(ActorType::Player, flags, guid2);

    // Different GUIDs should produce different colors (hash-based variation)
    bool different = (color1.r != color2.r) || (color1.g != color2.g) || (color1.b != color2.b);
    REQUIRE(different);
}

TEST_CASE("generateActorColor - Hostile creature vs friendly creature", "[color][generateActorColor]") {
    std::string_view guid = "Creature-0-3886-2660-16035-216336-00104282A4";
    UnitFlags hostileFlags = UnitFlags::parse("0x10a48");  // Hostile NPC
    UnitFlags friendlyFlags = UnitFlags::parse("0x10a10"); // Friendly NPC

    ActorColor hostileColor = awow::generateActorColor(ActorType::Creature, hostileFlags, guid);
    ActorColor friendlyColor = awow::generateActorColor(ActorType::Creature, friendlyFlags, guid);

    // Hostile and friendly should have different color schemes
    bool different = (hostileColor.r != friendlyColor.r) ||
                     (hostileColor.g != friendlyColor.g) ||
                     (hostileColor.b != friendlyColor.b);
    REQUIRE(different);
}

TEST_CASE("generateActorColor - Pet friendly vs hostile", "[color][generateActorColor]") {
    std::string_view guid = "Pet-0-ABCD-1234";
    UnitFlags friendlyFlags = UnitFlags::parse("0x1111");  // Friendly pet
    UnitFlags hostileFlags = UnitFlags::parse("0x1140");   // Hostile pet

    ActorColor friendlyColor = awow::generateActorColor(ActorType::Pet, friendlyFlags, guid);
    ActorColor hostileColor = awow::generateActorColor(ActorType::Pet, hostileFlags, guid);

    // Friendly and hostile pets should have different colors
    bool different = (friendlyColor.r != hostileColor.r) ||
                     (friendlyColor.g != hostileColor.g) ||
                     (friendlyColor.b != hostileColor.b);
    REQUIRE(different);
}

TEST_CASE("generateActorColor - Vehicle gets consistent color", "[color][generateActorColor]") {
    std::string_view guid = "Vehicle-0-1234-5678-ABCD";
    UnitFlags flags = UnitFlags::parse("0x10a48");

    ActorColor color1 = awow::generateActorColor(ActorType::Vehicle, flags, guid);
    ActorColor color2 = awow::generateActorColor(ActorType::Vehicle, flags, guid);

    REQUIRE(color1.r == Approx(color2.r).margin(0.001f));
    REQUIRE(color1.g == Approx(color2.g).margin(0.001f));
    REQUIRE(color1.b == Approx(color2.b).margin(0.001f));
}

TEST_CASE("generateActorColor - Unknown type returns gray", "[color][generateActorColor]") {
    std::string_view guid = "Unknown-Type";
    UnitFlags flags = UnitFlags::parse("0x0");

    ActorColor color = awow::generateActorColor(ActorType::Unknown, flags, guid);

    REQUIRE(color.r == Approx(0.5f).margin(0.001f));
    REQUIRE(color.g == Approx(0.5f).margin(0.001f));
    REQUIRE(color.b == Approx(0.5f).margin(0.001f));
}

TEST_CASE("generateActorColor - All RGB components are in valid range", "[color][generateActorColor]") {
    std::string_view guid = "Creature-0-3886-2660-16035-216336-00104282A4";
    UnitFlags flags = UnitFlags::parse("0x10a48");

    ActorColor color = awow::generateActorColor(ActorType::Creature, flags, guid);

    REQUIRE(color.r >= 0.0f);
    REQUIRE(color.r <= 1.0f);
    REQUIRE(color.g >= 0.0f);
    REQUIRE(color.g <= 1.0f);
    REQUIRE(color.b >= 0.0f);
    REQUIRE(color.b <= 1.0f);
}
