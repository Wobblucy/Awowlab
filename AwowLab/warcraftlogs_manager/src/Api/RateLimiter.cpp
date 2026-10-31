#include "Api/RateLimiter.h"
#include "Adapter/WclJsonHelpers.h"

#include <rapidjson/document.h>

namespace warcraftlogs {

void RateLimiter::update(int32_t limitPerHour, int32_t pointsSpent, int32_t resetIn) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_info.limitPerHour = limitPerHour;
    m_info.pointsSpentThisHour = pointsSpent;
    m_info.pointsResetIn = resetIn;
    m_info.lastUpdated = std::chrono::system_clock::now();
}

bool RateLimiter::updateFromJson(const std::string& jsonResponse) {
    rapidjson::Document doc;
    doc.Parse(jsonResponse.c_str());

    if (doc.HasParseError()) {
        return false;
    }

    const rapidjson::Value* rateLimitData = nullptr;

    if (doc.HasMember("data") &&
        doc["data"].HasMember("rateLimitData")) {
        rateLimitData = &doc["data"]["rateLimitData"];
    } else if (doc.HasMember("rateLimitData")) {
        rateLimitData = &doc["rateLimitData"];
    }

    if (!rateLimitData || !rateLimitData->IsObject()) {
        return false;
    }

    update(json::getInt(*rateLimitData, "limitPerHour"),
           json::getInt(*rateLimitData, "pointsSpentThisHour"),
           json::getInt(*rateLimitData, "pointsResetIn"));
    return true;
}

bool RateLimiter::canProceed(int32_t estimatedCost) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    // No rate limit data yet, so let the request through.
    if (m_info.limitPerHour == 0) {
        return true;
    }

    return (m_info.pointsSpentThisHour + estimatedCost) <= m_info.limitPerHour;
}

RateLimitInfo RateLimiter::getInfo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_info;
}

int32_t RateLimiter::getRecommendedDelay() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Slow down as usage passes 80%.
    if (m_info.isApproachingLimit()) {
        float usage = m_info.usagePercent();
        if (usage > 95.0f) {
            return 1000;
        } else if (usage > 90.0f) {
            return 500;
        } else if (usage > 80.0f) {
            return 100;
        }
    }

    if (m_info.isExceeded()) {
        return m_info.pointsResetIn;
    }

    return 0;
}

void RateLimiter::recordRequest(int32_t estimatedCost) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_info.pointsSpentThisHour += estimatedCost;
}

void RateLimiter::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_info = RateLimitInfo{};
}

} // namespace warcraftlogs
