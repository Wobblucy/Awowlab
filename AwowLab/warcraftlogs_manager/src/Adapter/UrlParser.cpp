#include "Adapter/UrlParser.h"

#include <algorithm>
#include <charconv>
#include <cctype>

namespace warcraftlogs {

namespace {

// Report codes are 16 alphanumeric characters.
constexpr size_t REPORT_CODE_LENGTH = 16;

constexpr const char* WARCRAFTLOGS_HOST = "warcraftlogs.com";
constexpr const char* REPORTS_PATH = "/reports/";

} // anonymous namespace

ParsedUrl UrlParser::parse(const std::string& input) {
    std::string trimmed = trim(input);

    if (trimmed.empty()) {
        throw UrlParseException("Input is empty");
    }

    ParsedUrl result;

    // A bare report code, with no URL around it.
    if (isValidReportCode(trimmed)) {
        result.reportCode = trimmed;
        return result;
    }

    size_t reportsPos = trimmed.find(REPORTS_PATH);
    if (reportsPos == std::string::npos) {
        // Fall back to a case-insensitive search.
        std::string lower = trimmed;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        reportsPos = lower.find(REPORTS_PATH);
        if (reportsPos == std::string::npos) {
            throw UrlParseException("Invalid WarcraftLogs URL: missing /reports/ path");
        }
    }

    size_t codeStart = reportsPos + std::string(REPORTS_PATH).length();

    // The code ends at the next /, ?, # or the end of the string.
    size_t codeEnd = trimmed.find_first_of("/?#", codeStart);
    if (codeEnd == std::string::npos) {
        codeEnd = trimmed.length();
    }

    std::string reportCode = trimmed.substr(codeStart, codeEnd - codeStart);

    if (!isValidReportCode(reportCode)) {
        throw UrlParseException("Invalid report code: " + reportCode);
    }

    result.reportCode = reportCode;

    // ?fight=X
    size_t queryPos = trimmed.find('?', codeEnd);
    if (queryPos != std::string::npos) {
        size_t queryEnd = trimmed.find('#', queryPos);
        std::string queryString = (queryEnd != std::string::npos)
            ? trimmed.substr(queryPos + 1, queryEnd - queryPos - 1)
            : trimmed.substr(queryPos + 1);
        parseFightParameter(queryString, result);
    }

    // #fight=X
    size_t hashPos = trimmed.find('#');
    if (hashPos != std::string::npos) {
        std::string hashFragment = trimmed.substr(hashPos + 1);
        parseFightParameter(hashFragment, result);
    }

    return result;
}

bool UrlParser::isValidReportCode(const std::string& code) noexcept {
    if (code.length() != REPORT_CODE_LENGTH) {
        return false;
    }

    return std::all_of(code.begin(), code.end(), [](unsigned char c) {
        return std::isalnum(c);
    });
}

std::string UrlParser::extractReportCode(const std::string& input) {
    return parse(input).reportCode;
}

std::string UrlParser::buildUrl(
    const std::string& reportCode,
    std::optional<int32_t> fightId
) {
    std::string url = "https://www.warcraftlogs.com/reports/";
    url += reportCode;

    if (fightId.has_value()) {
        url += "?fight=" + std::to_string(fightId.value());
    }

    return url;
}

void UrlParser::parseFightParameter(const std::string& queryOrHash, ParsedUrl& result) {
    // fight=3 or fight=last, possibly among other parameters.
    const std::string fightKey = "fight=";

    size_t fightPos = queryOrHash.find(fightKey);
    if (fightPos == std::string::npos) {
        return;
    }

    size_t valueStart = fightPos + fightKey.length();

    size_t valueEnd = queryOrHash.find('&', valueStart);
    if (valueEnd == std::string::npos) {
        valueEnd = queryOrHash.length();
    }

    std::string fightValue = queryOrHash.substr(valueStart, valueEnd - valueStart);

    if (fightValue == "last") {
        result.fightLast = true;
        return;
    }

    int32_t fightId = 0;
    auto [ptr, ec] = std::from_chars(
        fightValue.data(),
        fightValue.data() + fightValue.size(),
        fightId
    );

    if (ec == std::errc{} && ptr == fightValue.data() + fightValue.size()) {
        result.fightId = fightId;
    }
    // An unparseable fight value is ignored.
}

std::string UrlParser::trim(const std::string& str) {
    size_t start = 0;
    while (start < str.length() && std::isspace(static_cast<unsigned char>(str[start]))) {
        ++start;
    }

    if (start == str.length()) {
        return "";
    }

    size_t end = str.length() - 1;
    while (end > start && std::isspace(static_cast<unsigned char>(str[end]))) {
        --end;
    }

    return str.substr(start, end - start + 1);
}

} // namespace warcraftlogs
