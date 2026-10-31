#pragma once

#include <string>
#include <optional>
#include <chrono>
#include <map>
#include <stdexcept>

namespace warcraftlogs::http {

class HttpException : public std::runtime_error {
public:
    explicit HttpException(const std::string& message, int statusCode = 0)
        : std::runtime_error(message), m_statusCode(statusCode) {}

    [[nodiscard]] int statusCode() const noexcept { return m_statusCode; }

private:
    int m_statusCode;
};

struct HttpResponse {
    int statusCode;
    std::string body;
    std::map<std::string, std::string> headers;

    [[nodiscard]] bool isSuccess() const noexcept {
        return statusCode >= 200 && statusCode < 300;
    }

    [[nodiscard]] bool isError() const noexcept {
        return statusCode >= 400;
    }
};

// HTTPS client on Boost.Beast. Built as its own library so the Beast headers
// stay out of the code that changes often.
//
// Every request throws HttpException on a network failure. An HTTP error
// status is returned in the response, not thrown.
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    HttpClient(HttpClient&&) noexcept;
    HttpClient& operator=(HttpClient&&) noexcept;

    // Defaults to 120000 ms.
    void setTimeout(int timeoutMs);
    [[nodiscard]] int getTimeout() const noexcept;

    // authToken is sent as a Bearer token.
    [[nodiscard]] HttpResponse post(
        const std::string& host,
        const std::string& path,
        const std::string& body,
        const std::string& contentType = "application/json",
        const std::optional<std::string>& authToken = std::nullopt
    );

    [[nodiscard]] HttpResponse get(
        const std::string& host,
        const std::string& path,
        const std::optional<std::string>& authToken = std::nullopt
    );

    // Form-encoded POST, used for OAuth token exchange. basicAuthCredentials
    // is the already base64-encoded "id:secret" for Basic auth.
    [[nodiscard]] HttpResponse postForm(
        const std::string& host,
        const std::string& path,
        const std::map<std::string, std::string>& formData,
        const std::optional<std::string>& basicAuthCredentials = std::nullopt
    );

private:
    HttpResponse performRequest(
        const std::string& method,
        const std::string& host,
        const std::string& path,
        const std::string& body,
        const std::string& contentType,
        const std::optional<std::string>& authHeader
    );

    static std::string urlEncode(const std::string& value);

    int m_timeoutMs;
};

} // namespace warcraftlogs::http
