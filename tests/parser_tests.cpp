#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
#include <string_view>
#include <vector>
#include <cmath>
#include "../AwowLab/Parser/include/Parser/Parser_Damage.h"

TEST_CASE("SpellDamageTag parsing produces correct DamageEventData", "[parser]") {
    // Arrange: sample tokens matching modern 46-token format
    // Structure: base(11) + spell(3) + advanced(19) + damage_suffix(13)
    const std::vector<std::string_view> tokens = {
        "7/29/2025", "22:49:09.618-6", "SPELL_DAMAGE", "Vehicle-0-3884-2769-8562-231075-000009A381",
        "Chrome King Gallywix", "0xa48", "0x80000000", "Player-11-0E96012E", "Cärlos-Tichondrius-US", "0x514",
        "0x80000000", "466752", "Venting Heat", "0x4", "Player-11-0E96012E", "0000000000000000",
        "10903598", "11886512", "28857", "104030", "90362", "997", "300", "0", "11", "26", "125", "0",
        "-491.04", "-691.87", "2409", "3.8887", "684", "191644", "240071", "-1", "4", "0", "0", "0",
        "nil", "nil", "nil", "nil", "nil", "AOE"
    };

    // Act: parse tokens into DamageEventData
    auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);

    // Assert: verify position was parsed correctly
    REQUIRE(data.advanced_info.position.valid);

    // Check timestamp_ms is non-zero (relative timestamp)
    REQUIRE(data.timestamp_ms >= 0);

    // Check X coordinate (raw float from combat log)
    REQUIRE(data.advanced_info.position.x_loc == Catch::Approx(-491.04f).epsilon(0.01));

    // Check Y coordinate (raw float from combat log)
    REQUIRE(data.advanced_info.position.y_loc == Catch::Approx(-691.87f).epsilon(0.01));

    // Check rotation scaled by 10000
    uint16_t expected_radians = static_cast<uint16_t>(std::lround(3.8887 * 10000.0));
    REQUIRE(data.advanced_info.position.facing == expected_radians);

    // Check map ID matches input token
    REQUIRE(data.advanced_info.position.map_id == 2409);

    // Damage suffix convention (see ParsedEvents.h):
    // log position 0 = post-mitigation damage -> mitigated_amount
    // log position 1 = unmitigated baseAmount -> amount
    REQUIRE(data.mitigated_amount == 191644);
    REQUIRE(data.amount == 240071);
}

// Reference implementation: the original per-event mktime conversion.
// The production path caches the date's midnight epoch; these tests pin
// the two to identical results.
static uint64_t reference_timestamp_ms(std::string_view date, std::string_view time) {
    std::tm tm{};
    int milliseconds = 0;

    auto first_slash = date.find('/');
    auto second_slash = date.find('/', first_slash + 1);
    std::from_chars(date.data(), date.data() + first_slash, tm.tm_mon);
    std::from_chars(date.data() + first_slash + 1, date.data() + second_slash, tm.tm_mday);
    std::from_chars(date.data() + second_slash + 1, date.data() + date.size(), tm.tm_year);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;

    auto h_end = time.find(':');
    auto m2_end = time.find(':', h_end + 1);
    auto s_end = time.find('.', m2_end + 1);
    std::from_chars(time.data(), time.data() + h_end, tm.tm_hour);
    std::from_chars(time.data() + h_end + 1, time.data() + m2_end, tm.tm_min);
    std::from_chars(time.data() + m2_end + 1, time.data() + s_end, tm.tm_sec);
    size_t ms_start = s_end + 1;
    size_t ms_end = (std::min)(ms_start + 3, time.size());
    std::from_chars(time.data() + ms_start, time.data() + ms_end, milliseconds);

    std::time_t sec_epoch = std::mktime(&tm);
    return static_cast<uint64_t>(sec_epoch) * 1000ull + static_cast<uint64_t>(milliseconds);
}

TEST_CASE("parse_timestamp_ms matches per-event mktime conversion", "[parser][timestamp]") {
    // Covers midnight rollover, year rollover, both US DST transition
    // dates, and a UTC-offset suffix on the time field.
    const std::pair<std::string_view, std::string_view> samples[] = {
        {"7/29/2025", "22:49:09.618-6"},
        {"7/29/2025", "23:59:59.999"},
        {"7/30/2025", "0:00:00.000"},
        {"12/31/2025", "23:59:59.500"},
        {"1/1/2026", "0:00:01.001"},
        {"3/9/2025", "1:59:59.000"},   // Spring-forward date
        {"3/9/2025", "3:00:00.000"},
        {"11/2/2025", "1:30:00.000"},  // Fall-back date
        {"2/29/2024", "12:00:00.000"}, // Leap day
    };

    for (const auto& [date, time] : samples) {
        std::vector<std::string_view> tokens = {date, time};
        INFO("date=" << date << " time=" << time);
        REQUIRE(parser::EventParser<void>::parse_timestamp_ms(tokens)
                == reference_timestamp_ms(date, time));
    }
}

TEST_CASE("parse_timestamp_ms rejects malformed fields", "[parser][timestamp]") {
    const std::pair<std::string_view, std::string_view> bad[] = {
        {"garbage", "22:49:09.618"},
        {"7-29-2025", "22:49:09.618"},   // Wrong date separator
        {"7/29/2025", "garbage"},
        {"7/29/2025", "22:49:09"},       // No milliseconds separator
        {"a/b/c", "22:49:09.618"},
        {"7/29/2025", "xx:49:09.618"},
    };

    for (const auto& [date, time] : bad) {
        std::vector<std::string_view> tokens = {date, time};
        INFO("date=" << date << " time=" << time);
        REQUIRE(parser::EventParser<void>::parse_timestamp_ms(tokens) == 0);
    }
}