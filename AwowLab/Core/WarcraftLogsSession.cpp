#include <chrono>
#include <cstring>
#include <fstream>
#include "WarcraftLogsSession.h"
#include "Api/Queries.h"
#include "Auth/Credentials.h"
#include "Database/OutputWriter.h"
#include "Core/ErrorLogger.h"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <stdexcept>
#include <sstream>

namespace warcraftlogs {

namespace {

const WclFight* findFight(const WclReportMetadata& metadata, int32_t fightId) {
    for (const auto& fight : metadata.fights) {
        if (fight.id == fightId) {
            return &fight;
        }
    }
    return nullptr;
}

// A server that never stops paging gives up after this many pages
constexpr int kMaxEventPages = 100;

} // namespace

WarcraftLogsSession::WarcraftLogsSession()
    : m_userOAuth(std::make_unique<UserOAuthClient>())
    , m_clientOAuth(nullptr)  // No fallback - PKCE only (client credentials require secret)
    , m_rateLimiter(std::make_unique<RateLimiter>())
    , m_useUserAuth(true)  // Use PKCE flow by default (no client_secret required)
{
}

WarcraftLogsSession::~WarcraftLogsSession() = default;

WarcraftLogsSession::WarcraftLogsSession(WarcraftLogsSession&&) noexcept = default;
WarcraftLogsSession& WarcraftLogsSession::operator=(WarcraftLogsSession&&) noexcept = default;

bool WarcraftLogsSession::initialize(const std::string& urlOrCode) {
    reset();

    try {
        m_parsedUrl = UrlParser::parse(urlOrCode);
        m_reportCode = m_parsedUrl.reportCode;
        m_state = SessionState::NeedsAuthentication;
        return true;
    }
    catch (const UrlParseException& e) {
        m_lastError = e.what();
        m_state = SessionState::Error;
        return false;
    }
}

bool WarcraftLogsSession::requiresAuthentication() const noexcept {
    // User sign-in is the default because it reaches private reports
    return m_useUserAuth && !isAuthenticated();
}

bool WarcraftLogsSession::authenticate(ProgressCallback progressCallback) {
    if (!m_userOAuth) {
        m_lastError = "OAuth client not initialized";
        m_state = SessionState::Error;
        return false;
    }

    auto authProgress = [&progressCallback](const std::string& status) {
        if (progressCallback) {
            progressCallback(status, -1.0f);  // Indeterminate progress
        }
    };

    AuthResult result = m_userOAuth->authenticate(authProgress);

    if (!result.success) {
        m_lastError = result.errorMessage;
        m_state = SessionState::Error;
        return false;  // Require user to retry PKCE auth
    }

    m_state = SessionState::Ready;
    return true;
}

bool WarcraftLogsSession::isAuthenticated() const noexcept {
    if (m_useUserAuth && m_userOAuth) {
        return m_userOAuth->isAuthenticated();
    }
    // Client credentials don't require explicit authentication
    return true;
}

bool WarcraftLogsSession::fetchReport(ProgressCallback progressCallback) {
    if (m_reportCode.empty()) {
        m_lastError = "No report code set - call initialize() first";
        m_state = SessionState::Error;
        return false;
    }

    m_state = SessionState::FetchingReport;

    // The cache holds the raw metadata JSON
    if (m_cacheEnabled && m_cache) {
        auto cachedMeta = m_cache->loadRawMetadata(m_reportCode);
        if (cachedMeta) {
            if (progressCallback) {
                progressCallback("Loading report from cache...", 0.5f);
            }

            rapidjson::Document metaDoc;
            metaDoc.Parse(cachedMeta->c_str());

            if (!metaDoc.HasParseError()) {
                m_reportMetadata = std::make_unique<WclReportMetadata>(
                    m_adapter.parseReportMetadata(metaDoc)
                );
                m_state = SessionState::ReportLoaded;
                if (progressCallback) {
                    progressCallback("Report loaded from cache", 1.0f);
                }
                return true;
            }
            // Unparseable: fall through and fetch
        }
    }

    if (progressCallback) {
        progressCallback("Fetching report metadata...", 0.0f);
    }

    try {
        ensureGraphQLClient();

        rapidjson::Document variables;
        variables.SetObject();
        auto& alloc = variables.GetAllocator();
        variables.AddMember("code", rapidjson::Value(m_reportCode.c_str(), alloc), alloc);

        if (progressCallback) {
            progressCallback("Querying WarcraftLogs API...", 0.3f);
        }

        rapidjson::Document response = m_graphqlClient->executeQuery(queries::REPORT_METADATA, variables);

        if (progressCallback) {
            progressCallback("Parsing response...", 0.7f);
        }

        if (!response.HasMember("data") ||
            !response["data"].HasMember("reportData") ||
            !response["data"]["reportData"].HasMember("report")) {
            m_lastError = "Invalid response structure from WarcraftLogs API";
            m_state = SessionState::Error;
            return false;
        }

        const auto& reportData = response["data"]["reportData"]["report"];
        noteUserPointsReset(response);

        // Rebuilt as the JSON the adapter reads
        rapidjson::Document metaDoc;
        metaDoc.SetObject();
        auto& metaAlloc = metaDoc.GetAllocator();

        metaDoc.AddMember("report_code", rapidjson::Value(m_reportCode.c_str(), metaAlloc), metaAlloc);

        // Unix epoch ms
        if (reportData.HasMember("startTime")) {
            if (reportData["startTime"].IsDouble()) {
                metaDoc.AddMember("startTime", static_cast<int64_t>(reportData["startTime"].GetDouble()), metaAlloc);
            } else if (reportData["startTime"].IsInt64()) {
                metaDoc.AddMember("startTime", reportData["startTime"].GetInt64(), metaAlloc);
            }
        }

        if (reportData.HasMember("fights") && reportData["fights"].IsArray()) {
            rapidjson::Value fights(rapidjson::kArrayType);
            for (const auto& f : reportData["fights"].GetArray()) {
                rapidjson::Value fight(rapidjson::kObjectType);
                if (f.HasMember("id")) fight.AddMember("id", f["id"].GetInt(), metaAlloc);
                if (f.HasMember("name")) fight.AddMember("name", rapidjson::Value(f["name"].GetString(), metaAlloc), metaAlloc);
                if (f.HasMember("startTime")) fight.AddMember("startTime", f["startTime"].GetUint64(), metaAlloc);
                if (f.HasMember("endTime")) fight.AddMember("endTime", f["endTime"].GetUint64(), metaAlloc);
                if (f.HasMember("encounterID")) fight.AddMember("encounterID", f["encounterID"].GetInt(), metaAlloc);
                if (f.HasMember("difficulty") && !f["difficulty"].IsNull()) {
                    if (f["difficulty"].IsInt()) {
                        fight.AddMember("difficulty", f["difficulty"].GetInt(), metaAlloc);
                    } else if (f["difficulty"].IsDouble()) {
                        fight.AddMember("difficulty", static_cast<int32_t>(f["difficulty"].GetDouble()), metaAlloc);
                    }
                }
                if (f.HasMember("keystoneLevel") && !f["keystoneLevel"].IsNull()) {
                    fight.AddMember("keystoneLevel", f["keystoneLevel"].GetInt(), metaAlloc);
                }
                // Whether the pull killed the boss. A WCL report states this
                // only here - the event stream carries an encounterstart with
                // no matching end - so dropping it on the way into the cache
                // would make every cached fight read as a wipe.
                if (f.HasMember("kill") && f["kill"].IsBool()) {
                    fight.AddMember("kill", f["kill"].GetBool(), metaAlloc);
                }
                fights.PushBack(fight, metaAlloc);
            }
            metaDoc.AddMember("fights", fights, metaAlloc);
        }

        if (reportData.HasMember("masterData") &&
            reportData["masterData"].HasMember("actors") &&
            reportData["masterData"]["actors"].IsArray()) {
            rapidjson::Value actors(rapidjson::kArrayType);
            for (const auto& a : reportData["masterData"]["actors"].GetArray()) {
                rapidjson::Value actor(rapidjson::kObjectType);
                if (a.HasMember("id")) actor.AddMember("id", a["id"].GetInt(), metaAlloc);
                if (a.HasMember("gameID") && a["gameID"].IsNumber()) actor.AddMember("gameID", a["gameID"].GetDouble(), metaAlloc);
                if (a.HasMember("name")) actor.AddMember("name", rapidjson::Value(a["name"].GetString(), metaAlloc), metaAlloc);
                if (a.HasMember("type")) actor.AddMember("type", rapidjson::Value(a["type"].GetString(), metaAlloc), metaAlloc);
                if (a.HasMember("subType") && a["subType"].IsString()) actor.AddMember("subType", rapidjson::Value(a["subType"].GetString(), metaAlloc), metaAlloc);
                if (a.HasMember("petOwner") && !a["petOwner"].IsNull()) actor.AddMember("petOwner", a["petOwner"].GetInt(), metaAlloc);
                if (a.HasMember("server") && a["server"].IsString()) actor.AddMember("server", rapidjson::Value(a["server"].GetString(), metaAlloc), metaAlloc);
                if (a.HasMember("icon") && a["icon"].IsString()) actor.AddMember("icon", rapidjson::Value(a["icon"].GetString(), metaAlloc), metaAlloc);
                actors.PushBack(actor, metaAlloc);
            }
            metaDoc.AddMember("actors", actors, metaAlloc);
        }

        // Cached as the raw reply, before parsing
        if (m_cacheEnabled && m_cache) {
            rapidjson::StringBuffer buffer;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
            metaDoc.Accept(writer);
            m_cache->saveRawMetadata(m_reportCode, buffer.GetString());
        }

        m_reportMetadata = std::make_unique<WclReportMetadata>(
            m_adapter.parseReportMetadata(metaDoc)
        );

        m_state = SessionState::ReportLoaded;

        if (progressCallback) {
            progressCallback("Report loaded successfully", 1.0f);
        }

        return true;
    }
    catch (const GraphQLException& e) {
        m_lastError = std::string("GraphQL error: ") + e.what();
        m_state = SessionState::Error;
        return false;
    }
    catch (const http::HttpException& e) {
        m_lastError = std::string("HTTP error: ") + e.what();
        m_state = SessionState::Error;
        return false;
    }
    catch (const std::exception& e) {
        m_lastError = std::string("Error: ") + e.what();
        m_state = SessionState::Error;
        return false;
    }
}

const std::vector<WclFight>& WarcraftLogsSession::getFights() const noexcept {
    static const std::vector<WclFight> empty;
    if (m_reportMetadata) {
        return m_reportMetadata->fights;
    }
    return empty;
}

const WclReportMetadata* WarcraftLogsSession::getReportMetadata() const noexcept {
    return m_reportMetadata.get();
}

ExtractedData WarcraftLogsSession::loadFight(int32_t fightId,
                                            ProgressCallback progressCallback,
                                            int32_t onlyActorId) {
    if (!m_reportMetadata) {
        std::string errorMsg = "Report not loaded - call fetchReport() first";
        AWOW_LOG_ERROR("WCL", errorMsg);
        throw std::runtime_error(errorMsg);
    }

    const WclFight* fight = findFight(*m_reportMetadata, fightId);
    if (!fight) {
        std::string errorMsg = "Fight ID " + std::to_string(fightId) + " not found in report";
        AWOW_LOG_ERROR("WCL", errorMsg);
        throw std::runtime_error(errorMsg);
    }

    m_state = SessionState::FetchingFight;

    return fetchFightEvents(fightId, progressCallback, onlyActorId);
}

std::optional<int32_t> WarcraftLogsSession::getRequestedFightId() const noexcept {
    if (m_parsedUrl.fightId.has_value()) {
        return m_parsedUrl.fightId;
    }
    if (m_parsedUrl.fightLast && m_reportMetadata && !m_reportMetadata->fights.empty()) {
        return m_reportMetadata->fights.back().id;
    }
    return std::nullopt;
}

SessionState WarcraftLogsSession::getState() const noexcept {
    return m_state;
}

const std::string& WarcraftLogsSession::getLastError() const noexcept {
    return m_lastError;
}

const std::string& WarcraftLogsSession::getReportCode() const noexcept {
    return m_reportCode;
}

void WarcraftLogsSession::reset() {
    m_state = SessionState::NotInitialized;
    m_lastError.clear();
    m_parsedUrl = ParsedUrl{};
    m_reportCode.clear();
    m_reportMetadata.reset();
    m_graphqlClient.reset();
    m_useUserAuth = true;  // Use PKCE flow by default (no client_secret required)
}

void WarcraftLogsSession::setCacheEnabled(bool enabled, const std::string& cacheDir) {
    m_cacheEnabled = enabled;
    m_cacheDir = cacheDir;

    if (enabled) {
        m_cache = std::make_unique<ResponseCache>(cacheDir);
    } else {
        m_cache.reset();
    }
}

bool WarcraftLogsSession::isFightCached(int32_t fightId) const {
    if (!m_cacheEnabled || !m_cache || m_reportCode.empty()) {
        return false;
    }
    return m_cache->hasCachedFightEvents(m_reportCode, fightId);
}

namespace {

// Walks a reply for an "error" string. GraphQL puts these inside the payload
// rather than failing the request, and they can sit at any depth - for
// characterRankings it is under data.worldData.encounter.characterRankings.
const char* findEmbeddedError(const rapidjson::Value& value) {
    if (value.IsObject()) {
        for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it) {
            if (std::strcmp(it->name.GetString(), "error") == 0 &&
                it->value.IsString()) {
                return it->value.GetString();
            }
            if (const char* nested = findEmbeddedError(it->value)) return nested;
        }
    } else if (value.IsArray()) {
        for (const auto& element : value.GetArray()) {
            if (const char* nested = findEmbeddedError(element)) return nested;
        }
    }
    return nullptr;
}

// Runs a query on the session's own client and writes the reply out untouched
std::string runQueryToFile(warcraftlogs::GraphQLClient* client,
                           const char* query,
                           const rapidjson::Document* variables,
                           const std::string& outputPath,
                           const char* what) {
    rapidjson::Document response;
    try {
        response = variables ? client->executeQuery(query, *variables)
                             : client->executeQuery(query);
    } catch (const std::exception& e) {
        return std::string("query failed: ") + e.what();
    }

    // A rejected argument comes back as HTTP 200 with an "error" string inside
    // the payload; without this check the file is written and the caller
    // reports success.
    if (const char* reason = findEmbeddedError(response)) {
        return std::string(what) + ": " + reason;
    }

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    response.Accept(writer);

    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) return "cannot write " + outputPath;
    out.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
    out.flush();
    if (!out.good()) return "write failed for " + outputPath;

    AWOW_LOG_INFO("RANKINGS", std::string("Wrote ") + what + " to " + outputPath);
    return {};
}

// The season list and the rankings are asked for far more often than they
// change, and a baseline spends one query per encounter, so a repeat press
// reads the local copy instead.
//
// Zones only change when a patch lands; the rebuild button covers that day.
constexpr std::chrono::hours kSeasonListMaxAge{24 * 7};

// Rankings move over a season, but slowly enough that a week still follows
// the meta while stopping repeat baselines from costing ten queries each.
constexpr std::chrono::hours kRankingsMaxAge{24 * 7};

// True when a local copy exists and is younger than maxAge
bool isFresherThan(const std::string& path, std::chrono::hours maxAge) {
    if (!std::filesystem::exists(path)) return false;

    std::error_code ec;
    const auto written = std::filesystem::last_write_time(path, ec);
    if (ec) return false;   // cannot tell how old it is, so treat it as stale

    const auto age = std::filesystem::file_time_type::clock::now() - written;
    return age < maxAge;
}

// The whole file, or empty if it cannot be opened
std::string readWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

} // namespace

std::string WarcraftLogsSession::dumpWorldZones(const std::string& outputPath) {
    try {
        ensureGraphQLClient();
    } catch (const std::exception& e) {
        return std::string("not signed in: ") + e.what();
    }
    // No variables: this asks for the whole catalogue
    return runQueryToFile(m_graphqlClient.get(), warcraftlogs::queries::WORLD_ZONES,
                          nullptr, outputPath, "zone listing");
}

std::string WarcraftLogsSession::loadWorldZones(const std::string& cachePath,
                                                bool forceRefresh,
                                                std::string& outJson,
                                                bool* outWasFresh) {
    outJson.clear();
    if (outWasFresh) *outWasFresh = false;

    // A rebuild on a copy under kSeasonListMaxAge old is dropped: the button
    // is easy to press twice and the answer will not have changed.
    if (forceRefresh && isFresherThan(cachePath, kSeasonListMaxAge)) {
        forceRefresh = false;
        if (outWasFresh) *outWasFresh = true;
    }

    if (!forceRefresh && std::filesystem::exists(cachePath)) {
        outJson = readWholeFile(cachePath);
        // A truncated or empty file is worse than none - fall through and
        // refetch rather than handing back something half-written
        if (!outJson.empty()) {
            AWOW_LOG_INFO("RANKINGS", "Season listing read from " + cachePath);
            return {};
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(cachePath).parent_path(), ec);

    const std::string error = dumpWorldZones(cachePath);
    if (!error.empty()) return error;

    outJson = readWholeFile(cachePath);
    if (outJson.empty()) return "wrote the listing but cannot read it back";
    return {};
}

std::string WarcraftLogsSession::loadCharacterRankings(
    int32_t encounterId,
    const std::string& className,
    const std::string& specName,
    int32_t difficulty,
    const std::string& cachePath,
    bool* outWasFresh
) {
    if (outWasFresh) *outWasFresh = false;

    if (isFresherThan(cachePath, kRankingsMaxAge)) {
        if (outWasFresh) *outWasFresh = true;
        return {};
    }

    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(cachePath).parent_path(), ec);

    return dumpCharacterRankings(encounterId, className, specName, difficulty,
                                 cachePath);
}

std::string WarcraftLogsSession::dumpRawQuery(const std::string& query,
                                              const std::string& outputPath) {
    if (query.empty()) return "a query is required";

    try {
        ensureGraphQLClient();
    } catch (const std::exception& e) {
        return std::string("not signed in: ") + e.what();
    }

    return runQueryToFile(m_graphqlClient.get(), query.c_str(), nullptr,
                          outputPath, "raw query");
}

void WarcraftLogsSession::noteUserPointsReset(const rapidjson::Document& response) {
    const auto& data = response["data"];
    if (data.HasMember("rateLimitData") && data["rateLimitData"].IsObject()) {
        const auto& rate = data["rateLimitData"];
        if (rate.HasMember("userPointsResetIn") && rate["userPointsResetIn"].IsInt()) {
            m_lastUserPointsResetIn = rate["userPointsResetIn"].GetInt();
        }
    }
}

int WarcraftLogsSession::userPointsResetIn() {
    try {
        ensureGraphQLClient();
        return m_graphqlClient->fetchRateLimitInfo().userPointsResetIn;
    } catch (const std::exception& e) {
        AWOW_LOG_ERROR("WCL", std::string("Could not read the user points reset time: ") + e.what());
        return -1;
    }
}

std::string WarcraftLogsSession::dumpCharacterRankings(
    int32_t encounterId,
    const std::string& className,
    const std::string& specName,
    int32_t difficulty,
    const std::string& outputPath
) {
    if (encounterId <= 0) return "an encounter id is required";

    try {
        ensureGraphQLClient();
    } catch (const std::exception& e) {
        return std::string("not signed in: ") + e.what();
    }

    rapidjson::Document variables;
    variables.SetObject();
    auto& allocator = variables.GetAllocator();
    variables.AddMember("encounterId", encounterId, allocator);
    variables.AddMember("difficulty", difficulty, allocator);
    variables.AddMember("page", 1, allocator);

    // Left out entirely when empty, which asks for every class and spec. Safer
    // than sending a name the server might not recognise.
    if (!className.empty()) {
        variables.AddMember("className",
                            rapidjson::Value(className.c_str(), allocator), allocator);
    }
    if (!specName.empty()) {
        variables.AddMember("specName",
                            rapidjson::Value(specName.c_str(), allocator), allocator);
    }

    return runQueryToFile(m_graphqlClient.get(),
                          warcraftlogs::queries::CHARACTER_RANKINGS,
                          &variables, outputPath, "rankings reply");
}

void WarcraftLogsSession::ensureGraphQLClient() {
    if (m_graphqlClient) {
        return;
    }

    if (m_useUserAuth && m_userOAuth && m_userOAuth->isAuthenticated()) {
        // Use PKCE/User OAuth for private report access
        // This uses /api/v2/user endpoint which is required for PKCE tokens
        m_graphqlClient = std::make_unique<GraphQLClient>(*m_userOAuth, *m_rateLimiter);
    } else if (m_clientOAuth && m_hasCustomCredentials) {
        // Only use client credentials if user explicitly provided their own API credentials
        // This uses /api/v2/client endpoint
        m_graphqlClient = std::make_unique<GraphQLClient>(*m_clientOAuth, *m_rateLimiter);
    } else {
        std::string errorMsg = "User authentication required - please authenticate with WarcraftLogs";
        AWOW_LOG_ERROR("WCL", errorMsg);
        throw std::runtime_error(errorMsg);
    }

    if (m_requestTimeoutMs > 0) m_graphqlClient->setTimeout(m_requestTimeoutMs);
}

namespace {
void safeProgressCallback(const warcraftlogs::ProgressCallback& callback,
                          const std::string& message, float progress) {
    if (callback) {
        try {
            callback(message, progress);
        } catch (const std::exception& e) {
            AWOW_LOG_WARNING("WCL", "Progress callback failed: " + std::string(e.what()));
        } catch (...) {
            AWOW_LOG_WARNING("WCL", "Progress callback failed with unknown exception");
        }
    }
}
} // namespace

ExtractedData WarcraftLogsSession::fetchFightEvents(int32_t fightId,
                                                   ProgressCallback progressCallback,
                                                   int32_t onlyActorId) {
    const WclFight* fight = findFight(*m_reportMetadata, fightId);
    if (!fight) {
        std::string errorMsg = "Fight not found: " + std::to_string(fightId);
        AWOW_LOG_ERROR("WCL", errorMsg);
        throw std::runtime_error(errorMsg);
    }

    if (m_cacheEnabled && m_cache && m_cache->hasCachedFightEvents(m_reportCode, fightId)) {
        std::vector<std::string> cachedPages = m_cache->loadFightEventsPages(m_reportCode, fightId);
        if (!cachedPages.empty()) {
            safeProgressCallback(progressCallback, "Loading fight from cache...", 0.0f);

            // Reset adapter state for new fight (preserves state across pages)
            m_adapter.resetFightState();

            std::vector<ExtractedData> pages;
            pages.reserve(cachedPages.size());

            for (size_t i = 0; i < cachedPages.size(); ++i) {
                float progress = static_cast<float>(i) / cachedPages.size();
                safeProgressCallback(progressCallback, "Converting cached events...", progress);

                pages.push_back(m_adapter.convertEvents(
                    cachedPages[i],
                    *m_reportMetadata,
                    fight->startTime
                ));
            }

            safeProgressCallback(progressCallback, "Fight loaded from cache", 1.0f);

            m_state = SessionState::FightLoaded;
            return WclToAwowAdapter::mergePages(pages);
        }
    }

    ensureGraphQLClient();

    // Reset adapter state for new fight (preserves state across pages)
    m_adapter.resetFightState();

    std::vector<ExtractedData> pages;
    std::vector<std::string> rawPages;
    uint64_t currentTime = fight->startTime;
    int pageNum = 0;

    while (currentTime < fight->endTime) {
        ++pageNum;

        float progress = static_cast<float>(currentTime - fight->startTime) /
                       static_cast<float>(fight->endTime - fight->startTime);
        safeProgressCallback(progressCallback, "Fetching page " + std::to_string(pageNum) + "...", progress * 0.8f);

        rapidjson::Document variables;
        variables.SetObject();
        auto& alloc = variables.GetAllocator();

        variables.AddMember("code", rapidjson::Value(m_reportCode.c_str(), alloc), alloc);
        variables.AddMember("start", static_cast<double>(currentTime), alloc);
        variables.AddMember("end", static_cast<double>(fight->endTime), alloc);

        rapidjson::Value fightIds(rapidjson::kArrayType);
        fightIds.PushBack(fightId, alloc);
        variables.AddMember("fightIDs", fightIds, alloc);

        if (onlyActorId != 0) {
            variables.AddMember("sourceID", onlyActorId, alloc);
        }

        rapidjson::Document response = m_graphqlClient->executeQuery(
            onlyActorId != 0 ? queries::FIGHT_EVENTS_FOR_ACTOR
                             : queries::FIGHT_EVENTS,
            variables);

        if (!response.HasMember("data") ||
            !response["data"].HasMember("reportData") ||
            !response["data"]["reportData"].HasMember("report") ||
            !response["data"]["reportData"]["report"].HasMember("events")) {
            std::string errorMsg = "Invalid events response structure from WarcraftLogs API";
            AWOW_LOG_ERROR("WCL", errorMsg);
            throw std::runtime_error(errorMsg);
        }

        const auto& eventsData = response["data"]["reportData"]["report"]["events"];
        noteUserPointsReset(response);

        if (eventsData.HasMember("data") && eventsData["data"].IsArray()) {
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            eventsData["data"].Accept(writer);
            std::string eventsJson = buffer.GetString();

            rapidjson::Document eventsDoc;
            eventsDoc.Parse(eventsJson.c_str());

            pages.push_back(m_adapter.convertEvents(
                eventsDoc,
                *m_reportMetadata,
                fight->startTime
            ));
            rawPages.push_back(std::move(eventsJson));
        }

        if (eventsData.HasMember("nextPageTimestamp") && !eventsData["nextPageTimestamp"].IsNull()) {
            currentTime = static_cast<uint64_t>(eventsData["nextPageTimestamp"].GetDouble());
        } else {
            break;  // No more pages
        }

        if (pageNum > kMaxEventPages) {
            std::string errorMsg = "Too many event pages - possible infinite loop (page " + std::to_string(pageNum) + ")";
            AWOW_LOG_ERROR("WCL", errorMsg);
            throw std::runtime_error(errorMsg);
        }
    }

    if (m_cacheEnabled && m_cache && !rawPages.empty()) {
        for (size_t i = 0; i < rawPages.size(); ++i) {
            m_cache->saveFightEventsPage(m_reportCode, fightId, static_cast<int32_t>(i + 1), rawPages[i]);
        }
    }

    safeProgressCallback(progressCallback, "Processing events...", 0.9f);

    ExtractedData result = WclToAwowAdapter::mergePages(pages);

    safeProgressCallback(progressCallback, "Fight loaded successfully", 1.0f);

    m_state = SessionState::FightLoaded;
    return result;
}

void WarcraftLogsSession::setRequestTimeout(int timeoutMs) {
    // Remembered rather than applied straight away: the client is built lazily
    // by ensureGraphQLClient, which may not have run yet.
    m_requestTimeoutMs = timeoutMs;
    if (m_graphqlClient) m_graphqlClient->setTimeout(timeoutMs);
}

void WarcraftLogsSession::setUserAccessToken(const std::string& token) {
    if (token.empty()) {
        m_useUserAuth = false;
        return;
    }

    if (!m_userOAuth) {
        m_userOAuth = std::make_unique<UserOAuthClient>();
    }

    m_userOAuth->setToken(token);
    m_useUserAuth = true;

    // Rebuilt lazily with the new token
    m_graphqlClient.reset();
}

void WarcraftLogsSession::setCustomCredentials(const std::string& clientId, const std::string& clientSecret) {
    m_clientOAuth = std::make_unique<OAuthClient>(clientId, clientSecret);
    m_hasCustomCredentials = true;

    // Rebuilt lazily with the new credentials
    m_graphqlClient.reset();
}

} // namespace warcraftlogs
