#include "Auth/OAuthClient.h"
#include "Auth/Credentials.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <stdexcept>
#include <sstream>

namespace warcraftlogs {

namespace {

// Base64 for HTTP Basic auth.
std::string base64Encode(const std::string& input) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((input.size() + 2) / 3) * 4);

    int val = 0;
    int valb = -6;

    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result.push_back(chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }

    if (valb > -6) {
        result.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }

    while (result.size() % 4) {
        result.push_back('=');
    }

    return result;
}

} // anonymous namespace

OAuthClient::OAuthClient()
    : m_clientId(credentials::CLIENT_ID)
    , m_clientSecret(credentials::CLIENT_SECRET)
    , m_tokenExpiry(std::chrono::system_clock::time_point::min())
    , m_httpClient()
{
}

OAuthClient::OAuthClient(std::string clientId, std::string clientSecret)
    : m_clientId(std::move(clientId))
    , m_clientSecret(std::move(clientSecret))
    , m_tokenExpiry(std::chrono::system_clock::time_point::min())
    , m_httpClient()
{
}

std::string OAuthClient::getToken() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (isTokenValid()) {
        return m_accessToken;
    }

    return requestToken();
}

std::string OAuthClient::refreshToken() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return requestToken();
}

bool OAuthClient::isTokenValid() const noexcept {
    if (m_accessToken.empty()) {
        return false;
    }

    // Treat the token as expired a little early, so it can't lapse mid-request.
    constexpr auto expiryMargin = std::chrono::seconds(60);
    return (m_tokenExpiry - std::chrono::system_clock::now()) > expiryMargin;
}

std::optional<std::chrono::system_clock::time_point> OAuthClient::getTokenExpiry() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_accessToken.empty()) {
        return std::nullopt;
    }
    return m_tokenExpiry;
}

void OAuthClient::clearToken() noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_accessToken.clear();
    m_tokenExpiry = std::chrono::system_clock::time_point::min();
}

std::string OAuthClient::requestToken() {
    try {
        std::string basicAuth = base64Encode(m_clientId + ":" + m_clientSecret);

        std::map<std::string, std::string> formData = {
            {"grant_type", "client_credentials"}
        };

        auto response = m_httpClient.postForm(
            credentials::API_HOST,
            "/oauth/token",
            formData,
            basicAuth
        );

        if (!response.isSuccess()) {
            throw std::runtime_error(
                "OAuth token request failed with status " +
                std::to_string(response.statusCode) + ": " + response.body
            );
        }

        parseTokenResponse(response.body);

        return m_accessToken;

    } catch (const std::exception& e) {
        // Network failures (HttpException), HTTP errors and bad responses alike.
        throw std::runtime_error(std::string("Failed to acquire OAuth token: ") + e.what());
    }
}

void OAuthClient::parseTokenResponse(const std::string& responseBody) {
    rapidjson::Document doc;
    doc.Parse(responseBody.c_str());

    if (doc.HasParseError()) {
        throw std::runtime_error(
            std::string("Failed to parse token response: ") +
            rapidjson::GetParseError_En(doc.GetParseError())
        );
    }

    if (!doc.HasMember("access_token") || !doc["access_token"].IsString()) {
        throw std::runtime_error("Token response missing access_token field");
    }

    m_accessToken = doc["access_token"].GetString();

    int expiresIn = 3600;
    if (doc.HasMember("expires_in") && doc["expires_in"].IsInt()) {
        expiresIn = doc["expires_in"].GetInt();
    }

    m_tokenExpiry = std::chrono::system_clock::now() + std::chrono::seconds(expiresIn);
}

} // namespace warcraftlogs
