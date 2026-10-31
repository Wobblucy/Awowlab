#pragma once

#include "Auth/UserOAuthClient.h"
#include "Api/GraphQLClient.h"
#include "Api/RateLimiter.h"
#include "Adapter/UrlParser.h"
#include "Adapter/WclToAwowAdapter.h"
#include "Storage/ResponseCache.h"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>

struct ExtractedData;

namespace warcraftlogs {

// progress is 0.0 to 1.0, or -1 when indeterminate
using ProgressCallback = std::function<void(const std::string& message, float progress)>;

enum class SessionState {
    NotInitialized,     // No URL/code set
    NeedsAuthentication, // URL parsed but user not authenticated
    Ready,              // Authenticated and ready to fetch
    FetchingReport,     // Downloading report metadata
    ReportLoaded,       // Report metadata available
    FetchingFight,      // Downloading fight events
    FightLoaded,        // Fight data converted and ready
    Error
};

/**
 * One WarcraftLogs report: parses the URL or code, signs in when a private
 * report needs it, fetches the fight list and actors, then fetches a fight's
 * events and converts them to ExtractedData. Replies are cached on disk.
 *
 *   WarcraftLogsSession session;
 *   session.initialize("https://www.warcraftlogs.com/reports/abc123?fight=5");
 *
 *   if (session.requiresAuthentication()) {
 *       session.authenticate();
 *   }
 *
 *   session.fetchReport();
 *   const auto& fights = session.getFights();
 *
 *   ExtractedData data = session.loadFight(5);
 */
class WarcraftLogsSession {
public:
    WarcraftLogsSession();
    ~WarcraftLogsSession();

    WarcraftLogsSession(const WarcraftLogsSession&) = delete;
    WarcraftLogsSession& operator=(const WarcraftLogsSession&) = delete;
    WarcraftLogsSession(WarcraftLogsSession&&) noexcept;
    WarcraftLogsSession& operator=(WarcraftLogsSession&&) noexcept;

    // A report URL or the 16-character code. False if neither parses.
    bool initialize(const std::string& urlOrCode);

    // True while user sign-in is in use and has not happened yet
    [[nodiscard]] bool requiresAuthentication() const noexcept;

    // Browser OAuth sign-in
    bool authenticate(ProgressCallback progressCallback = nullptr);

    [[nodiscard]] bool isAuthenticated() const noexcept;

    // Fights and actors
    bool fetchReport(ProgressCallback progressCallback = nullptr);

    /**
     * Fetch the top-ranked players for a boss and write the reply to disk
     * exactly as it arrived.
     *
     * Rankings are not tied to a report, so this needs authentication but no
     * initialize(). RankingSample reads the saved file.
     *
     * @param encounterId WCL encounter id.
     * @param className   e.g. "Priest"; empty asks for every class.
     * @param specName    e.g. "Shadow"; empty asks for every spec.
     * @param difficulty  WCL's own scale: 3 Normal, 4 Heroic, 5 Mythic.
     * @param outputPath  Where to write the JSON.
     * @return Empty on success, otherwise the reason it failed.
     */
    /**
     * Fetch every zone WCL tracks, with its bosses and difficulties, and write
     * the reply to disk as it arrived.
     *
     * This is how the current season's dungeons and raid are discovered without
     * a log or a cached report - rankings belong to the season, not to anything
     * the user has pulled. The encounter ids in the reply are what
     * dumpCharacterRankings takes.
     *
     * @param outputPath Where to write the JSON.
     * @return Empty on success, otherwise the reason it failed.
     */
    [[nodiscard]] std::string dumpWorldZones(const std::string& outputPath);

    /**
     * The season's zones, read from a local copy and fetched only when that
     * copy is missing or a rebuild is asked for.
     *
     * What zones exist, and which bosses are in them, only changes when a patch
     * lands - so re-fetching it on every baseline would spend a query to learn
     * what we already knew. The rebuild is explicit for the same reason: the
     * user knows when a patch has landed, and the app would only be guessing.
     *
     * A rebuild is ignored while the local copy is under a week old. The button
     * is easy to press repeatedly and the answer will not have changed, so the
     * query is not worth spending; outWasFresh says so rather than letting the
     * caller report a fetch that did not happen.
     *
     * @param cachePath  Where the listing is kept.
     * @param forceRefresh Fetch even when the local copy is present.
     * @param outJson    Receives the listing on success.
     * @param outWasFresh Set when a refresh was skipped as too recent.
     * @return Empty on success, otherwise the reason it failed.
     */
    [[nodiscard]] std::string loadWorldZones(const std::string& cachePath,
                                             bool forceRefresh,
                                             std::string& outJson,
                                             bool* outWasFresh = nullptr);

    /**
     * Rankings for one boss, kept on disk and refetched only when the local
     * copy is over a week old.
     *
     * A baseline spans every encounter in a zone, so one covers eight dungeons
     * or ten bosses - ten queries for an answer that barely moves day to day.
     * Rankings shift as the season goes on, but not on the timescale someone
     * rebuilds a baseline, so a week is long enough to stop repeat pulls and
     * short enough to follow the meta.
     *
     * @param outWasFresh Set when the cached copy was used instead of a fetch.
     * @return Empty on success, otherwise the reason it failed.
     */
    [[nodiscard]] std::string loadCharacterRankings(int32_t encounterId,
                                                    const std::string& className,
                                                    const std::string& specName,
                                                    int32_t difficulty,
                                                    const std::string& cachePath,
                                                    bool* outWasFresh = nullptr);

    // Runs a query written by the caller and saves the reply. Meant for
    // asking the schema what a field accepts, which the shape of a reply does
    // not reveal. Ordinary fetching goes through the named queries in
    // Queries.h, so there is one place per question.
    [[nodiscard]] std::string dumpRawQuery(const std::string& query,
                                           const std::string& outputPath);

    // Seconds until the signed-in user's WarcraftLogs points reset, or -1
    // when that can't be read. Costs one request.
    [[nodiscard]] int userPointsResetIn();

    [[nodiscard]] std::string dumpCharacterRankings(int32_t encounterId,
                                                    const std::string& className,
                                                    const std::string& specName,
                                                    int32_t difficulty,
                                                    const std::string& outputPath);

    // Empty until a report is loaded
    [[nodiscard]] const std::vector<WclFight>& getFights() const noexcept;

    // Null until a report is loaded
    [[nodiscard]] const WclReportMetadata* getReportMetadata() const noexcept;

    /**
     * Fetches one fight's events and converts them. Throws std::runtime_error
     * on failure.
     *
     * onlyActorId narrows the fetch to a single player. A raid fight runs to
     * around forty pages of ten thousand events and each page is a query, so
     * studying one player by pulling the whole raid spends about forty queries
     * to keep one player's worth of events. Pass their actor id and the server
     * filters instead.
     *
     * The filter is on the event source, so damage the player took does not
     * come back. Leave it at zero for a replay, which needs everyone.
     */
    ExtractedData loadFight(int32_t fightId,
                            ProgressCallback progressCallback = nullptr,
                            int32_t onlyActorId = 0);

    // The fight named in the URL, if it named one
    [[nodiscard]] std::optional<int32_t> getRequestedFightId() const noexcept;

    [[nodiscard]] SessionState getState() const noexcept;

    [[nodiscard]] const std::string& getLastError() const noexcept;

    [[nodiscard]] const std::string& getReportCode() const noexcept;

    void reset();

    void setCacheEnabled(bool enabled, const std::string& cacheDir = "./wcl_cache");

    // A token from an earlier PKCE sign-in; switches to user auth
    void setUserAccessToken(const std::string& token);

    /**
     * How long a single request may take, in milliseconds.
     *
     * The default is thirty seconds, which a long fight can exceed: a ten
     * minute encounter returns far more per page than a four minute one, and
     * the request times out as a network error rather than as anything that
     * names the cause.
     */
    void setRequestTimeout(int timeoutMs);

    [[nodiscard]] bool isFightCached(int32_t fightId) const;

    // The user's own API client
    void setCustomCredentials(const std::string& clientId, const std::string& clientSecret);

    [[nodiscard]] bool hasCustomCredentials() const noexcept { return m_hasCustomCredentials; }

private:
    bool fetchReportMetadata();
    ExtractedData fetchFightEvents(int32_t fightId,
                                   ProgressCallback progressCallback,
                                   int32_t onlyActorId);
    void ensureGraphQLClient();

    SessionState m_state = SessionState::NotInitialized;
    std::string m_lastError;

    ParsedUrl m_parsedUrl;
    std::string m_reportCode;

    std::unique_ptr<UserOAuthClient> m_userOAuth;
    std::unique_ptr<OAuthClient> m_clientOAuth;  // Fallback for public reports
    bool m_useUserAuth = true;

    std::unique_ptr<GraphQLClient> m_graphqlClient;
    int m_requestTimeoutMs = 0;   // 0 leaves the client's own default
    std::unique_ptr<RateLimiter> m_rateLimiter;

    std::unique_ptr<WclReportMetadata> m_reportMetadata;
    WclToAwowAdapter m_adapter;

    std::unique_ptr<ResponseCache> m_cache;
    bool m_cacheEnabled = true;
    std::string m_cacheDir = "./wcl_cache";

    bool m_hasCustomCredentials = false;
};

} // namespace warcraftlogs
