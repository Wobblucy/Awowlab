#pragma once

#include "../Models/Report.h"
#include "../Models/Event.h"

#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace warcraftlogs {

// On-disk cache of WarcraftLogs API responses, one JSON file per report
// metadata and per page of fight events.
class ResponseCache {
public:
    // %LOCALAPPDATA%\AwowLab\warcraftlogs_cache on Windows.
    ResponseCache();

    explicit ResponseCache(std::filesystem::path cacheDir);

    ~ResponseCache() = default;

    ResponseCache(const ResponseCache&) = delete;
    ResponseCache& operator=(const ResponseCache&) = delete;

    ResponseCache(ResponseCache&&) noexcept = default;
    ResponseCache& operator=(ResponseCache&&) noexcept = default;

    void setCacheDirectory(std::filesystem::path cacheDir);

    [[nodiscard]] const std::filesystem::path& getCacheDirectory() const noexcept;

    [[nodiscard]] bool hasCachedReport(const std::string& reportCode) const;

    [[nodiscard]] bool hasCachedFightEvents(const std::string& reportCode, int32_t fightId) const;

    bool saveReport(const Report& report);

    // nullopt if missing or unreadable.
    [[nodiscard]] std::optional<Report> loadReport(const std::string& reportCode) const;

    bool saveRawMetadata(const std::string& reportCode, const std::string& metadataJson);

    // nullopt if not cached.
    [[nodiscard]] std::optional<std::string> loadRawMetadata(const std::string& reportCode) const;

    // One page; pages are numbered from 1.
    bool saveFightEventsPage(
        const std::string& reportCode,
        int32_t fightId,
        int32_t pageNumber,
        const std::string& eventsJson
    );

    [[nodiscard]] std::vector<std::string> loadFightEventsPages(
        const std::string& reportCode,
        int32_t fightId
    ) const;

    [[nodiscard]] int32_t getCachedPageCount(
        const std::string& reportCode,
        int32_t fightId
    ) const;

    bool clearFightCache(const std::string& reportCode, int32_t fightId);

    bool clearReportCache(const std::string& reportCode);

    bool clearAllCache();

    // In bytes.
    [[nodiscard]] std::uintmax_t getCacheSize() const;

    [[nodiscard]] std::vector<std::string> getCachedReportCodes() const;

private:
    [[nodiscard]] std::filesystem::path getReportDir(const std::string& reportCode) const;

    [[nodiscard]] std::filesystem::path getFightDir(
        const std::string& reportCode,
        int32_t fightId
    ) const;

    [[nodiscard]] std::filesystem::path getReportMetaPath(const std::string& reportCode) const;

    [[nodiscard]] std::filesystem::path getFightPagePath(
        const std::string& reportCode,
        int32_t fightId,
        int32_t pageNumber
    ) const;

    bool ensureDirectoryExists(const std::filesystem::path& path) const;

    bool writeFile(const std::filesystem::path& path, const std::string& content) const;

    [[nodiscard]] std::optional<std::string> readFile(const std::filesystem::path& path) const;

    std::filesystem::path m_cacheDir;
};

} // namespace warcraftlogs
