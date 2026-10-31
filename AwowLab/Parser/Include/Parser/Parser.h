#pragma once
#include <string_view>
#include <vector>
#include <charconv>
#include <ctime>
#include <algorithm>
#include "../structures.h"

namespace parser {

    // Absolute time of the first event parsed. Every parser measures its
    // relative timestamps from this one value.
    inline uint64_t& get_log_start_timestamp_ms() {
        static uint64_t ts = 0;
        return ts;
    }

    // Converts an absolute timestamp to milliseconds after the log start,
    // taking this one as the start if none has been seen yet.
    inline int32_t relative_timestamp_ms(uint64_t absolute_ts) {
        auto& log_start = get_log_start_timestamp_ms();
        if (log_start == 0) {
            log_start = absolute_ts;
        }
        return static_cast<int32_t>(static_cast<int64_t>(absolute_ts) - static_cast<int64_t>(log_start));
    }

    template <typename EventType>
    struct EventParser {

        static constexpr size_t expected_token_count() {
            return 0;
        }

        // Returns 0 for lines whose date/time fields are malformed.
        static uint64_t parse_timestamp_ms(const std::vector<std::string_view>& tokens) {

            const std::string_view date = tokens[0];
            const std::string_view time = tokens[1];

            // Date format is MM/DD/YYYY
            auto first_slash = date.find('/');
            if (first_slash == std::string_view::npos) return 0;
            auto second_slash = date.find('/', first_slash + 1);
            if (second_slash == std::string_view::npos) return 0;

            // Time format is HH:MM:SS.mmm (may carry a UTC-offset suffix we ignore)
            auto h_end = time.find(':');
            if (h_end == std::string_view::npos) return 0;
            auto m2_end = time.find(':', h_end + 1);
            if (m2_end == std::string_view::npos) return 0;
            auto s_end = time.find('.', m2_end + 1);
            if (s_end == std::string_view::npos) return 0;

            int year = 0, month = 0, day = 0;
            int hour = 0, minute = 0, second = 0, milliseconds = 0;

            auto parsed = [](const char* first, const char* last, int& out) {
                return std::from_chars(first, last, out).ec == std::errc();
            };

            bool ok = parsed(date.data(), date.data() + first_slash, month)
                   && parsed(date.data() + first_slash + 1, date.data() + second_slash, day)
                   && parsed(date.data() + second_slash + 1, date.data() + date.size(), year)
                   && parsed(time.data(), time.data() + h_end, hour)
                   && parsed(time.data() + h_end + 1, time.data() + m2_end, minute)
                   && parsed(time.data() + m2_end + 1, time.data() + s_end, second);
            if (!ok) return 0;

            size_t ms_start = s_end + 1;
            size_t ms_end = (std::min)(ms_start + 3, time.size());  // (std::min) dodges the Windows min macro
            std::from_chars(time.data() + ms_start, time.data() + ms_end, milliseconds);

            // This runs for every event, but the date only changes at a
            // midnight rollover, so mktime is called once per date and the
            // midnight epoch reused. The zeroed tm has tm_isdst = 0, which
            // keeps mktime a plain calendar conversion and makes
            // midnight + h*3600 + m*60 + s exact.
            thread_local int cached_year = 0, cached_month = 0, cached_day = 0;
            thread_local std::time_t cached_midnight = 0;
            if (year != cached_year || month != cached_month || day != cached_day) {
                std::tm tm{};
                tm.tm_year = year - 1900;
                tm.tm_mon = month - 1;
                tm.tm_mday = day;
                cached_midnight = std::mktime(&tm);
                cached_year = year;
                cached_month = month;
                cached_day = day;
            }

            std::time_t sec_epoch = cached_midnight + hour * 3600 + minute * 60 + second;
            return static_cast<uint64_t>(sec_epoch) * 1000ull + static_cast<uint64_t>(milliseconds);
        };
    };

} // namespace parser
