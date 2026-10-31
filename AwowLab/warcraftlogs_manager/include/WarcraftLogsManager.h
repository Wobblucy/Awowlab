#pragma once

#include "Auth/OAuthClient.h"
#include "Api/GraphQLClient.h"
#include "Api/RateLimiter.h"
#include "Storage/ResponseCache.h"
#include "Models/Report.h"
#include "Models/Event.h"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <filesystem>

namespace warcraftlogs {

using ProgressCallback = std::function<void(int current, int total, const std::string& message)>;

// One entry point for the WarcraftLogs API: sign-in, GraphQL queries, rate
// limiting and the response cache.
class WarcraftLogsManager {
public:
    // Uses the built-in client id.
    WarcraftLogsManager();

    WarcraftLogsManager(const std::string& clientId, const std::string& clientSecret);

    ~WarcraftLogsManager();

    WarcraftLogsManager(const WarcraftLogsManager&) = delete;
    WarcraftLogsManager& operator=(const WarcraftLogsManager&) = delete;

    WarcraftLogsManager(WarcraftLogsManager&&) noexcept;
    WarcraftLogsManager& operator=(WarcraftLogsManager&&) noexcept;

    void setCacheDirectory(const std::filesystem::path& cacheDir);
    void setCachingEnabled(bool enabled);
    [[nodiscard]] bool isCachingEnabled() const noexcept;
    void setTimeout(int timeoutMs);
    void setProgressCallback(ProgressCallback callback);

    // Report metadata (fights and actors), from the cache when caching is on
    // and forceRefresh is false. Throws GraphQLException on API errors and
    // HttpException on network errors.
    [[nodiscard]] Report fetchReport(const std::string& reportCode, bool forceRefresh = false);

    // Every page of the fight's events.
    [[nodiscard]] std::vector<Event> fetchFightEvents(
        const std::string& reportCode,
        int32_t fightId,
        bool forceRefresh = false
    );

    // The same pages as raw JSON, for callers that parse them themselves.
    [[nodiscard]] std::vector<std::string> fetchFightEventsRaw(
        const std::string& reportCode,
        int32_t fightId,
        bool forceRefresh = false
    );

    [[nodiscard]] RateLimitInfo getRateLimitInfo() const;

    // Asks the API for the current figures.
    RateLimitInfo fetchRateLimitInfo();

    [[nodiscard]] bool canProceed() const;

    [[nodiscard]] bool isReportCached(const std::string& reportCode) const;
    [[nodiscard]] bool areFightEventsCached(const std::string& reportCode, int32_t fightId) const;
    void clearReportCache(const std::string& reportCode);
    void clearAllCache();
    [[nodiscard]] std::vector<std::string> getCachedReports() const;

    // True if a token can be obtained.
    [[nodiscard]] bool testAuthentication();

    void clearAuthToken();

private:
    Report parseReportResponse(const rapidjson::Document& response, const std::string& reportCode);

    std::vector<Event> parseEventsResponse(const rapidjson::Document& response);

    std::vector<std::string> fetchFightEventsFromApi(
        const std::string& reportCode,
        int32_t fightId,
        int64_t startTime,
        int64_t endTime
    );

    void reportProgress(int current, int total, const std::string& message);

    std::unique_ptr<OAuthClient> m_oauthClient;
    std::unique_ptr<RateLimiter> m_rateLimiter;
    std::unique_ptr<GraphQLClient> m_graphqlClient;
    std::unique_ptr<ResponseCache> m_cache;

    bool m_cachingEnabled;
    ProgressCallback m_progressCallback;
};

} // namespace warcraftlogs
