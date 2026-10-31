#pragma once

#include "../Auth/UserOAuthClient.h"
#include "RateLimiter.h"
#include <HttpClient.h>

#include <rapidjson/document.h>
#include <string>
#include <memory>
#include <functional>

namespace warcraftlogs {

using http::HttpException;

class GraphQLException : public std::runtime_error {
public:
    explicit GraphQLException(const std::string& message)
        : std::runtime_error(message) {}
};

// WarcraftLogs GraphQL client. Sends the signed-in user's token to the user
// endpoint, the only one that accepts a PKCE token.
class GraphQLClient {
public:
    explicit GraphQLClient(UserOAuthClient& userOAuthClient);
    GraphQLClient(UserOAuthClient& userOAuthClient, RateLimiter& rateLimiter);

    ~GraphQLClient();

    // Holds references, so neither copyable nor movable.
    GraphQLClient(const GraphQLClient&) = delete;
    GraphQLClient& operator=(const GraphQLClient&) = delete;
    GraphQLClient(GraphQLClient&&) = delete;
    GraphQLClient& operator=(GraphQLClient&&) = delete;

    // Throws GraphQLException when the response carries GraphQL errors, and
    // HttpException on network failure or an HTTP error status.
    [[nodiscard]] rapidjson::Document executeQuery(
        const std::string& query,
        const rapidjson::Document& variables = rapidjson::Document()
    );

    [[nodiscard]] rapidjson::Document executeQuery(
        const std::string& query,
        const std::string& variablesJson
    );

    void setTimeout(int timeoutMs);

    // nullopt when no rate limiter was given.
    [[nodiscard]] std::optional<RateLimitInfo> getRateLimitInfo() const;

    // Asks the API for the current figures.
    RateLimitInfo fetchRateLimitInfo();

private:
    std::string httpsPost(
        const std::string& path,
        const std::string& body,
        const std::string& authToken
    );

    std::string buildRequestBody(
        const std::string& query,
        const rapidjson::Document& variables
    );

    // Throws GraphQLException if the body isn't JSON or reports errors.
    rapidjson::Document parseResponse(const std::string& responseBody);

    std::string getAuthToken();

    UserOAuthClient* m_userOAuthClient = nullptr;
    RateLimiter* m_rateLimiter = nullptr;           // may be null
    http::HttpClient m_httpClient;
};

} // namespace warcraftlogs
