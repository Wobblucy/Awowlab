#pragma once

#include <string>
#include <optional>
#include <stdexcept>

namespace warcraftlogs {

struct ParsedUrl {
    std::string reportCode;
    std::optional<int32_t> fightId;
    bool fightLast = false;              // "fight=last"

    [[nodiscard]] bool hasFightSpecified() const noexcept {
        return fightId.has_value() || fightLast;
    }
};

class UrlParseException : public std::runtime_error {
public:
    explicit UrlParseException(const std::string& message)
        : std::runtime_error(message) {}
};

// Accepts any of:
//   https://www.warcraftlogs.com/reports/j1N7Rf4d23FAmtMk?fight=3
//   https://www.warcraftlogs.com/reports/j1N7Rf4d23FAmtMk#fight=last
//   https://classic.warcraftlogs.com/reports/...
//   j1N7Rf4d23FAmtMk
class UrlParser {
public:
    // Throws UrlParseException if the input is neither a report URL nor a code.
    [[nodiscard]] static ParsedUrl parse(const std::string& input);

    // Report codes are 16 alphanumeric characters.
    [[nodiscard]] static bool isValidReportCode(const std::string& code) noexcept;

    // Throws UrlParseException if the input is neither a report URL nor a code.
    [[nodiscard]] static std::string extractReportCode(const std::string& input);

    [[nodiscard]] static std::string buildUrl(
        const std::string& reportCode,
        std::optional<int32_t> fightId = std::nullopt
    );

private:
    // queryOrHash is the text after ? or #.
    static void parseFightParameter(const std::string& queryOrHash, ParsedUrl& result);

    [[nodiscard]] static std::string trim(const std::string& str);
};

} // namespace warcraftlogs
