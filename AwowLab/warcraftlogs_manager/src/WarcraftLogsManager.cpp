#include "WarcraftLogsManager.h"
#include "Api/Queries.h"
#include "Adapter/WclJsonHelpers.h"

#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

namespace warcraftlogs {

WarcraftLogsManager::WarcraftLogsManager()
    : m_oauthClient(std::make_unique<OAuthClient>())
    , m_rateLimiter(std::make_unique<RateLimiter>())
    , m_graphqlClient(std::make_unique<GraphQLClient>(*m_oauthClient, *m_rateLimiter))
    , m_cache(std::make_unique<ResponseCache>())
    , m_cachingEnabled(true)
    , m_progressCallback(nullptr)
{
}

WarcraftLogsManager::WarcraftLogsManager(const std::string& clientId, const std::string& clientSecret)
    : m_oauthClient(std::make_unique<OAuthClient>(clientId, clientSecret))
    , m_rateLimiter(std::make_unique<RateLimiter>())
    , m_graphqlClient(std::make_unique<GraphQLClient>(*m_oauthClient, *m_rateLimiter))
    , m_cache(std::make_unique<ResponseCache>())
    , m_cachingEnabled(true)
    , m_progressCallback(nullptr)
{
}

WarcraftLogsManager::~WarcraftLogsManager() = default;

WarcraftLogsManager::WarcraftLogsManager(WarcraftLogsManager&&) noexcept = default;
WarcraftLogsManager& WarcraftLogsManager::operator=(WarcraftLogsManager&&) noexcept = default;

void WarcraftLogsManager::setCacheDirectory(const std::filesystem::path& cacheDir) {
    m_cache->setCacheDirectory(cacheDir);
}

void WarcraftLogsManager::setCachingEnabled(bool enabled) {
    m_cachingEnabled = enabled;
}

bool WarcraftLogsManager::isCachingEnabled() const noexcept {
    return m_cachingEnabled;
}

void WarcraftLogsManager::setTimeout(int timeoutMs) {
    m_graphqlClient->setTimeout(timeoutMs);
}

void WarcraftLogsManager::setProgressCallback(ProgressCallback callback) {
    m_progressCallback = std::move(callback);
}

Report WarcraftLogsManager::fetchReport(const std::string& reportCode, bool forceRefresh) {
    if (m_cachingEnabled && !forceRefresh && m_cache->hasCachedReport(reportCode)) {
        reportProgress(1, 1, "Loading report from cache...");
        auto cached = m_cache->loadReport(reportCode);
        if (cached) {
            return *cached;
        }
    }

    reportProgress(0, 1, "Fetching report metadata...");

    rapidjson::Document variables;
    variables.SetObject();
    auto& allocator = variables.GetAllocator();
    variables.AddMember("code", rapidjson::Value(reportCode.c_str(), allocator), allocator);

    auto response = m_graphqlClient->executeQuery(queries::REPORT_METADATA, variables);

    Report report = parseReportResponse(response, reportCode);

    if (m_cachingEnabled) {
        m_cache->saveReport(report);
    }

    reportProgress(1, 1, "Report metadata loaded");
    return report;
}

std::vector<Event> WarcraftLogsManager::fetchFightEvents(
    const std::string& reportCode,
    int32_t fightId,
    bool forceRefresh
) {
    auto rawPages = fetchFightEventsRaw(reportCode, fightId, forceRefresh);

    std::vector<Event> allEvents;

    for (const auto& pageJson : rawPages) {
        rapidjson::Document doc;
        doc.Parse(pageJson.c_str());

        if (doc.HasParseError() || !doc.IsArray()) {
            continue;
        }

        auto events = parseEventsResponse(doc);
        allEvents.insert(allEvents.end(),
            std::make_move_iterator(events.begin()),
            std::make_move_iterator(events.end()));
    }

    return allEvents;
}

std::vector<std::string> WarcraftLogsManager::fetchFightEventsRaw(
    const std::string& reportCode,
    int32_t fightId,
    bool forceRefresh
) {
    if (m_cachingEnabled && !forceRefresh && m_cache->hasCachedFightEvents(reportCode, fightId)) {
        reportProgress(1, 1, "Loading events from cache...");
        return m_cache->loadFightEventsPages(reportCode, fightId);
    }

    // The fight's start and end come from the report metadata.
    Report report = fetchReport(reportCode, false);

    auto fightOpt = report.findFight(fightId);
    if (!fightOpt) {
        throw GraphQLException("Fight ID " + std::to_string(fightId) + " not found in report");
    }

    const Fight& fight = *fightOpt;
    if (!fight.isValid()) {
        throw GraphQLException("Fight has invalid duration");
    }

    return fetchFightEventsFromApi(reportCode, fightId, fight.startTime, fight.endTime);
}

RateLimitInfo WarcraftLogsManager::getRateLimitInfo() const {
    return m_rateLimiter->getInfo();
}

RateLimitInfo WarcraftLogsManager::fetchRateLimitInfo() {
    return m_graphqlClient->fetchRateLimitInfo();
}

bool WarcraftLogsManager::canProceed() const {
    return m_rateLimiter->canProceed();
}

bool WarcraftLogsManager::isReportCached(const std::string& reportCode) const {
    return m_cache->hasCachedReport(reportCode);
}

bool WarcraftLogsManager::areFightEventsCached(const std::string& reportCode, int32_t fightId) const {
    return m_cache->hasCachedFightEvents(reportCode, fightId);
}

void WarcraftLogsManager::clearReportCache(const std::string& reportCode) {
    m_cache->clearReportCache(reportCode);
}

void WarcraftLogsManager::clearAllCache() {
    m_cache->clearAllCache();
}

std::vector<std::string> WarcraftLogsManager::getCachedReports() const {
    return m_cache->getCachedReportCodes();
}

bool WarcraftLogsManager::testAuthentication() {
    try {
        (void)m_oauthClient->getToken();
        return true;
    } catch (...) {
        return false;
    }
}

void WarcraftLogsManager::clearAuthToken() {
    m_oauthClient->clearToken();
}

Report WarcraftLogsManager::parseReportResponse(
    const rapidjson::Document& response,
    const std::string& reportCode
) {
    Report report;
    report.code = reportCode;

    if (!response.HasMember("data") ||
        !response["data"].HasMember("reportData") ||
        !response["data"]["reportData"].HasMember("report")) {
        throw GraphQLException("Invalid report response structure");
    }

    const auto& reportData = response["data"]["reportData"]["report"];

    if (reportData.IsNull()) {
        throw GraphQLException("Report not found or is null");
    }

    // Unix epoch ms.
    if (auto startTime = json::getTimestamp(reportData, "startTime")) {
        report.startTime = *startTime;
    }

    if (reportData.HasMember("fights") && reportData["fights"].IsArray()) {
        for (const auto& f : reportData["fights"].GetArray()) {
            Fight fight;
            fight.id = json::getInt(f, "id");
            fight.name = json::getString(f, "name");
            fight.startTime = json::getInt64(f, "startTime");
            fight.endTime = json::getInt64(f, "endTime");
            fight.encounterID = json::getInt(f, "encounterID");
            fight.difficulty = json::getInt(f, "difficulty");
            fight.keystoneLevel = json::getInt(f, "keystoneLevel");
            // Null on a trash fight, which is neither a kill nor a wipe.
            fight.kill = json::getBool(f, "kill");
            report.fights.push_back(fight);
        }
    }

    if (reportData.HasMember("masterData") &&
        reportData["masterData"].IsObject() &&
        reportData["masterData"].HasMember("actors") &&
        reportData["masterData"]["actors"].IsArray()) {

        for (const auto& a : reportData["masterData"]["actors"].GetArray()) {
            Actor actor;
            actor.id = json::getInt(a, "id");
            // A JSON number that is sometimes sent as a double.
            if (a.HasMember("gameID") && a["gameID"].IsNumber()) {
                actor.gameID = static_cast<int64_t>(a["gameID"].GetDouble());
            }
            actor.name = json::getString(a, "name");
            actor.type = parseActorType(json::getString(a, "type"));
            actor.subType = json::getString(a, "subType");
            actor.petOwner = json::getInt(a, "petOwner", actor.petOwner);
            actor.server = json::getString(a, "server");
            actor.icon = json::getString(a, "icon");
            report.actors.push_back(actor);
        }
    }

    report.buildActorMap();

    return report;
}

std::vector<Event> WarcraftLogsManager::parseEventsResponse(const rapidjson::Document& response) {
    std::vector<Event> events;

    // Either the events array itself or a full GraphQL response around it.
    const rapidjson::Value* eventsArray = nullptr;

    if (response.IsArray()) {
        eventsArray = &response;
    } else if (response.HasMember("data") &&
               response["data"].HasMember("reportData") &&
               response["data"]["reportData"].HasMember("report") &&
               response["data"]["reportData"]["report"].HasMember("events") &&
               response["data"]["reportData"]["report"]["events"].HasMember("data")) {
        eventsArray = &response["data"]["reportData"]["report"]["events"]["data"];
    }

    if (!eventsArray || !eventsArray->IsArray()) {
        return events;
    }

    for (const auto& e : eventsArray->GetArray()) {
        Event event;
        event.timestamp = json::getInt64(e, "timestamp", event.timestamp);
        event.type = json::getString(e, "type", event.type);
        event.fight = json::getInt(e, "fight", event.fight);
        event.sourceID = json::getInt(e, "sourceID", event.sourceID);
        event.targetID = json::getInt(e, "targetID", event.targetID);

        // Optional fields stay empty unless the event carries them.
        if (e.HasMember("abilityGameID") && e["abilityGameID"].IsInt()) {
            event.abilityGameID = e["abilityGameID"].GetInt();
        }
        if (e.HasMember("amount") && e["amount"].IsInt64()) {
            event.amount = e["amount"].GetInt64();
        }
        if (e.HasMember("absorbed") && e["absorbed"].IsInt64()) {
            event.absorbed = e["absorbed"].GetInt64();
        }
        if (e.HasMember("x") && e["x"].IsNumber()) {
            event.x = static_cast<float>(e["x"].GetDouble());
        }
        if (e.HasMember("y") && e["y"].IsNumber()) {
            event.y = static_cast<float>(e["y"].GetDouble());
        }
        if (e.HasMember("mapID") && e["mapID"].IsInt()) {
            event.mapID = e["mapID"].GetInt();
        }
        if (e.HasMember("facing") && e["facing"].IsNumber()) {
            event.facing = static_cast<float>(e["facing"].GetDouble());
        }

        // Kept whole for fields not parsed above.
        event.rawData.CopyFrom(e, event.rawData.GetAllocator());

        events.push_back(std::move(event));
    }

    return events;
}

std::vector<std::string> WarcraftLogsManager::fetchFightEventsFromApi(
    const std::string& reportCode,
    int32_t fightId,
    int64_t startTime,
    int64_t endTime
) {
    std::vector<std::string> pages;
    int64_t pageStart = startTime;
    int page = 1;

    reportProgress(0, 0, "Fetching fight events...");

    while (pageStart < endTime) {
        reportProgress(page, 0, "Fetching page " + std::to_string(page) + "...");

        rapidjson::Document variables;
        variables.SetObject();
        auto& allocator = variables.GetAllocator();

        variables.AddMember("code", rapidjson::Value(reportCode.c_str(), allocator), allocator);
        variables.AddMember("start", static_cast<double>(pageStart), allocator);
        variables.AddMember("end", static_cast<double>(endTime), allocator);

        rapidjson::Value fightIDs(rapidjson::kArrayType);
        fightIDs.PushBack(fightId, allocator);
        variables.AddMember("fightIDs", fightIDs, allocator);

        auto response = m_graphqlClient->executeQuery(queries::FIGHT_EVENTS, variables);

        if (!response.HasMember("data") ||
            !response["data"].HasMember("reportData") ||
            !response["data"]["reportData"].HasMember("report") ||
            !response["data"]["reportData"]["report"].HasMember("events")) {
            break;
        }

        const auto& eventsObj = response["data"]["reportData"]["report"]["events"];

        if (!eventsObj.HasMember("data") || !eventsObj["data"].IsArray()) {
            break;
        }

        const auto& eventsData = eventsObj["data"];
        if (eventsData.Empty()) {
            break;
        }

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        eventsData.Accept(writer);

        std::string eventsJson = buffer.GetString();
        pages.push_back(eventsJson);

        if (m_cachingEnabled) {
            m_cache->saveFightEventsPage(reportCode, fightId, page, eventsJson);
        }

        // Absent or null on the last page.
        auto nextPageStart = json::getTimestamp(eventsObj, "nextPageTimestamp");
        if (!nextPageStart) {
            break;
        }
        pageStart = *nextPageStart;

        ++page;
    }

    reportProgress(page, page, "Events loaded (" + std::to_string(page) + " pages)");
    return pages;
}

void WarcraftLogsManager::reportProgress(int current, int total, const std::string& message) {
    if (m_progressCallback) {
        m_progressCallback(current, total, message);
    }
}

} // namespace warcraftlogs
