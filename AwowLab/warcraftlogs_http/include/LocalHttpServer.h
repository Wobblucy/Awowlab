#pragma once

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <optional>

namespace warcraftlogs::http {

// What the OAuth redirect brought back.
struct OAuthCallback {
    std::string code;
    std::string state;      // checked against the sent state to catch CSRF
    std::string error;
    std::string errorDescription;

    [[nodiscard]] bool isSuccess() const noexcept {
        return !code.empty() && error.empty();
    }

    [[nodiscard]] bool isError() const noexcept {
        return !error.empty();
    }
};

using OAuthCallbackHandler = std::function<void(const OAuthCallback& callback)>;

// Small HTTP server on localhost that receives the OAuth redirect. It listens
// on one port until a request for /callback arrives or the timeout passes.
// Other requests get a 404, and a connection that sends nothing is dropped
// after a few seconds.
class LocalHttpServer {
public:
    static constexpr int DEFAULT_PORT = 8080;
    static constexpr int DEFAULT_TIMEOUT_SECONDS = 120;

    LocalHttpServer();
    ~LocalHttpServer();

    LocalHttpServer(const LocalHttpServer&) = delete;
    LocalHttpServer& operator=(const LocalHttpServer&) = delete;

    // Blocks until the callback arrives or the timeout passes. Returns true
    // only for a successful authorization.
    bool waitForCallback(
        OAuthCallbackHandler callback,
        int port = DEFAULT_PORT,
        int timeoutSeconds = DEFAULT_TIMEOUT_SECONDS
    );

    // Runs the server on a background thread. Poll isComplete(), then read
    // getResult().
    bool startAsync(int port = DEFAULT_PORT, int timeoutSeconds = DEFAULT_TIMEOUT_SECONDS);

    [[nodiscard]] std::optional<OAuthCallback> getResult() const;

    // True once a callback arrived or the wait timed out.
    [[nodiscard]] bool isComplete() const noexcept;

    [[nodiscard]] bool isRunning() const noexcept;

    // The error and its description, for diagnosing a failed wait.
    [[nodiscard]] std::string getLastError() const noexcept;

    void stop();

    // e.g. "http://localhost:8765/callback"
    [[nodiscard]] static std::string getRedirectUri(int port = DEFAULT_PORT);

    // True if the port can be bound on loopback right now.
    [[nodiscard]] bool tryBind(int port);

private:
    void serverLoop(int port, int timeoutSeconds);

    static OAuthCallback parseQueryString(const std::string& queryString);

    static std::string buildSuccessPage();
    static std::string buildErrorPage(const std::string& message);

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_complete{false};
    std::atomic<bool> m_shouldStop{false};
    std::unique_ptr<std::thread> m_serverThread;
    OAuthCallback m_result;
    OAuthCallbackHandler m_callback;
};

} // namespace warcraftlogs::http
