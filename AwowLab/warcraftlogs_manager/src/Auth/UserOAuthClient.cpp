#include "Auth/UserOAuthClient.h"
#include "Auth/Credentials.h"
#include "Core/ErrorLogger.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <openssl/sha.h>
#include <openssl/rand.h>

#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <iomanip>
#include <iterator>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#endif

namespace warcraftlogs {

namespace {

// Base64URL (+ becomes -, / becomes _) without the '=' padding, as RFC 7636
// requires for PKCE.
std::string base64UrlEncode(const unsigned char* data, size_t length) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    std::string result;
    result.reserve(((length + 2) / 3) * 4);

    int val = 0;
    int valb = -6;

    for (size_t i = 0; i < length; ++i) {
        val = (val << 8) + data[i];
        valb += 8;
        while (valb >= 0) {
            result.push_back(chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }

    if (valb > -6) {
        result.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }

    return result;
}

std::string urlEncode(const std::string& str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : str) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
        }
    }

    return escaped.str();
}

// The characters RFC 7636 allows in a code verifier.
constexpr const char* CODE_VERIFIER_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
constexpr size_t CODE_VERIFIER_CHAR_COUNT = 66;
// The longest verifier RFC 7636 allows.
constexpr size_t CODE_VERIFIER_LENGTH = 128;
constexpr size_t STATE_LENGTH = 32;

constexpr int DEFAULT_REDIRECT_PORT = 8080;
constexpr int TOKEN_REQUEST_TIMEOUT_MS = 30000;
// Used when the token response leaves out expires_in.
constexpr int DEFAULT_TOKEN_LIFETIME_SECONDS = 3600;

// length characters drawn from CODE_VERIFIER_CHARS by OpenSSL's secure
// random generator. A guessable verifier or state defeats PKCE, so there is
// no weaker fallback: an empty result fails the sign-in instead.
//
// 256 isn't a multiple of 66, so bytes at or above the largest multiple (198)
// are thrown away; taking them modulo 66 would make the first 58 characters
// come up more often than the rest.
std::string secureRandomString(size_t length) {
    constexpr unsigned UNBIASED_LIMIT = 256 - (256 % CODE_VERIFIER_CHAR_COUNT);

    std::string text;
    text.reserve(length);
    std::vector<unsigned char> randomBytes(length);
    while (text.size() < length) {
        if (RAND_bytes(randomBytes.data(), static_cast<int>(randomBytes.size())) != 1) {
            return {};
        }
        for (unsigned char byte : randomBytes) {
            if (byte >= UNBIASED_LIMIT) {
                continue;
            }
            text.push_back(CODE_VERIFIER_CHARS[byte % CODE_VERIFIER_CHAR_COUNT]);
            if (text.size() == length) {
                break;
            }
        }
    }
    return text;
}

// "8080, 8765, 8766, or 8767"
std::string describeRedirectPorts() {
    std::string text;
    const size_t count = std::size(credentials::REDIRECT_PORTS);
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) text += ", ";
        if (i > 0 && i + 1 == count) text += "or ";
        text += std::to_string(credentials::REDIRECT_PORTS[i]);
    }
    return text;
}

// A user-facing explanation for a callback server that stopped without a code.
std::string describeServerFailure(const std::string& serverError, int port) {
    if (serverError.find("bind") != std::string::npos ||
        serverError.find("address") != std::string::npos ||
        serverError.find("Address already in use") != std::string::npos) {
        return "Port " + std::to_string(port) +
            " is already in use. Please close any other applications using this port and try again.";
    }
    if (serverError.find("timeout") != std::string::npos) {
        return "Authorization timed out. Check awowlab_errors.log for troubleshooting steps.";
    }
    if (serverError.find("network") != std::string::npos) {
        return "Network error during authorization. Check awowlab_errors.log for details.";
    }
    return "Authorization failed. Check awowlab_errors.log for details.";
}

} // anonymous namespace

UserOAuthClient::UserOAuthClient()
    : m_clientId(credentials::CLIENT_ID)
    , m_redirectUri(credentials::getRedirectUri(credentials::REDIRECT_PORTS[0]))
    , m_redirectPort(credentials::REDIRECT_PORTS[0])
    , m_tokenExpiry(std::chrono::system_clock::time_point::min())
    , m_httpClient()
{
    m_httpClient.setTimeout(TOKEN_REQUEST_TIMEOUT_MS);
}

UserOAuthClient::UserOAuthClient(std::string clientId, std::string redirectUri)
    : m_clientId(std::move(clientId))
    , m_redirectUri(std::move(redirectUri))
    , m_redirectPort(DEFAULT_REDIRECT_PORT)
    , m_tokenExpiry(std::chrono::system_clock::time_point::min())
    , m_httpClient()
{
    m_httpClient.setTimeout(TOKEN_REQUEST_TIMEOUT_MS);

    // A port in the redirect URI overrides the default.
    constexpr std::string_view hostPrefix = "localhost:";
    size_t portPos = m_redirectUri.find(hostPrefix);
    if (portPos != std::string::npos) {
        portPos += hostPrefix.size();
        size_t endPos = m_redirectUri.find('/', portPos);
        if (endPos == std::string::npos) {
            endPos = m_redirectUri.length();
        }
        try {
            m_redirectPort = std::stoi(m_redirectUri.substr(portPos, endPos - portPos));
        } catch (...) {
            // Keep the default port.
        }
    }
}

AuthResult UserOAuthClient::fail(const std::string& message) {
    m_lastError = message;
    m_authState = AuthState::Error;

    AuthResult result;
    result.errorMessage = message;
    return result;
}

AuthResult UserOAuthClient::succeed() {
    m_authState = AuthState::Authenticated;

    AuthResult result;
    result.success = true;
    result.accessToken = m_accessToken;
    result.expiresAt = m_tokenExpiry;
    result.expiresIn = static_cast<int32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            m_tokenExpiry - std::chrono::system_clock::now()
        ).count()
    );
    return result;
}

AuthResult UserOAuthClient::authenticate(
    AuthProgressCallback progressCallback,
    int timeoutSeconds
) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto reportProgress = [&](const char* status) {
        if (progressCallback) {
            progressCallback(status);
        }
    };

    m_authState = AuthState::AwaitingUser;

    if (!generatePkceCodes()) {
        return fail(m_lastError);
    }

    reportProgress("Generated PKCE codes");

    // Pick a free port before the browser opens, so the redirect URI names one.
    http::LocalHttpServer server;
    int boundPort = 0;

    AWOW_LOG_INFO("OAUTH", "Finding available port for OAuth callback...");
    for (int port : credentials::REDIRECT_PORTS) {
        if (server.tryBind(port)) {
            boundPort = port;
            AWOW_LOG_INFO("OAUTH", "Port " + std::to_string(port) + " is available");
            break;
        }
        AWOW_LOG_INFO("OAUTH", "Port " + std::to_string(port) + " unavailable, trying next...");
    }

    if (boundPort == 0) {
        AWOW_LOG_INFO("OAUTH", "No available port found for OAuth callback");
        return fail("Could not find an available port for OAuth callback. "
                    "Please close applications using ports " + describeRedirectPorts() + ".");
    }

    m_redirectPort = boundPort;
    m_redirectUri = credentials::getRedirectUri(boundPort);

    std::string authUrl = buildAuthorizationUrl();

    AWOW_LOG_INFO("OAUTH", "Authorization URL built, redirect URI: " + m_redirectUri);
    AWOW_LOG_INFO("OAUTH", "Using port " + std::to_string(m_redirectPort) + " for OAuth callback");

    reportProgress("Opening browser for authorization...");

    if (!openBrowser(authUrl)) {
        return fail("Failed to open browser for WarcraftLogs authorization. "
                    "Please ensure you have a default browser configured.");
    }

    reportProgress("Waiting for user authorization...");

    std::string capturedCode;
    std::string capturedState;
    std::string callbackError;
    bool callbackReceived = server.waitForCallback(
        [&](const http::OAuthCallback& callback) {
            capturedCode = callback.code;
            capturedState = callback.state;
            callbackError = callback.error;
        },
        m_redirectPort,
        timeoutSeconds
    );

    if (!callbackReceived) {
        std::string serverError = server.getLastError();

        AWOW_LOG_INFO("OAUTH", "Callback not received. Server error: " +
            (serverError.empty() ? "(none)" : serverError));

        if (!callbackError.empty()) {
            AWOW_LOG_INFO("OAUTH", "User denied authorization: " + callbackError);
            return fail("Authorization denied: " + callbackError);
        }
        if (!serverError.empty()) {
            // Port in use, network failure and the like.
            return fail(describeServerFailure(serverError, m_redirectPort));
        }
        return fail("Authorization timeout or cancelled. Check awowlab_errors.log if the browser showed a spinning page.");
    }

    // A state mismatch means the callback didn't come from this sign-in (CSRF).
    if (capturedState != m_state) {
        return fail("State mismatch - possible CSRF attack");
    }

    reportProgress("Exchanging code for token...");

    if (!requestToken(capturedCode)) {
        return fail(m_lastError);
    }

    reportProgress("Authentication successful!");

    return succeed();
}

std::string UserOAuthClient::getToken() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Returned even if expired: a rejected request triggers sign-in again.
    return m_accessToken;
}

bool UserOAuthClient::isAuthenticated() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Expiry isn't checked here; a rejected request triggers sign-in again.
    return !m_accessToken.empty() && m_authState == AuthState::Authenticated;
}

AuthState UserOAuthClient::getState() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_authState;
}

std::optional<std::chrono::system_clock::time_point> UserOAuthClient::getTokenExpiry() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_accessToken.empty()) {
        return std::nullopt;
    }
    return m_tokenExpiry;
}

void UserOAuthClient::logout() noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_accessToken.clear();
    m_codeVerifier.clear();
    m_codeChallenge.clear();
    m_state.clear();
    m_tokenExpiry = std::chrono::system_clock::time_point::min();
    m_authState = AuthState::NotAuthenticated;
    m_lastError.clear();
}

void UserOAuthClient::setToken(const std::string& token,
                               std::optional<std::chrono::system_clock::time_point> expiry) noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (token.empty()) {
        m_accessToken.clear();
        m_tokenExpiry = std::chrono::system_clock::time_point::min();
        m_authState = AuthState::NotAuthenticated;
        return;
    }

    m_accessToken = token;
    m_tokenExpiry = expiry.value_or(std::chrono::system_clock::now() + std::chrono::hours(1));
    m_authState = AuthState::Authenticated;
    m_lastError.clear();
}

std::string UserOAuthClient::getAuthorizationUrl() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!generatePkceCodes()) {
        m_authState = AuthState::Error;
        return {};
    }

    return buildAuthorizationUrl();
}

AuthResult UserOAuthClient::exchangeCode(const std::string& code) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_codeVerifier.empty()) {
        return fail("No code verifier available - call getAuthorizationUrl first");
    }

    if (!requestToken(code)) {
        return fail(m_lastError);
    }

    return succeed();
}

bool UserOAuthClient::generatePkceCodes() {
    m_codeVerifier = generateCodeVerifier();
    m_state = generateState();

    if (m_codeVerifier.empty() || m_state.empty()) {
        m_codeVerifier.clear();
        m_codeChallenge.clear();
        m_state.clear();
        m_lastError = "Could not generate secure random data for sign-in";
        AWOW_LOG_ERROR("OAUTH", m_lastError);
        return false;
    }

    m_codeChallenge = generateCodeChallenge(m_codeVerifier);
    return true;
}

std::string UserOAuthClient::generateCodeVerifier() {
    return secureRandomString(CODE_VERIFIER_LENGTH);
}

std::string UserOAuthClient::generateCodeChallenge(const std::string& codeVerifier) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(
        reinterpret_cast<const unsigned char*>(codeVerifier.data()),
        codeVerifier.size(),
        hash
    );

    // Base64URL without padding.
    return base64UrlEncode(hash, SHA256_DIGEST_LENGTH);
}

std::string UserOAuthClient::generateState() {
    return secureRandomString(STATE_LENGTH);
}

bool UserOAuthClient::openBrowser(const std::string& url) {
#ifdef _WIN32
    HINSTANCE result = ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
#else
    std::string cmd = "xdg-open \"" + url + "\" 2>/dev/null || open \"" + url + "\"";
    return std::system(cmd.c_str()) == 0;
#endif
}

std::string UserOAuthClient::buildAuthorizationUrl() const {
    std::ostringstream url;
    url << credentials::OAUTH_AUTHORIZE_URL;
    url << "?client_id=" << urlEncode(m_clientId);
    url << "&response_type=code";
    url << "&redirect_uri=" << urlEncode(m_redirectUri);
    url << "&state=" << urlEncode(m_state);
    url << "&code_challenge=" << urlEncode(m_codeChallenge);
    url << "&code_challenge_method=S256";
    return url.str();
}

bool UserOAuthClient::requestToken(const std::string& code) {
    try {
        // PKCE sends no client_secret.
        std::map<std::string, std::string> formData = {
            {"client_id", m_clientId},
            {"code_verifier", m_codeVerifier},
            {"redirect_uri", m_redirectUri},
            {"grant_type", "authorization_code"},
            {"code", code}
        };

        auto response = m_httpClient.postForm(
            credentials::API_HOST,
            "/oauth/token",
            formData
        );

        if (!response.isSuccess()) {
            std::string errorDetail = "Token exchange failed (HTTP " + std::to_string(response.statusCode) + ")";

            if (response.statusCode == 401) {
                errorDetail += ": Invalid authorization code. This can happen if the code expired or was already used.";
            } else if (response.statusCode == 400) {
                errorDetail += ": Bad request - the authorization parameters may be invalid.";
            } else {
                errorDetail += ": " + response.body;
            }

            m_lastError = errorDetail;
            return false;
        }

        return parseTokenResponse(response.body);

    } catch (const http::HttpException& e) {
        m_lastError = std::string("HTTP error: ") + e.what();
        return false;
    } catch (const std::exception& e) {
        m_lastError = std::string("Error: ") + e.what();
        return false;
    }
}

bool UserOAuthClient::parseTokenResponse(const std::string& responseBody) {
    rapidjson::Document doc;
    doc.Parse(responseBody.c_str());

    if (doc.HasParseError()) {
        m_lastError = "Failed to parse token response: " +
            std::string(rapidjson::GetParseError_En(doc.GetParseError()));
        return false;
    }

    if (doc.HasMember("error")) {
        m_lastError = "OAuth error: ";
        if (doc["error"].IsString()) {
            m_lastError += doc["error"].GetString();
        }
        if (doc.HasMember("error_description") && doc["error_description"].IsString()) {
            m_lastError += " - " + std::string(doc["error_description"].GetString());
        }
        return false;
    }

    if (!doc.HasMember("access_token") || !doc["access_token"].IsString()) {
        m_lastError = "Token response missing access_token field";
        return false;
    }

    m_accessToken = doc["access_token"].GetString();

    int expiresIn = DEFAULT_TOKEN_LIFETIME_SECONDS;
    if (doc.HasMember("expires_in") && doc["expires_in"].IsInt()) {
        expiresIn = doc["expires_in"].GetInt();
    }

    m_tokenExpiry = std::chrono::system_clock::now() + std::chrono::seconds(expiresIn);

    return true;
}

} // namespace warcraftlogs
