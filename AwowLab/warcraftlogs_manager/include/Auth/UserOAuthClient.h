#pragma once

#include <HttpClient.h>
#include <LocalHttpServer.h>
#include <string>
#include <chrono>
#include <optional>
#include <mutex>
#include <functional>

namespace warcraftlogs {

using AuthProgressCallback = std::function<void(const std::string& status)>;

enum class AuthState {
    NotAuthenticated,
    AwaitingUser,        // browser opened, waiting for the user to approve
    Authenticated,
    Error
};

struct AuthResult {
    bool success = false;
    std::string errorMessage;
    std::string accessToken;
    int32_t expiresIn = 0;
    std::chrono::system_clock::time_point expiresAt;

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

// Signs the user in to WarcraftLogs with OAuth2 PKCE, so no client secret is
// needed. A code verifier and challenge are generated, the browser opens the
// WarcraftLogs authorization page, a local HTTP server catches the redirect
// with the authorization code, and the code plus verifier are exchanged for
// an access token.
//
// The token works on /api/v2/user, which can read private reports the user
// is allowed to see.
class UserOAuthClient {
public:
    // Client id and redirect URI from Credentials.h.
    UserOAuthClient();

    // redirectUri must match one registered for the client.
    UserOAuthClient(std::string clientId, std::string redirectUri);

    // Holds a mutex, so neither copyable nor movable.
    UserOAuthClient(const UserOAuthClient&) = delete;
    UserOAuthClient& operator=(const UserOAuthClient&) = delete;
    UserOAuthClient(UserOAuthClient&&) = delete;
    UserOAuthClient& operator=(UserOAuthClient&&) = delete;

    ~UserOAuthClient() = default;

    // Opens the default browser on the authorization page and blocks until
    // the user finishes or timeoutSeconds pass.
    AuthResult authenticate(
        AuthProgressCallback progressCallback = nullptr,
        int timeoutSeconds = 120
    );

    // Empty when not signed in.
    [[nodiscard]] std::string getToken() const;

    // True while a token is held. Expiry isn't checked; a rejected request
    // triggers sign-in again.
    [[nodiscard]] bool isAuthenticated() const noexcept;

    [[nodiscard]] AuthState getState() const noexcept;

    [[nodiscard]] std::optional<std::chrono::system_clock::time_point> getTokenExpiry() const noexcept;

    void logout() noexcept;

    // For a token restored from saved settings. Without an expiry it is
    // taken to last one hour from now.
    void setToken(const std::string& token,
                  std::optional<std::chrono::system_clock::time_point> expiry = std::nullopt) noexcept;

    // For opening the browser yourself. Generates new PKCE codes, so call it
    // once per attempt.
    [[nodiscard]] std::string getAuthorizationUrl();

    // Finishes a sign-in whose callback was handled outside authenticate().
    AuthResult exchangeCode(const std::string& code);

private:
    // Fresh verifier, challenge and state for one attempt. False, with
    // m_lastError set, if no secure random data was available; the codes are
    // then empty and the sign-in must stop.
    bool generatePkceCodes();

    // Moves to AuthState::Error and returns a failed result carrying message.
    AuthResult fail(const std::string& message);

    // Moves to AuthState::Authenticated and returns the token just obtained.
    AuthResult succeed();

    // 43-128 characters from [A-Za-z0-9-._~] (RFC 7636). Empty if no secure
    // random data was available.
    static std::string generateCodeVerifier();

    // BASE64URL(SHA256(code_verifier))
    static std::string generateCodeChallenge(const std::string& codeVerifier);

    // Random state for CSRF protection. Empty if no secure random data was
    // available.
    static std::string generateState();

    static bool openBrowser(const std::string& url);

    std::string buildAuthorizationUrl() const;

    bool requestToken(const std::string& code);

    bool parseTokenResponse(const std::string& responseBody);

    std::string m_clientId;
    std::string m_redirectUri;
    int m_redirectPort;

    // Regenerated for each attempt.
    std::string m_codeVerifier;
    std::string m_codeChallenge;
    std::string m_state;

    std::string m_accessToken;
    std::chrono::system_clock::time_point m_tokenExpiry;
    AuthState m_authState = AuthState::NotAuthenticated;
    std::string m_lastError;

    mutable std::mutex m_mutex;
    http::HttpClient m_httpClient;
};

} // namespace warcraftlogs
