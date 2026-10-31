#include "LocalHttpServer.h"
#include "Core/ErrorLogger.h"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <sstream>
#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace warcraftlogs::http {

LocalHttpServer::LocalHttpServer() = default;

LocalHttpServer::~LocalHttpServer() {
    stop();
}

bool LocalHttpServer::waitForCallback(
    OAuthCallbackHandler callback,
    int port,
    int timeoutSeconds
) {
    m_callback = std::move(callback);
    m_running = true;
    m_complete = false;
    m_shouldStop = false;

    serverLoop(port, timeoutSeconds);

    if (m_complete && m_callback) {
        m_callback(m_result);
    }

    return m_complete && m_result.isSuccess();
}

bool LocalHttpServer::startAsync(int port, int timeoutSeconds) {
    if (m_running) {
        return false;
    }

    m_running = true;
    m_complete = false;
    m_shouldStop = false;

    m_serverThread = std::make_unique<std::thread>([this, port, timeoutSeconds]() {
        serverLoop(port, timeoutSeconds);
    });

    return true;
}

std::optional<OAuthCallback> LocalHttpServer::getResult() const {
    if (m_complete) {
        return m_result;
    }
    return std::nullopt;
}

bool LocalHttpServer::isComplete() const noexcept {
    return m_complete;
}

bool LocalHttpServer::isRunning() const noexcept {
    return m_running;
}

std::string LocalHttpServer::getLastError() const noexcept {
    if (m_result.error.empty()) {
        return {};
    }

    std::string errorMsg = m_result.error;
    if (!m_result.errorDescription.empty()) {
        errorMsg += ": " + m_result.errorDescription;
    }
    return errorMsg;
}

void LocalHttpServer::stop() {
    m_shouldStop = true;

    if (m_serverThread && m_serverThread->joinable()) {
        m_serverThread->join();
    }
    m_serverThread.reset();
    m_running = false;
}

std::string LocalHttpServer::getRedirectUri(int port) {
    return "http://localhost:" + std::to_string(port) + "/callback";
}

bool LocalHttpServer::tryBind(int port) {
    try {
        net::io_context ioc;
        // Loopback only, never all interfaces.
        auto loopback = net::ip::make_address("127.0.0.1");
        tcp::acceptor acceptor(ioc, tcp::endpoint(loopback, static_cast<unsigned short>(port)));
        acceptor.close();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

namespace {

// How long a connection gets to send its request. Browsers open spare
// connections that never send anything; those must not hold up sign-in.
constexpr auto kRequestReadTimeout = std::chrono::seconds(15);

// One browser connection, kept alive by the handlers that use it
struct Connection {
    beast::tcp_stream stream;
    beast::flat_buffer buffer;
    ::http::request<::http::string_body> req;
    ::http::response<::http::string_body> res;

    explicit Connection(tcp::socket socket) : stream(std::move(socket)) {}
};

std::string escapeHtml(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;        break;
        }
    }
    return out;
}

// For a user whose browser sat on a spinning page until the timeout.
void logTimeoutHints(int port) {
    const std::string portText = std::to_string(port);
    AWOW_LOG_INFO("OAUTH", "TROUBLESHOOTING: If the browser shows a spinning page, the callback is being blocked.");
    AWOW_LOG_INFO("OAUTH", "  1. Check if port " + portText + " is in use: netstat -ano | findstr :" + portText);
    AWOW_LOG_INFO("OAUTH", "  2. Check Windows Firewall: Allow AwowLab through firewall for private networks");
    AWOW_LOG_INFO("OAUTH", "  3. Temporarily disable antivirus to test if it's blocking localhost connections");
}

// Common bind failures get a hint about the likely cause.
void logBindFailureHints(const std::string& errorMsg, int port) {
    const std::string portText = std::to_string(port);
    if (errorMsg.find("bind") != std::string::npos ||
        errorMsg.find("address") != std::string::npos ||
        errorMsg.find("Address already in use") != std::string::npos ||
        errorMsg.find("10048") != std::string::npos) {  // WSAEADDRINUSE
        AWOW_LOG_INFO("OAUTH", "TROUBLESHOOTING: Port " + portText + " appears to be in use by another application");
        AWOW_LOG_INFO("OAUTH", "  Run this command to find what's using it: netstat -ano | findstr :" + portText);
    } else if (errorMsg.find("permission") != std::string::npos ||
               errorMsg.find("10013") != std::string::npos) {  // WSAEACCES
        AWOW_LOG_INFO("OAUTH", "TROUBLESHOOTING: Permission denied binding to port " + portText);
        AWOW_LOG_INFO("OAUTH", "  Try running AwowLab as Administrator, or check firewall settings");
    }
}

} // namespace

void LocalHttpServer::serverLoop(int port, int timeoutSeconds) {
    AWOW_LOG_INFO("OAUTH", "Starting OAuth callback server on port " + std::to_string(port));

    try {
        net::io_context ioc;

        // Loopback only, never all interfaces.
        auto loopback = net::ip::make_address("127.0.0.1");
        tcp::acceptor acceptor(ioc, tcp::endpoint(loopback, static_cast<unsigned short>(port)));
        acceptor.set_option(net::socket_base::reuse_address(true));

        AWOW_LOG_INFO("OAUTH", "Server bound successfully to port " + std::to_string(port) +
            " - waiting for browser callback (timeout: " + std::to_string(timeoutSeconds) + "s)");

        // Set once a request for /callback has been answered (or accepting
        // failed). Other requests, such as /favicon.ico, get a 404 and the
        // server keeps waiting.
        bool done = false;
        bool responseSent = false;

        auto handleConnection = [&](tcp::socket socket) {
            auto conn = std::make_shared<Connection>(std::move(socket));
            conn->stream.expires_after(kRequestReadTimeout);

            ::http::async_read(conn->stream, conn->buffer, conn->req,
                [&, conn](beast::error_code ec, std::size_t) {
                    if (ec) {
                        AWOW_LOG_INFO("OAUTH", "HTTP read error: " + ec.message() +
                            " (code: " + std::to_string(ec.value()) + ")");
                        conn->stream.close();
                        return;
                    }

                    if (done) {
                        conn->stream.close();
                        return;
                    }

                    // The query string carries the authorization code, so
                    // only the path is logged.
                    std::string target(conn->req.target());
                    auto queryPos = target.find('?');
                    std::string path = target.substr(0, queryPos);
                    AWOW_LOG_INFO("OAUTH", "Received request for " + path);

                    bool isCallback = (path == "/callback");

                    auto& res = conn->res;
                    res.version(conn->req.version());
                    res.keep_alive(false);
                    res.set(::http::field::server, "AwowLab OAuth Handler");

                    if (isCallback) {
                        if (queryPos != std::string::npos) {
                            m_result = parseQueryString(target.substr(queryPos + 1));
                        } else {
                            m_result.error = "invalid_callback";
                            m_result.errorDescription = "No query parameters in callback";
                        }

                        res.result(::http::status::ok);
                        res.set(::http::field::content_type, "text/html");
                        res.body() = m_result.isSuccess()
                            ? buildSuccessPage()
                            : buildErrorPage(m_result.errorDescription);

                        done = true;
                        beast::error_code closeEc;
                        acceptor.close(closeEc);
                    } else {
                        res.result(::http::status::not_found);
                        res.set(::http::field::content_type, "text/plain");
                        res.body() = "Not found";
                    }

                    res.prepare_payload();
                    conn->stream.expires_after(kRequestReadTimeout);
                    ::http::async_write(conn->stream, res,
                        [&, conn, isCallback](beast::error_code, std::size_t) {
                            beast::error_code shutdownEc;
                            conn->stream.socket().shutdown(tcp::socket::shutdown_send, shutdownEc);
                            if (isCallback) {
                                responseSent = true;
                            }
                        });
                });
        };

        std::function<void()> acceptNext = [&]() {
            acceptor.async_accept([&](beast::error_code ec, tcp::socket socket) {
                if (done) {
                    return;
                }

                if (ec) {
                    AWOW_LOG_INFO("OAUTH", "Accept error: " + ec.message() +
                        " (code: " + std::to_string(ec.value()) + ")");
                    m_result.error = "network_error";
                    m_result.errorDescription = ec.message();
                    done = true;
                    responseSent = true;
                    return;
                }

                AWOW_LOG_INFO("OAUTH", "Connection received from browser");
                handleConnection(std::move(socket));
                acceptNext();
            });
        };
        acceptNext();

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);

        while (!done && !m_shouldStop) {
            ioc.run_for(std::chrono::milliseconds(100));

            if (!done && std::chrono::steady_clock::now() > deadline) {
                AWOW_LOG_INFO("OAUTH", "Callback server timed out - no callback received after " +
                    std::to_string(timeoutSeconds) + " seconds");
                logTimeoutHints(port);
                m_result.error = "timeout";
                m_result.errorDescription = "Authorization timed out";
                m_complete = true;
                m_running = false;
                return;
            }
        }

        if (!done) {
            AWOW_LOG_INFO("OAUTH", "Callback server stopped by user");
            m_running = false;
            return;
        }

        // Give the browser its page before the server goes away
        auto flushDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!responseSent && !m_shouldStop && std::chrono::steady_clock::now() < flushDeadline) {
            ioc.run_for(std::chrono::milliseconds(50));
        }

        if (m_result.isSuccess()) {
            AWOW_LOG_INFO("OAUTH", "OAuth callback processed successfully");
        } else {
            AWOW_LOG_INFO("OAUTH", "OAuth callback error: " + m_result.error +
                (m_result.errorDescription.empty() ? "" : " - " + m_result.errorDescription));
        }

        m_complete = true;
        m_running = false;

    } catch (const std::exception& e) {
        std::string errorMsg = e.what();
        AWOW_LOG_INFO("OAUTH", "OAuth server exception: " + errorMsg);
        logBindFailureHints(errorMsg, port);

        m_result.error = "exception";
        m_result.errorDescription = errorMsg;
        m_complete = true;
        m_running = false;
    }
}

OAuthCallback LocalHttpServer::parseQueryString(const std::string& queryString) {
    OAuthCallback result;

    std::istringstream stream(queryString);
    std::string pair;

    while (std::getline(stream, pair, '&')) {
        auto eqPos = pair.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = pair.substr(0, eqPos);
        std::string value = pair.substr(eqPos + 1);

        // Decodes %XX and '+'.
        std::string decoded;
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '%' && i + 2 < value.size()) {
                int hex = 0;
                std::istringstream hexStream(value.substr(i + 1, 2));
                hexStream >> std::hex >> hex;
                decoded += static_cast<char>(hex);
                i += 2;
            } else if (value[i] == '+') {
                decoded += ' ';
            } else {
                decoded += value[i];
            }
        }

        if (key == "code") {
            result.code = decoded;
        } else if (key == "state") {
            result.state = decoded;
        } else if (key == "error") {
            result.error = decoded;
        } else if (key == "error_description") {
            result.errorDescription = decoded;
        }
    }

    return result;
}

std::string LocalHttpServer::buildSuccessPage() {
    return R"html(<!DOCTYPE html>
<html>
<head>
    <title>Authorization Successful</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
            color: #fff;
        }
        .container {
            text-align: center;
            padding: 40px;
            background: rgba(255,255,255,0.1);
            border-radius: 16px;
            backdrop-filter: blur(10px);
        }
        .success-icon {
            font-size: 64px;
            margin-bottom: 20px;
        }
        h1 { margin: 0 0 10px 0; }
        p { opacity: 0.8; margin: 0; }
    </style>
</head>
<body>
    <div class="container">
        <div class="success-icon">&#10003;</div>
        <h1>Authorization Successful!</h1>
        <p>You can close this window and return to AwowLab.</p>
    </div>
</body>
</html>)html";
}

std::string LocalHttpServer::buildErrorPage(const std::string& message) {
    std::string html = R"html(<!DOCTYPE html>
<html>
<head>
    <title>Authorization Failed</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
            background: linear-gradient(135deg, #2e1a1a 0%, #3e1616 100%);
            color: #fff;
        }
        .container {
            text-align: center;
            padding: 40px;
            background: rgba(255,255,255,0.1);
            border-radius: 16px;
            backdrop-filter: blur(10px);
        }
        .error-icon {
            font-size: 64px;
            margin-bottom: 20px;
        }
        h1 { margin: 0 0 10px 0; }
        p { opacity: 0.8; margin: 0; }
        .error-msg {
            margin-top: 15px;
            padding: 10px;
            background: rgba(255,0,0,0.2);
            border-radius: 8px;
            font-family: monospace;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="error-icon">&#10007;</div>
        <h1>Authorization Failed</h1>
        <p>There was a problem with the authorization.</p>
        <div class="error-msg">)html";

    html += escapeHtml(message);
    html += R"html(</div>
    </div>
</body>
</html>)html";

    return html;
}

} // namespace warcraftlogs::http
