#pragma once

#include <HttpClient.h>
#include <string>
#include <chrono>
#include <optional>
#include <mutex>

namespace warcraftlogs {

// Client-credentials OAuth for the WarcraftLogs API, with the token cached
// until shortly before it expires.
class OAuthClient {
public:
    // Uses the built-in client id.
    OAuthClient();

    OAuthClient(std::string clientId, std::string clientSecret);

    // Holds a mutex, so neither copyable nor movable.
    OAuthClient(const OAuthClient&) = delete;
    OAuthClient& operator=(const OAuthClient&) = delete;
    OAuthClient(OAuthClient&&) = delete;
    OAuthClient& operator=(OAuthClient&&) = delete;

    ~OAuthClient() = default;

    // Requests a new token when the cached one has expired. Thread-safe.
    // Throws std::runtime_error if no token can be obtained.
    [[nodiscard]] std::string getToken();

    // Always requests a new token. Thread-safe.
    // Throws std::runtime_error if no token can be obtained.
    std::string refreshToken();

    [[nodiscard]] bool isTokenValid() const noexcept;

    [[nodiscard]] std::optional<std::chrono::system_clock::time_point> getTokenExpiry() const noexcept;

    void clearToken() noexcept;

private:
    // Throws std::runtime_error on failure.
    std::string requestToken();

    void parseTokenResponse(const std::string& responseBody);

    std::string m_clientId;
    std::string m_clientSecret;
    std::string m_accessToken;
    std::chrono::system_clock::time_point m_tokenExpiry;
    mutable std::mutex m_mutex;
    http::HttpClient m_httpClient;
};

} // namespace warcraftlogs
