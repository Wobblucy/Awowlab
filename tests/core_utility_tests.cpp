#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
#include "../AwowLab/Core/LRUCache.h"
#include "../AwowLab/Core/StringInterner.h"
#include "../AwowLab/Core/PhaseSettings.h"
#include "../AwowLab/Core/PhaseResolver.h"
#include "../AwowLab/Core/DefaultDefensives.h"
#include "../AwowLab/Core/Utils/FileNameUtils.h"
#include "../AwowLab/UIManager/include/UI/SpellIconTable.h"
#include "../AwowLab/Parser/Include/Parser/Parser_Damage.h"
#include "../AwowLab/Parser/Include/Parser/Parser_Combatant_Info.h"
#include "../AwowLab/Parser/Include/Parser/SpecializationColors.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>

// LRUCache Tests

TEST_CASE("LRUCache - basic put and get operations", "[utility][lrucache]") {
    LRUCache<int, std::string> cache(3);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    auto* val1 = cache.get(1);
    auto* val2 = cache.get(2);
    auto* val3 = cache.get(3);

    REQUIRE(val1 != nullptr);
    REQUIRE(*val1 == "one");
    REQUIRE(val2 != nullptr);
    REQUIRE(*val2 == "two");
    REQUIRE(val3 != nullptr);
    REQUIRE(*val3 == "three");
    REQUIRE(cache.size() == 3);
}

TEST_CASE("LRUCache - get returns nullptr for missing keys", "[utility][lrucache]") {
    LRUCache<int, std::string> cache(5);

    cache.put(1, "one");

    auto* missing = cache.get(99);
    REQUIRE(missing == nullptr);
}

TEST_CASE("LRUCache - eviction callback invoked on capacity overflow", "[utility][lrucache]") {
    int evictionCount = 0;
    std::vector<int> evictedKeys;

    auto callback = [&](const int& key, std::string& /*value*/) {
        evictionCount++;
        evictedKeys.push_back(key);
    };

    LRUCache<int, std::string> cache(2, callback);

    cache.put(1, "one");
    cache.put(2, "two");
    REQUIRE(evictionCount == 0);

    cache.put(3, "three");  // Should evict key 1 (least recently used)
    REQUIRE(evictionCount == 1);
    REQUIRE(evictedKeys.size() == 1);
    REQUIRE(evictedKeys[0] == 1);
}

TEST_CASE("LRUCache - eviction order is least recently used from back", "[utility][lrucache]") {
    std::vector<int> evictedKeys;
    auto callback = [&](const int& key, std::string& /*value*/) {
        evictedKeys.push_back(key);
    };

    LRUCache<int, std::string> cache(3, callback);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    // Now the order is: 3 (front/MRU) -> 2 -> 1 (back/LRU)
    cache.put(4, "four");  // Should evict 1

    REQUIRE(evictedKeys.size() == 1);
    REQUIRE(evictedKeys[0] == 1);
    REQUIRE(cache.get(1) == nullptr);
    REQUIRE(cache.get(2) != nullptr);
    REQUIRE(cache.get(3) != nullptr);
    REQUIRE(cache.get(4) != nullptr);
}

TEST_CASE("LRUCache - get updates LRU order", "[utility][lrucache]") {
    std::vector<int> evictedKeys;
    auto callback = [&](const int& key, std::string& /*value*/) {
        evictedKeys.push_back(key);
    };

    LRUCache<int, std::string> cache(3, callback);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    // Order: 3 -> 2 -> 1 (LRU)

    cache.get(1);  // Move 1 to front
    // Order: 1 -> 3 -> 2 (LRU)

    cache.put(4, "four");  // Should evict 2, not 1
    REQUIRE(evictedKeys.size() == 1);
    REQUIRE(evictedKeys[0] == 2);
    REQUIRE(cache.get(1) != nullptr);
    REQUIRE(cache.get(2) == nullptr);
}

TEST_CASE("LRUCache - put updates existing keys and moves to front", "[utility][lrucache]") {
    std::vector<int> evictedKeys;
    auto callback = [&](const int& key, std::string& /*value*/) {
        evictedKeys.push_back(key);
    };

    LRUCache<int, std::string> cache(3, callback);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    cache.put(1, "ONE");  // Update existing, move to front
    // Order: 1 -> 3 -> 2 (LRU)
    // The replaced value went through the callback
    REQUIRE(evictedKeys == std::vector<int>{1});

    auto* val = cache.get(1);
    REQUIRE(val != nullptr);
    REQUIRE(*val == "ONE");

    cache.put(4, "four");  // Should evict 2
    REQUIRE(evictedKeys == std::vector<int>{1, 2});
}

TEST_CASE("LRUCache - replacing a key releases the old value", "[utility][lrucache]") {
    // The texture caches free GPU resources in the callback, so a value
    // dropped by put() without it is a leaked image
    std::vector<std::string> released;
    auto callback = [&](const int& /*key*/, std::string& value) {
        released.push_back(value);
    };

    LRUCache<int, std::string> cache(3, callback);
    cache.put(7, "first");
    cache.put(7, "second");

    REQUIRE(released == std::vector<std::string>{"first"});
    REQUIRE(cache.size() == 1);
    REQUIRE(*cache.get(7) == "second");

    cache.clear();
    REQUIRE(released == std::vector<std::string>{"first", "second"});
}

TEST_CASE("LRUCache - peek does NOT update LRU order", "[utility][lrucache]") {
    std::vector<int> evictedKeys;
    auto callback = [&](const int& key, std::string& /*value*/) {
        evictedKeys.push_back(key);
    };

    LRUCache<int, std::string> cache(3, callback);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    // Order: 3 -> 2 -> 1 (LRU)

    const auto* val = cache.peek(1);  // Peek should NOT change order
    REQUIRE(val != nullptr);
    REQUIRE(*val == "one");

    cache.put(4, "four");  // Should still evict 1 (peek didn't move it)
    REQUIRE(evictedKeys.size() == 1);
    REQUIRE(evictedKeys[0] == 1);
}

TEST_CASE("LRUCache - contains checks key existence", "[utility][lrucache]") {
    LRUCache<int, std::string> cache(3);

    cache.put(1, "one");
    cache.put(2, "two");

    REQUIRE(cache.contains(1));
    REQUIRE(cache.contains(2));
    REQUIRE_FALSE(cache.contains(3));
    REQUIRE_FALSE(cache.contains(99));
}

TEST_CASE("LRUCache - remove invokes callback and removes entry", "[utility][lrucache]") {
    int evictionCount = 0;
    int evictedKey = -1;
    auto callback = [&](const int& key, std::string& /*value*/) {
        evictionCount++;
        evictedKey = key;
    };

    LRUCache<int, std::string> cache(5, callback);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    bool removed = cache.remove(2);
    REQUIRE(removed);
    REQUIRE(evictionCount == 1);
    REQUIRE(evictedKey == 2);
    REQUIRE_FALSE(cache.contains(2));
    REQUIRE(cache.size() == 2);

    bool removedAgain = cache.remove(2);
    REQUIRE_FALSE(removedAgain);
    REQUIRE(evictionCount == 1);  // Callback not called for missing key
}

TEST_CASE("LRUCache - clear invokes callbacks for all entries", "[utility][lrucache]") {
    int evictionCount = 0;
    std::vector<int> evictedKeys;
    auto callback = [&](const int& key, std::string& /*value*/) {
        evictionCount++;
        evictedKeys.push_back(key);
    };

    LRUCache<int, std::string> cache(5, callback);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    cache.clear();
    REQUIRE(evictionCount == 3);
    REQUIRE(evictedKeys.size() == 3);
    REQUIRE(cache.size() == 0);
    REQUIRE(cache.empty());
}

TEST_CASE("LRUCache - empty and size accessors", "[utility][lrucache]") {
    LRUCache<int, std::string> cache(5);

    REQUIRE(cache.empty());
    REQUIRE(cache.size() == 0);

    cache.put(1, "one");
    REQUIRE_FALSE(cache.empty());
    REQUIRE(cache.size() == 1);

    cache.put(2, "two");
    REQUIRE(cache.size() == 2);

    cache.clear();
    REQUIRE(cache.empty());
    REQUIRE(cache.size() == 0);
}

TEST_CASE("LRUCache - capacity boundary condition (capacity 1)", "[utility][lrucache]") {
    std::vector<int> evictedKeys;
    auto callback = [&](const int& key, std::string& /*value*/) {
        evictedKeys.push_back(key);
    };

    LRUCache<int, std::string> cache(1, callback);

    cache.put(1, "one");
    REQUIRE(cache.size() == 1);

    cache.put(2, "two");  // Should evict 1
    REQUIRE(cache.size() == 1);
    REQUIRE(evictedKeys.size() == 1);
    REQUIRE(evictedKeys[0] == 1);
    REQUIRE(cache.get(1) == nullptr);
    REQUIRE(cache.get(2) != nullptr);
}

TEST_CASE("LRUCache - forEach iteration", "[utility][lrucache]") {
    LRUCache<int, std::string> cache(5);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    int count = 0;
    cache.forEach([&](int key, std::string& /*value*/) {
        count++;
        REQUIRE((key == 1 || key == 2 || key == 3));
    });

    REQUIRE(count == 3);
}

TEST_CASE("LRUCache - no callback provided (nullptr)", "[utility][lrucache]") {
    LRUCache<int, std::string> cache(2);  // No callback

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");  // Eviction should work without callback

    REQUIRE(cache.size() == 2);
    REQUIRE(cache.get(1) == nullptr);
    REQUIRE(cache.get(2) != nullptr);
    REQUIRE(cache.get(3) != nullptr);
}

// StringInterner Tests

TEST_CASE("StringInterner - intern same string returns same ID", "[utility][interner]") {
    StringInterner interner;

    auto id1 = interner.intern("Player-123");
    auto id2 = interner.intern("Player-123");

    REQUIRE(id1 == id2);
    REQUIRE(id1 != StringInterner::INVALID);
}

TEST_CASE("StringInterner - different strings get different IDs", "[utility][interner]") {
    StringInterner interner;

    auto id1 = interner.intern("Player-123");
    auto id2 = interner.intern("Player-456");

    REQUIRE(id1 != id2);
    REQUIRE(id1 != StringInterner::INVALID);
    REQUIRE(id2 != StringInterner::INVALID);
}

TEST_CASE("StringInterner - lookup returns correct string_view", "[utility][interner]") {
    StringInterner interner;

    auto id = interner.intern("Player-3657-0A5F32EA");
    auto str = interner.lookup(id);

    REQUIRE(str == "Player-3657-0A5F32EA");
}

TEST_CASE("StringInterner - INVALID ID handling", "[utility][interner]") {
    StringInterner interner;

    auto str = interner.lookup(StringInterner::INVALID);
    REQUIRE(str.empty());
}

TEST_CASE("StringInterner - isValid correctness", "[utility][interner]") {
    StringInterner interner;

    REQUIRE_FALSE(interner.isValid(StringInterner::INVALID));

    auto id = interner.intern("Test");
    REQUIRE(interner.isValid(id));

    REQUIRE_FALSE(interner.isValid(9999));  // Out of range ID
}

TEST_CASE("StringInterner - empty string returns INVALID", "[utility][interner]") {
    StringInterner interner;

    auto id = interner.intern("");
    REQUIRE(id == StringInterner::INVALID);
}

TEST_CASE("StringInterner - size tracking", "[utility][interner]") {
    StringInterner interner;

    REQUIRE(interner.size() == 1);  // ID 0 is reserved as empty placeholder

    interner.intern("Test1");
    REQUIRE(interner.size() == 2);

    interner.intern("Test1");  // Same string, size should not change
    REQUIRE(interner.size() == 2);

    interner.intern("Test2");
    REQUIRE(interner.size() == 3);
}

TEST_CASE("StringInterner - clear resets state", "[utility][interner]") {
    StringInterner interner;

    auto id1 = interner.intern("Test1");
    auto id2 = interner.intern("Test2");

    REQUIRE(interner.size() == 3);  // 1 (empty) + 2 strings

    interner.clear();
    REQUIRE(interner.size() == 1);  // Only empty placeholder remains
    REQUIRE_FALSE(interner.isValid(id1));
    REQUIRE_FALSE(interner.isValid(id2));
}

TEST_CASE("StringInterner - memoryUsage estimation", "[utility][interner]") {
    StringInterner interner;

    size_t initialMemory = interner.memoryUsage();
    REQUIRE(initialMemory > 0);

    interner.intern("Player-3657-0A5F32EA");
    interner.intern("Creature-0-1234-5678");

    size_t afterMemory = interner.memoryUsage();
    REQUIRE(afterMemory > initialMemory);
}

TEST_CASE("StringInterner - lookup with out-of-range ID returns empty", "[utility][interner]") {
    StringInterner interner;

    auto str = interner.lookup(999999);
    REQUIRE(str.empty());
}

// parser::util Function Tests

TEST_CASE("parser::util::parseInt64 - normal positive numbers", "[utility][parser]") {
    REQUIRE(parser::util::parseInt64("12345") == 12345);
    REQUIRE(parser::util::parseInt64("0") == 0);
    REQUIRE(parser::util::parseInt64("999999999") == 999999999);
}

TEST_CASE("parser::util::parseInt64 - negative numbers", "[utility][parser]") {
    REQUIRE(parser::util::parseInt64("-12345") == -12345);
    REQUIRE(parser::util::parseInt64("-1") == -1);
    REQUIRE(parser::util::parseInt64("-999999999") == -999999999);
}

TEST_CASE("parser::util::parseInt64 - nil handling", "[utility][parser]") {
    REQUIRE(parser::util::parseInt64("nil") == 0);
}

TEST_CASE("parser::util::parseInt64 - empty string handling", "[utility][parser]") {
    REQUIRE(parser::util::parseInt64("") == 0);
}

TEST_CASE("parser::util::parseInt64 - large numbers", "[utility][parser]") {
    REQUIRE(parser::util::parseInt64("9223372036854775807") == 9223372036854775807LL);
    REQUIRE(parser::util::parseInt64("-9223372036854775808") == (-9223372036854775807LL - 1));
}

TEST_CASE("parser::util::parseUint64 - normal numbers", "[utility][parser]") {
    REQUIRE(parser::util::parseUint64("12345") == 12345ULL);
    REQUIRE(parser::util::parseUint64("0") == 0ULL);
    REQUIRE(parser::util::parseUint64("18446744073709551615") == 18446744073709551615ULL);
}

TEST_CASE("parser::util::parseUint64 - nil and empty handling", "[utility][parser]") {
    REQUIRE(parser::util::parseUint64("nil") == 0ULL);
    REQUIRE(parser::util::parseUint64("") == 0ULL);
}

TEST_CASE("parser::util::parseInt32 - normal numbers", "[utility][parser]") {
    REQUIRE(parser::util::parseInt32("12345") == 12345);
    REQUIRE(parser::util::parseInt32("-12345") == -12345);
    REQUIRE(parser::util::parseInt32("0") == 0);
}

TEST_CASE("parser::util::parseInt32 - boundary values", "[utility][parser]") {
    REQUIRE(parser::util::parseInt32("2147483647") == 2147483647);
    REQUIRE(parser::util::parseInt32("-2147483648") == (-2147483647 - 1));
}

TEST_CASE("parser::util::parseUint32 - normal numbers", "[utility][parser]") {
    REQUIRE(parser::util::parseUint32("12345") == 12345U);
    REQUIRE(parser::util::parseUint32("0") == 0U);
    REQUIRE(parser::util::parseUint32("4294967295") == 4294967295U);
}

TEST_CASE("parser::util::parseUint32 - nil handling", "[utility][parser]") {
    REQUIRE(parser::util::parseUint32("nil") == 0U);
}

TEST_CASE("parser::util::parseFloat - normal floats", "[utility][parser]") {
    REQUIRE(parser::util::parseFloat("123.45") == Catch::Approx(123.45f));
    REQUIRE(parser::util::parseFloat("0.0") == Catch::Approx(0.0f));
    REQUIRE(parser::util::parseFloat("3.14159") == Catch::Approx(3.14159f));
}

TEST_CASE("parser::util::parseFloat - negative floats", "[utility][parser]") {
    REQUIRE(parser::util::parseFloat("-123.45") == Catch::Approx(-123.45f));
    REQUIRE(parser::util::parseFloat("-0.5") == Catch::Approx(-0.5f));
}

TEST_CASE("parser::util::parseFloat - nil and empty handling", "[utility][parser]") {
    REQUIRE(parser::util::parseFloat("nil") == Catch::Approx(0.0f));
    REQUIRE(parser::util::parseFloat("") == Catch::Approx(0.0f));
}

TEST_CASE("parser::util::parseFloat - high precision", "[utility][parser]") {
    REQUIRE(parser::util::parseFloat("1.234567") == Catch::Approx(1.234567f).epsilon(0.0001));
}

TEST_CASE("parser::util::parseBool - true values", "[utility][parser]") {
    REQUIRE(parser::util::parseBool("1") == true);
}

TEST_CASE("parser::util::parseBool - false values", "[utility][parser]") {
    REQUIRE(parser::util::parseBool("0") == false);
    REQUIRE(parser::util::parseBool("nil") == false);
    REQUIRE(parser::util::parseBool("") == false);
    REQUIRE(parser::util::parseBool("false") == false);
    REQUIRE(parser::util::parseBool("2") == false);
}

TEST_CASE("parser::util - numbers with leading zeros", "[utility][parser]") {
    REQUIRE(parser::util::parseInt32("0012345") == 12345);
    REQUIRE(parser::util::parseUint32("00100") == 100U);
    REQUIRE(parser::util::parseInt64("000999") == 999);
}

// PhaseSettings Tests

TEST_CASE("PhaseSettings - add/remove/hasRule behavior", "[utility][phases]") {
    auto& settings = PhaseSettings::instance();
    settings.rules.clear();

    settings.addRule(2902, 442432);
    settings.addRule(2902, 442432);  // Duplicate add is a no-op
    REQUIRE(settings.hasRule(2902, 442432));

    const auto* rules = settings.rulesFor(2902);
    REQUIRE(rules != nullptr);
    REQUIRE(rules->size() == 1);

    // Unknown encounter / spell
    REQUIRE_FALSE(settings.hasRule(9999, 442432));
    REQUIRE_FALSE(settings.hasRule(2902, 12345));
    REQUIRE(settings.rulesFor(9999) == nullptr);

    // Invalid ids are rejected
    settings.addRule(0, 442432);
    settings.addRule(2902, 0);
    REQUIRE(settings.rules.size() == 1);
    REQUIRE(settings.rulesFor(2902)->size() == 1);

    // Removing the last rule drops the encounter entry entirely
    settings.removeRule(2902, 442432);
    REQUIRE_FALSE(settings.hasRule(2902, 442432));
    REQUIRE(settings.rulesFor(2902) == nullptr);
    REQUIRE(settings.rules.empty());

    settings.rules.clear();
}

TEST_CASE("PhaseSettings - JSON round trip", "[utility][phases]") {
    auto& settings = PhaseSettings::instance();
    settings.rules.clear();

    // Two encounters, one rule with a custom label
    settings.addRule(2902, 442432);
    settings.addRule(2902, 443068, "Intermission");
    settings.addRule(3009, 1214893);

    std::string json = settings.toJson();
    settings.rules.clear();
    settings.fromJson(json);

    REQUIRE(settings.rules.size() == 2);
    REQUIRE(settings.hasRule(2902, 442432));
    REQUIRE(settings.hasRule(2902, 443068));
    REQUIRE(settings.hasRule(3009, 1214893));

    // Labels survive the round trip (empty stays empty)
    const auto* rules = settings.rulesFor(2902);
    REQUIRE(rules != nullptr);
    REQUIRE(rules->size() == 2);
    for (const auto& rule : *rules) {
        if (rule.spellId == 443068) {
            CHECK(rule.label == "Intermission");
        } else {
            CHECK(rule.label.empty());
        }
    }

    settings.rules.clear();
}

TEST_CASE("PhaseSettings - malformed JSON leaves rules empty", "[utility][phases]") {
    auto& settings = PhaseSettings::instance();
    settings.rules.clear();
    settings.addRule(2902, 442432);

    SECTION("parse error") {
        settings.fromJson("{not valid json");
        REQUIRE(settings.rules.empty());
    }

    SECTION("empty string") {
        settings.fromJson("");
        REQUIRE(settings.rules.empty());
    }

    SECTION("wrong root type") {
        settings.fromJson("[1,2,3]");
        REQUIRE(settings.rules.empty());
    }

    SECTION("rules missing the spell field are skipped") {
        settings.fromJson("{\"2902\":[{\"label\":\"no spell\"}]}");
        REQUIRE(settings.rules.empty());
    }

    settings.rules.clear();
}

TEST_CASE("PhaseSettings - round trip covers every rule kind", "[utility][phases]") {
    auto& settings = PhaseSettings::instance();
    settings.rules.clear();

    PhaseSettings::PhaseRule castRule;
    castRule.kind = PhaseSettings::RuleKind::SpellCast;
    castRule.spellId = 442432;
    castRule.label = "Intermission";
    settings.addRule(2902, castRule);

    PhaseSettings::PhaseRule timeRule;
    timeRule.kind = PhaseSettings::RuleKind::Elapsed;
    timeRule.elapsedMs = 150000;
    settings.addRule(2902, timeRule);

    // Emote text with commas and quotes must survive JSON escaping
    PhaseSettings::PhaseRule emoteRule;
    emoteRule.kind = PhaseSettings::RuleKind::Emote;
    emoteRule.emoteText = "Dimensius prepares to \"Devour\", ending it all";
    settings.addRule(2902, emoteRule);

    std::string json = settings.toJson();
    settings.rules.clear();
    settings.fromJson(json);

    const auto* rules = settings.rulesFor(2902);
    REQUIRE(rules != nullptr);
    REQUIRE(rules->size() == 3);

    for (const auto& rule : *rules) {
        switch (rule.kind) {
            case PhaseSettings::RuleKind::SpellCast:
                CHECK(rule.spellId == 442432);
                CHECK(rule.label == "Intermission");
                break;
            case PhaseSettings::RuleKind::Elapsed:
                CHECK(rule.elapsedMs == 150000);
                break;
            case PhaseSettings::RuleKind::Emote:
                CHECK(rule.emoteText == "Dimensius prepares to \"Devour\", ending it all");
                break;
        }
    }

    settings.rules.clear();
}

TEST_CASE("PhaseSettings - legacy spell-only JSON loads as SpellCast", "[utility][phases]") {
    auto& settings = PhaseSettings::instance();
    settings.rules.clear();

    // Saves written before rule kinds existed carry only "spell"/"label"
    settings.fromJson("{\"2902\":[{\"spell\":442432},{\"spell\":443068,\"label\":\"P3\"}]}");

    const auto* rules = settings.rulesFor(2902);
    REQUIRE(rules != nullptr);
    REQUIRE(rules->size() == 2);
    for (const auto& rule : *rules) {
        CHECK(rule.kind == PhaseSettings::RuleKind::SpellCast);
    }
    REQUIRE(settings.hasRule(2902, 442432));
    REQUIRE(settings.hasRule(2902, 443068));

    settings.rules.clear();
}

TEST_CASE("PhaseSettings - duplicate adds are no-ops per kind", "[utility][phases]") {
    auto& settings = PhaseSettings::instance();
    settings.rules.clear();

    PhaseSettings::PhaseRule timeRule;
    timeRule.kind = PhaseSettings::RuleKind::Elapsed;
    timeRule.elapsedMs = 90000;
    settings.addRule(2902, timeRule);
    settings.addRule(2902, timeRule);

    PhaseSettings::PhaseRule emoteRule;
    emoteRule.kind = PhaseSettings::RuleKind::Emote;
    emoteRule.emoteText = "Take flight!";
    settings.addRule(2902, emoteRule);
    settings.addRule(2902, emoteRule);

    REQUIRE(settings.rulesFor(2902)->size() == 2);

    // Invalid rules of each kind are rejected outright
    PhaseSettings::PhaseRule zeroTime;
    zeroTime.kind = PhaseSettings::RuleKind::Elapsed;
    zeroTime.elapsedMs = 0;
    settings.addRule(2902, zeroTime);

    PhaseSettings::PhaseRule emptyEmote;
    emptyEmote.kind = PhaseSettings::RuleKind::Emote;
    settings.addRule(2902, emptyEmote);

    REQUIRE(settings.rulesFor(2902)->size() == 2);

    settings.rules.clear();
}

TEST_CASE("PhaseSettings - removeRule only removes the matching kind", "[utility][phases]") {
    auto& settings = PhaseSettings::instance();
    settings.rules.clear();

    // A cast rule and a time rule sharing the same numeric value must
    // not shadow each other
    PhaseSettings::PhaseRule castRule;
    castRule.kind = PhaseSettings::RuleKind::SpellCast;
    castRule.spellId = 90000;
    settings.addRule(2902, castRule);

    PhaseSettings::PhaseRule timeRule;
    timeRule.kind = PhaseSettings::RuleKind::Elapsed;
    timeRule.elapsedMs = 90000;
    settings.addRule(2902, timeRule);

    PhaseSettings::PhaseRule emoteRule;
    emoteRule.kind = PhaseSettings::RuleKind::Emote;
    emoteRule.emoteText = "Take flight!";
    settings.addRule(2902, emoteRule);

    REQUIRE(settings.rulesFor(2902)->size() == 3);

    settings.removeRule(2902, timeRule);
    const auto* rules = settings.rulesFor(2902);
    REQUIRE(rules != nullptr);
    REQUIRE(rules->size() == 2);
    CHECK(settings.hasRule(2902, 90000));  // Cast rule untouched
    for (const auto& rule : *rules) {
        CHECK(rule.kind != PhaseSettings::RuleKind::Elapsed);
    }

    settings.removeRule(2902, emoteRule);
    REQUIRE(settings.rulesFor(2902)->size() == 1);
    REQUIRE(settings.hasRule(2902, 90000));

    settings.removeRule(2902, castRule);
    REQUIRE(settings.rulesFor(2902) == nullptr);

    settings.rules.clear();
}

// PhaseResolver Tests (shared rule -> phase-window resolution)

namespace {

// Rule builders for the resolver tests
PhaseSettings::PhaseRule castRuleFor(uint32_t spellId, std::string label = {}) {
    PhaseSettings::PhaseRule r;
    r.kind = PhaseSettings::RuleKind::SpellCast;
    r.spellId = spellId;
    r.label = std::move(label);
    return r;
}
PhaseSettings::PhaseRule timeRuleFor(int32_t ms, std::string label = {}) {
    PhaseSettings::PhaseRule r;
    r.kind = PhaseSettings::RuleKind::Elapsed;
    r.elapsedMs = ms;
    r.label = std::move(label);
    return r;
}
PhaseSettings::PhaseRule emoteRuleFor(std::string text, std::string label = {}) {
    PhaseSettings::PhaseRule r;
    r.kind = PhaseSettings::RuleKind::Emote;
    r.emoteText = std::move(text);
    r.label = std::move(label);
    return r;
}

}  // namespace

TEST_CASE("PhaseResolver - no rules yields no phases", "[phaseresolver]") {
    phase::RuleInputs inputs;
    inputs.pullStart_ms = 0;
    inputs.pullEnd_ms = 100000;
    CHECK(phase::resolvePhases({}, inputs).empty());
}

TEST_CASE("PhaseResolver - spell cast rule builds P1/P2", "[phaseresolver]") {
    phase::RuleInputs inputs;
    inputs.pullStart_ms = 1000;
    inputs.pullEnd_ms = 61000;
    inputs.firstCastTime = [](uint32_t spellId) -> std::optional<int32_t> {
        if (spellId == 500) return 31000;
        return std::nullopt;
    };

    std::vector<PhaseSettings::PhaseRule> rules = { castRuleFor(500) };
    auto phases = phase::resolvePhases(rules, inputs);
    REQUIRE(phases.size() == 2);
    CHECK(phases[0].label == "P1");
    CHECK(phases[0].start_ms == 1000);
    CHECK(phases[0].end_ms == 31000);
    CHECK(phases[1].label == "P2");
    CHECK(phases[1].start_ms == 31000);
    CHECK(phases[1].end_ms == 61000);
}

TEST_CASE("PhaseResolver - elapsed rule fires only before pull end", "[phaseresolver]") {
    phase::RuleInputs inputs;
    inputs.pullStart_ms = 0;
    inputs.pullEnd_ms = 20000;

    // 15s in - fires
    CHECK(phase::resolveRuleTime(timeRuleFor(15000), inputs) == std::optional<int32_t>(15000));
    // 25s in - past the pull end, dropped
    CHECK(phase::resolveRuleTime(timeRuleFor(25000), inputs) == std::nullopt);

    std::vector<PhaseSettings::PhaseRule> rules = { timeRuleFor(25000) };
    CHECK(phase::resolvePhases(rules, inputs).empty());
}

TEST_CASE("PhaseResolver - emote rule resolves via callback", "[phaseresolver]") {
    phase::RuleInputs inputs;
    inputs.pullStart_ms = 0;
    inputs.pullEnd_ms = 60000;
    inputs.firstEmoteTime = [](std::string_view text) -> std::optional<int32_t> {
        if (text == "begins to channel") return 12000;
        return std::nullopt;
    };

    CHECK(phase::resolveRuleTime(emoteRuleFor("begins to channel"), inputs)
          == std::optional<int32_t>(12000));
    CHECK(phase::resolveRuleTime(emoteRuleFor("never said"), inputs) == std::nullopt);
}

TEST_CASE("PhaseResolver - a rule that never fired is dropped", "[phaseresolver]") {
    phase::RuleInputs inputs;
    inputs.pullStart_ms = 0;
    inputs.pullEnd_ms = 60000;
    inputs.firstCastTime = [](uint32_t) -> std::optional<int32_t> { return std::nullopt; };

    std::vector<PhaseSettings::PhaseRule> rules = { castRuleFor(999) };
    CHECK(phase::resolvePhases(rules, inputs).empty());
}

TEST_CASE("PhaseResolver - boundaries sort by resolved time, not rule order", "[phaseresolver]") {
    phase::RuleInputs inputs;
    inputs.pullStart_ms = 0;
    inputs.pullEnd_ms = 90000;
    inputs.firstCastTime = [](uint32_t spellId) -> std::optional<int32_t> {
        if (spellId == 1) return 60000;
        if (spellId == 2) return 30000;
        return std::nullopt;
    };

    // Rule order puts the later cast first; result must still be time-ordered
    std::vector<PhaseSettings::PhaseRule> rules = {
        castRuleFor(1, "Late"), castRuleFor(2, "Early")
    };
    auto phases = phase::resolvePhases(rules, inputs);
    REQUIRE(phases.size() == 3);
    CHECK(phases[0].label == "P1");
    CHECK(phases[1].label == "Early");
    CHECK(phases[1].start_ms == 30000);
    CHECK(phases[2].label == "Late");
    CHECK(phases[2].start_ms == 60000);
    CHECK(phases[2].end_ms == 90000);
}

TEST_CASE("PhaseResolver - same-millisecond boundaries collapse", "[phaseresolver]") {
    phase::RuleInputs inputs;
    inputs.pullStart_ms = 0;
    inputs.pullEnd_ms = 90000;
    inputs.firstCastTime = [](uint32_t) -> std::optional<int32_t> { return 45000; };

    std::vector<PhaseSettings::PhaseRule> rules = {
        castRuleFor(1), castRuleFor(2)  // both resolve to 45000
    };
    auto phases = phase::resolvePhases(rules, inputs);
    // Only one boundary survives: P1 + P2
    REQUIRE(phases.size() == 2);
    CHECK(phases[1].start_ms == 45000);
}

// COMBATANT_INFO spec-id extraction + spec->color fallback

namespace {

// A minimal but realistic 11.x COMBATANT_INFO line: timestamp(2) +
// event(1) + guid(1) + faction(1) + 22 stats + specID(1) + talent array.
std::vector<std::string_view> combatantInfoTokens(std::string_view guid,
                                                   std::string_view specStr,
                                                   std::vector<std::string>& storage) {
    storage.clear();
    storage.push_back("1/1/2025");
    storage.push_back("20:00:00.000");
    storage.push_back("COMBATANT_INFO");
    storage.push_back(std::string(guid));
    storage.push_back("0");  // faction
    for (int i = 0; i < 22; ++i) storage.push_back("100");  // 22 stats
    storage.push_back(std::string(specStr));  // spec id (index 27)
    storage.push_back("[]");  // talent array
    std::vector<std::string_view> views;
    views.reserve(storage.size());
    for (const auto& s : storage) views.emplace_back(s);
    return views;
}

}  // namespace

TEST_CASE("CombatantInfo - extracts player guid and spec id", "[combatantinfo]") {
    std::vector<std::string> storage;
    auto tokens = combatantInfoTokens("Player-1-ABCDEF01", "268", storage);  // Brewmaster Monk
    CHECK(parser::EventParser<parser::CombatantInfoTag>::extract_player_guid(tokens)
          == "Player-1-ABCDEF01");
    CHECK(parser::EventParser<parser::CombatantInfoTag>::extract_spec_id(tokens) == 268);
}

TEST_CASE("CombatantInfo - short line yields no spec", "[combatantinfo]") {
    std::vector<std::string_view> tokens = {"1/1/2025", "20:00:00.000", "COMBATANT_INFO"};
    CHECK(parser::EventParser<parser::CombatantInfoTag>::extract_spec_id(tokens) == 0);
}

TEST_CASE("SpecColors - known spec maps to its class color", "[speccolor]") {
    // Brewmaster (268) is Monk -> #00FF98
    auto monk = parser::getColorFromSpecId(268);
    CHECK(monk.r == 0x00);
    CHECK(monk.g == 0xFF);
    CHECK(monk.b == 0x98);
    // All three Monk specs share the class
    CHECK(parser::getClassFromSpecId(269) == parser::WowClass::Monk);
    CHECK(parser::getClassFromSpecId(270) == parser::WowClass::Monk);
}

TEST_CASE("SpecColors - unknown spec falls back to gray sentinel", "[speccolor]") {
    CHECK(parser::getClassFromSpecId(0) == parser::WowClass::Unknown);
    auto unknown = parser::getColorFromSpecId(0);
    CHECK(unknown.r == 0x80);
    CHECK(unknown.g == 0x80);
    CHECK(unknown.b == 0x80);
}

// Default tracked-defensives seed

TEST_CASE("Default defensive list is non-empty and has no duplicates", "[defensives][defaults]") {
    const auto& ids = awow::defaultDefensiveSpellIds();
    REQUIRE(!ids.empty());

    std::unordered_set<uint32_t> seen;
    for (uint32_t id : ids) {
        CHECK(id != 0);                       // 0 is never a real spell id
        CHECK(seen.insert(id).second);        // no duplicate ids
    }

    // A couple of well-known ids we expect to be present.
    CHECK(seen.count(45438) == 1);   // Ice Block
    CHECK(seen.count(22812) == 1);   // Barkskin
}

TEST_CASE("PhaseResolver - offset shifts a cast boundary", "[phaseresolver]") {
    // A boss casts the ability that announces a transition seconds after the
    // transition itself, so the spell alone puts the boundary late. An offset
    // is what lets a rule say "phase two starts ten seconds before this cast"
    // and still resolve independently against any log - which is what keeps an
    // imported reference and a local pull describing the same phase.
    phase::RuleInputs inputs;
    inputs.pullStart_ms = 0;
    inputs.pullEnd_ms = 120000;
    inputs.firstCastTime = [](uint32_t spellId) -> std::optional<int32_t> {
        if (spellId == 500) return 60000;
        return std::nullopt;
    };

    SECTION("no offset lands on the cast") {
        CHECK(phase::resolveRuleTime(castRuleFor(500), inputs) ==
              std::optional<int32_t>(60000));
    }

    SECTION("a negative offset moves the boundary earlier") {
        auto rule = castRuleFor(500);
        rule.offsetMs = -10000;
        CHECK(phase::resolveRuleTime(rule, inputs) == std::optional<int32_t>(50000));
    }

    SECTION("a positive offset moves it later") {
        auto rule = castRuleFor(500);
        rule.offsetMs = 5000;
        CHECK(phase::resolveRuleTime(rule, inputs) == std::optional<int32_t>(65000));
    }

    SECTION("an offset past the pull start clamps rather than going negative") {
        // Otherwise later arithmetic reads a negative time as "never fired"
        // and the phase silently disappears.
        auto rule = castRuleFor(500);
        rule.offsetMs = -90000;
        CHECK(phase::resolveRuleTime(rule, inputs) == std::optional<int32_t>(0));
    }

    SECTION("a rule whose spell never fired stays unresolved") {
        auto rule = castRuleFor(999);
        rule.offsetMs = -10000;
        CHECK(phase::resolveRuleTime(rule, inputs) == std::nullopt);
    }
}

// File name helpers

TEST_CASE("isPlainFileName - accepts a bare name", "[utility][filename]") {
    using awow::files::isPlainFileName;
    CHECK(isPlainFileName("settings"));
    CHECK(isPlainFileName("my raid settings"));
    CHECK(isPlainFileName("a..b"));
    CHECK(isPlainFileName("\xc3\xa9quipe"));  // UTF-8 letters are fine
}

TEST_CASE("isPlainFileName - refuses anything that leaves the folder", "[utility][filename]") {
    using awow::files::isPlainFileName;
    CHECK_FALSE(isPlainFileName(""));
    CHECK_FALSE(isPlainFileName("."));
    CHECK_FALSE(isPlainFileName(".."));
    CHECK_FALSE(isPlainFileName("..\\evil"));
    CHECK_FALSE(isPlainFileName("../evil"));
    CHECK_FALSE(isPlainFileName("sub/name"));
    CHECK_FALSE(isPlainFileName("C:\\Windows\\x"));
    CHECK_FALSE(isPlainFileName("C:name"));
    CHECK_FALSE(isPlainFileName("what?"));
    CHECK_FALSE(isPlainFileName("a*b"));
    CHECK_FALSE(isPlainFileName("tab\there"));
    CHECK_FALSE(isPlainFileName("trailing."));
    CHECK_FALSE(isPlainFileName("trailing "));
}

TEST_CASE("toFileStem - keeps non-ASCII names distinct", "[utility][filename]") {
    using awow::files::toFileStem;
    // Two Cyrillic names must not collapse to the same fallback file
    const std::string a = toFileStem("\xd0\x91\xd0\xbe\xd1\x81\xd1\x81", "preset");
    const std::string b = toFileStem("\xd0\xa2\xd0\xb0\xd0\xbd\xd0\xba", "preset");
    CHECK(a == "\xd0\x91\xd0\xbe\xd1\x81\xd1\x81");
    CHECK(b == "\xd0\xa2\xd0\xb0\xd0\xbd\xd0\xba");
    CHECK(a != b);
}

TEST_CASE("toFileStem - matches the old ASCII behaviour", "[utility][filename]") {
    using awow::files::toFileStem;
    CHECK(toFileStem("Boss One", "preset") == "boss_one");
    CHECK(toFileStem("Phase-2_Notes", "preset") == "phase-2_notes");
    CHECK(toFileStem("a/b\\c:d", "preset") == "abcd");
    CHECK(toFileStem("???", "preset") == "preset");
    CHECK(toFileStem("..", "preset") == "preset");
    CHECK(toFileStem("", "preset") == "preset");
}

TEST_CASE("pathFromUtf8 - round-trips UTF-8 names", "[utility][filename]") {
    using namespace awow::files;
    const std::string name = "\xe5\x9b\xa2\xe9\x98\x9f.json";  // CJK
    CHECK(utf8FromPath(pathFromUtf8(name)) == name);
    CHECK(utf8FromPath(pathFromUtf8("data/raid_notes").filename()) == "raid_notes");
}

// Spell icon table

namespace {

void appendU32(std::string& out, uint32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); }
void appendU16(std::string& out, uint16_t v) { out.append(reinterpret_cast<const char*>(&v), 2); }

std::string makeIconTable(uint32_t numFilenames, uint32_t numMappings,
                          const std::vector<std::string>& names,
                          const std::vector<std::pair<uint32_t, uint16_t>>& mappings) {
    std::string out;
    appendU32(out, spell_icon_table::kMagic);
    appendU32(out, spell_icon_table::kVersion);
    appendU32(out, numFilenames);
    appendU32(out, numMappings);
    for (const auto& name : names) {
        appendU16(out, static_cast<uint16_t>(name.size()));
        out += name;
    }
    for (const auto& [spellId, index] : mappings) {
        appendU32(out, spellId);
        appendU16(out, index);
    }
    return out;
}

} // namespace

TEST_CASE("Spell icon table - well formed file parses", "[utility][icons]") {
    auto data = makeIconTable(2, 2, {"a.png", "bb.png"}, {{100, 0}, {200, 1}});
    std::string error;
    auto layout = spell_icon_table::parse(data.data(), data.size(), error);
    REQUIRE(layout.has_value());
    CHECK(layout->filenames == std::vector<std::string>{"a.png", "bb.png"});
    CHECK(layout->numMappings == 2);
    CHECK(layout->mappingOffset == 16 + 2 + 5 + 2 + 6);
}

TEST_CASE("Spell icon table - header counts larger than the file are refused", "[utility][icons]") {
    std::string error;

    SECTION("mapping count runs past the end") {
        auto data = makeIconTable(1, 1000, {"a.png"}, {{100, 0}});
        CHECK_FALSE(spell_icon_table::parse(data.data(), data.size(), error).has_value());
    }
    SECTION("filename count would reserve far more than the file holds") {
        auto data = makeIconTable(0xFFFFFFFFu, 0, {}, {});
        CHECK_FALSE(spell_icon_table::parse(data.data(), data.size(), error).has_value());
    }
    SECTION("filename length runs past the end") {
        auto data = makeIconTable(1, 0, {}, {});
        appendU16(data, 500);
        data += "short";
        CHECK_FALSE(spell_icon_table::parse(data.data(), data.size(), error).has_value());
    }
    SECTION("truncated header") {
        auto data = makeIconTable(0, 0, {}, {});
        CHECK_FALSE(spell_icon_table::parse(data.data(), 12, error).has_value());
    }
    SECTION("wrong magic") {
        auto data = makeIconTable(0, 0, {}, {});
        data[0] = 'X';
        CHECK_FALSE(spell_icon_table::parse(data.data(), data.size(), error).has_value());
    }
    CHECK_FALSE(error.empty());
}
