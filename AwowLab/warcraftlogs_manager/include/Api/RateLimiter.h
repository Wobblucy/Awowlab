#pragma once

#include <cstdint>
#include <chrono>
#include <mutex>
#include <string>

namespace warcraftlogs {

// The API's rate limit, as last reported.
struct RateLimitInfo {
    int32_t limitPerHour = 0;
    int32_t pointsSpentThisHour = 0;
    int32_t pointsResetIn = 0;          // milliseconds until the limit resets
    int32_t userPointsResetIn = -1;     // seconds until the signed-in user's points reset; -1 without a user
    std::chrono::system_clock::time_point lastUpdated;

    [[nodiscard]] int32_t remainingPoints() const noexcept {
        return limitPerHour - pointsSpentThisHour;
    }

    [[nodiscard]] float usagePercent() const noexcept {
        if (limitPerHour == 0) return 0.0f;
        return static_cast<float>(pointsSpentThisHour) / static_cast<float>(limitPerHour) * 100.0f;
    }

    // More than 80% used.
    [[nodiscard]] bool isApproachingLimit() const noexcept {
        return usagePercent() > 80.0f;
    }

    [[nodiscard]] bool isExceeded() const noexcept {
        return pointsSpentThisHour >= limitPerHour;
    }
};

// Tracks WarcraftLogs API point usage. Thread-safe.
class RateLimiter {
public:
    RateLimiter() = default;

    // Holds a mutex, so neither copyable nor movable.
    RateLimiter(const RateLimiter&) = delete;
    RateLimiter& operator=(const RateLimiter&) = delete;
    RateLimiter(RateLimiter&&) = delete;
    RateLimiter& operator=(RateLimiter&&) = delete;

    void update(int32_t limitPerHour, int32_t pointsSpent, int32_t resetIn);

    // Reads rateLimitData from a response. False if it isn't there.
    bool updateFromJson(const std::string& jsonResponse);

    // Always true until the first rate limit figures arrive.
    [[nodiscard]] bool canProceed(int32_t estimatedCost = 1) const;

    [[nodiscard]] RateLimitInfo getInfo() const;

    // Milliseconds to wait before the next request; 0 for none.
    [[nodiscard]] int32_t getRecommendedDelay() const;

    // Call just before sending a request.
    void recordRequest(int32_t estimatedCost = 1);

    // For example after the hourly reset.
    void reset();

private:
    RateLimitInfo m_info;
    mutable std::mutex m_mutex;
};

} // namespace warcraftlogs
