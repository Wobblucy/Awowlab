#include "Storage/ResponseCache.h"
#include "Adapter/WclJsonHelpers.h"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
// Windows macros that clash with RapidJSON.
#ifdef GetObject
#undef GetObject
#endif
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace warcraftlogs {

namespace {

std::filesystem::path getDefaultCacheDir() {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        return std::filesystem::path(path) / "AwowLab" / "warcraftlogs_cache";
    }
    return std::filesystem::current_path() / "warcraftlogs_cache";
#else
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            home = pw->pw_dir;
        }
    }
    if (home) {
        return std::filesystem::path(home) / ".cache" / "awowlab" / "warcraftlogs";
    }
    return std::filesystem::current_path() / "warcraftlogs_cache";
#endif
}

} // anonymous namespace

ResponseCache::ResponseCache()
    : m_cacheDir(getDefaultCacheDir())
{
}

ResponseCache::ResponseCache(std::filesystem::path cacheDir)
    : m_cacheDir(std::move(cacheDir))
{
}

void ResponseCache::setCacheDirectory(std::filesystem::path cacheDir) {
    m_cacheDir = std::move(cacheDir);
}

const std::filesystem::path& ResponseCache::getCacheDirectory() const noexcept {
    return m_cacheDir;
}

bool ResponseCache::hasCachedReport(const std::string& reportCode) const {
    return std::filesystem::exists(getReportMetaPath(reportCode));
}

bool ResponseCache::hasCachedFightEvents(const std::string& reportCode, int32_t fightId) const {
    auto fightDir = getFightDir(reportCode, fightId);
    if (!std::filesystem::exists(fightDir)) {
        return false;
    }

    // A fight counts as cached once page 1 exists.
    return std::filesystem::exists(getFightPagePath(reportCode, fightId, 1));
}

bool ResponseCache::saveReport(const Report& report) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();

    doc.AddMember("report_code",
        rapidjson::Value(report.code.c_str(), allocator),
        allocator);

    // Unix epoch ms.
    doc.AddMember("startTime", report.startTime, allocator);

    rapidjson::Value fightsArray(rapidjson::kArrayType);
    for (const auto& fight : report.fights) {
        rapidjson::Value fightObj(rapidjson::kObjectType);
        fightObj.AddMember("id", fight.id, allocator);
        fightObj.AddMember("name", rapidjson::Value(fight.name.c_str(), allocator), allocator);
        fightObj.AddMember("startTime", fight.startTime, allocator);
        fightObj.AddMember("endTime", fight.endTime, allocator);
        fightObj.AddMember("encounterID", fight.encounterID, allocator);
        fightObj.AddMember("difficulty", fight.difficulty, allocator);
        fightObj.AddMember("keystoneLevel", fight.keystoneLevel, allocator);
        fightObj.AddMember("kill", fight.kill, allocator);
        fightsArray.PushBack(fightObj, allocator);
    }
    doc.AddMember("fights", fightsArray, allocator);

    rapidjson::Value actorsArray(rapidjson::kArrayType);
    for (const auto& actor : report.actors) {
        rapidjson::Value actorObj(rapidjson::kObjectType);
        actorObj.AddMember("id", actor.id, allocator);
        actorObj.AddMember("gameID", actor.gameID, allocator);
        actorObj.AddMember("name", rapidjson::Value(actor.name.c_str(), allocator), allocator);
        actorObj.AddMember("type",
            rapidjson::Value(std::string(actorTypeToString(actor.type)).c_str(), allocator),
            allocator);
        actorObj.AddMember("subType", rapidjson::Value(actor.subType.c_str(), allocator), allocator);
        actorObj.AddMember("petOwner", actor.petOwner, allocator);
        actorObj.AddMember("server", rapidjson::Value(actor.server.c_str(), allocator), allocator);
        actorObj.AddMember("icon", rapidjson::Value(actor.icon.c_str(), allocator), allocator);
        actorsArray.PushBack(actorObj, allocator);
    }
    doc.AddMember("actors", actorsArray, allocator);

    rapidjson::Value actorMapObj(rapidjson::kObjectType);
    for (const auto& [id, name] : report.actorMap) {
        actorMapObj.AddMember(
            rapidjson::Value(std::to_string(id).c_str(), allocator),
            rapidjson::Value(name.c_str(), allocator),
            allocator);
    }
    doc.AddMember("actor_map", actorMapObj, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return writeFile(getReportMetaPath(report.code), buffer.GetString());
}

bool ResponseCache::saveRawMetadata(const std::string& reportCode, const std::string& metadataJson) {
    return writeFile(getReportMetaPath(reportCode), metadataJson);
}

std::optional<std::string> ResponseCache::loadRawMetadata(const std::string& reportCode) const {
    return readFile(getReportMetaPath(reportCode));
}

std::optional<Report> ResponseCache::loadReport(const std::string& reportCode) const {
    auto content = readFile(getReportMetaPath(reportCode));
    if (!content) {
        return std::nullopt;
    }

    rapidjson::Document doc;
    doc.Parse(content->c_str());

    if (doc.HasParseError() || !doc.IsObject()) {
        return std::nullopt;
    }

    Report report;
    report.code = reportCode;

    // Unix epoch ms; 0 in reports cached by older builds.
    if (doc.HasMember("startTime")) {
        if (doc["startTime"].IsInt64()) {
            report.startTime = doc["startTime"].GetInt64();
        } else if (doc["startTime"].IsDouble()) {
            report.startTime = static_cast<int64_t>(doc["startTime"].GetDouble());
        } else if (doc["startTime"].IsInt()) {
            report.startTime = doc["startTime"].GetInt();
        } else if (doc["startTime"].IsUint64()) {
            report.startTime = static_cast<int64_t>(doc["startTime"].GetUint64());
        }
    }

    if (doc.HasMember("fights") && doc["fights"].IsArray()) {
        for (const auto& f : doc["fights"].GetArray()) {
            if (!f.IsObject()) continue;
            Fight fight;
            fight.id = json::getInt(f, "id");
            fight.name = json::getString(f, "name");
            fight.startTime = json::getInt64(f, "startTime");
            fight.endTime = json::getInt64(f, "endTime");
            fight.encounterID = json::getInt(f, "encounterID");
            fight.difficulty = json::getInt(f, "difficulty");
            fight.keystoneLevel = json::getInt(f, "keystoneLevel");
            fight.kill = json::getBool(f, "kill");
            report.fights.push_back(fight);
        }
    }

    if (doc.HasMember("actors") && doc["actors"].IsArray()) {
        for (const auto& a : doc["actors"].GetArray()) {
            if (!a.IsObject()) continue;
            Actor actor;
            actor.id = json::getInt(a, "id");
            actor.gameID = json::getInt64(a, "gameID");
            actor.name = json::getString(a, "name");
            actor.type = parseActorType(json::getString(a, "type"));
            actor.subType = json::getString(a, "subType");
            actor.petOwner = json::getInt(a, "petOwner", actor.petOwner);
            actor.server = json::getString(a, "server");
            actor.icon = json::getString(a, "icon");
            report.actors.push_back(actor);
        }
    }

    if (doc.HasMember("actor_map") && doc["actor_map"].IsObject()) {
        for (const auto& m : doc["actor_map"].GetObject()) {
            // A damaged entry is skipped rather than failing the whole report.
            auto id = json::parseIdKey(m.name);
            if (!id || !m.value.IsString()) {
                continue;
            }
            report.actorMap[*id] = std::string(m.value.GetString(), m.value.GetStringLength());
        }
    }

    return report;
}

bool ResponseCache::saveFightEventsPage(
    const std::string& reportCode,
    int32_t fightId,
    int32_t pageNumber,
    const std::string& eventsJson
) {
    return writeFile(getFightPagePath(reportCode, fightId, pageNumber), eventsJson);
}

std::vector<std::string> ResponseCache::loadFightEventsPages(
    const std::string& reportCode,
    int32_t fightId
) const {
    std::vector<std::string> pages;

    int32_t pageNum = 1;
    while (true) {
        auto content = readFile(getFightPagePath(reportCode, fightId, pageNum));
        if (!content) {
            break;
        }
        pages.push_back(*content);
        ++pageNum;
    }

    return pages;
}

int32_t ResponseCache::getCachedPageCount(
    const std::string& reportCode,
    int32_t fightId
) const {
    auto fightDir = getFightDir(reportCode, fightId);
    if (!std::filesystem::exists(fightDir)) {
        return 0;
    }

    int32_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(fightDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            ++count;
        }
    }
    return count;
}

bool ResponseCache::clearFightCache(const std::string& reportCode, int32_t fightId) {
    auto fightDir = getFightDir(reportCode, fightId);
    if (!std::filesystem::exists(fightDir)) {
        return true;
    }

    std::error_code ec;
    std::filesystem::remove_all(fightDir, ec);
    return !ec;
}

bool ResponseCache::clearReportCache(const std::string& reportCode) {
    auto reportDir = getReportDir(reportCode);
    if (!std::filesystem::exists(reportDir)) {
        return true;
    }

    std::error_code ec;
    std::filesystem::remove_all(reportDir, ec);
    return !ec;
}

bool ResponseCache::clearAllCache() {
    if (!std::filesystem::exists(m_cacheDir)) {
        return true;
    }

    std::error_code ec;
    std::filesystem::remove_all(m_cacheDir, ec);
    return !ec;
}

std::uintmax_t ResponseCache::getCacheSize() const {
    if (!std::filesystem::exists(m_cacheDir)) {
        return 0;
    }

    std::uintmax_t totalSize = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(m_cacheDir)) {
        if (entry.is_regular_file()) {
            totalSize += entry.file_size();
        }
    }
    return totalSize;
}

std::vector<std::string> ResponseCache::getCachedReportCodes() const {
    std::vector<std::string> codes;

    if (!std::filesystem::exists(m_cacheDir)) {
        return codes;
    }

    for (const auto& entry : std::filesystem::directory_iterator(m_cacheDir)) {
        if (entry.is_directory()) {
            codes.push_back(entry.path().filename().string());
        }
    }

    return codes;
}

std::filesystem::path ResponseCache::getReportDir(const std::string& reportCode) const {
    return m_cacheDir / reportCode;
}

std::filesystem::path ResponseCache::getFightDir(
    const std::string& reportCode,
    int32_t fightId
) const {
    return getReportDir(reportCode) / ("fight" + std::to_string(fightId));
}

std::filesystem::path ResponseCache::getReportMetaPath(const std::string& reportCode) const {
    return getReportDir(reportCode) / "report_meta.json";
}

std::filesystem::path ResponseCache::getFightPagePath(
    const std::string& reportCode,
    int32_t fightId,
    int32_t pageNumber
) const {
    return getFightDir(reportCode, fightId) / ("page" + std::to_string(pageNumber) + ".json");
}

bool ResponseCache::ensureDirectoryExists(const std::filesystem::path& path) const {
    if (std::filesystem::exists(path)) {
        return true;
    }

    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
}

bool ResponseCache::writeFile(const std::filesystem::path& path, const std::string& content) const {
    if (!ensureDirectoryExists(path.parent_path())) {
        return false;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    file.write(content.c_str(), static_cast<std::streamsize>(content.size()));
    return file.good();
}

std::optional<std::string> ResponseCache::readFile(const std::filesystem::path& path) const {
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace warcraftlogs
