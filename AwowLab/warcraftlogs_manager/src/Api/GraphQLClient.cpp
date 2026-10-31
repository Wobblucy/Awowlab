#include "Api/GraphQLClient.h"
#include "Api/Queries.h"
#include "Adapter/WclJsonHelpers.h"
#include "Auth/Credentials.h"
#include "Core/ErrorLogger.h"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>

#include <sstream>

namespace warcraftlogs {

namespace {

// Until the caller sets its own with setTimeout().
constexpr int kDefaultRequestTimeoutMs = 30000;

// The first part of a response body that goes into the log on a parse error.
constexpr size_t kLoggedBodyLength = 500;

} // namespace

GraphQLClient::GraphQLClient(OAuthClient& oauthClient)
    : m_oauthClient(&oauthClient)
    , m_endpoint(ApiEndpoint::Client)
{
    m_httpClient.setTimeout(kDefaultRequestTimeoutMs);
}

GraphQLClient::GraphQLClient(OAuthClient& oauthClient, RateLimiter& rateLimiter)
    : GraphQLClient(oauthClient)
{
    m_rateLimiter = &rateLimiter;
}

GraphQLClient::GraphQLClient(UserOAuthClient& userOAuthClient)
    : m_userOAuthClient(&userOAuthClient)
    , m_endpoint(ApiEndpoint::User)
{
    m_httpClient.setTimeout(kDefaultRequestTimeoutMs);
}

GraphQLClient::GraphQLClient(UserOAuthClient& userOAuthClient, RateLimiter& rateLimiter)
    : GraphQLClient(userOAuthClient)
{
    m_rateLimiter = &rateLimiter;
}

GraphQLClient::~GraphQLClient() = default;

rapidjson::Document GraphQLClient::executeQuery(
    const std::string& query,
    const rapidjson::Document& variables
) {
    std::string body = buildRequestBody(query, variables);

    std::string token = getAuthToken();

    // User (PKCE) tokens only work on /api/v2/user; client-credentials tokens
    // use /api/v2/client.
    const char* endpoint = (m_endpoint == ApiEndpoint::User)
        ? "/api/v2/user"
        : "/api/v2/client";

    std::string response = httpsPost(endpoint, body, token);

    return parseResponse(response);
}

rapidjson::Document GraphQLClient::executeQuery(
    const std::string& query,
    const std::string& variablesJson
) {
    rapidjson::Document variables;
    if (!variablesJson.empty()) {
        variables.Parse(variablesJson.c_str());
        if (variables.HasParseError()) {
            std::string errorMsg = "Invalid variables JSON: " +
                std::string(rapidjson::GetParseError_En(variables.GetParseError()));
            AWOW_LOG_ERROR("WCL", errorMsg);
            throw GraphQLException(errorMsg);
        }
    }
    return executeQuery(query, variables);
}

void GraphQLClient::setTimeout(int timeoutMs) {
    m_httpClient.setTimeout(timeoutMs);
}

std::optional<RateLimitInfo> GraphQLClient::getRateLimitInfo() const {
    if (m_rateLimiter) {
        return m_rateLimiter->getInfo();
    }
    return std::nullopt;
}

RateLimitInfo GraphQLClient::fetchRateLimitInfo() {
    rapidjson::Document response = executeQuery(queries::RATE_LIMIT, rapidjson::Document());

    if (!response.HasMember("data") || !response["data"].IsObject() ||
        !response["data"].HasMember("rateLimitData")) {
        std::string errorMsg = "Rate limit response missing expected data";
        AWOW_LOG_ERROR("WCL", errorMsg);
        throw GraphQLException(errorMsg);
    }

    const auto& rateLimitData = response["data"]["rateLimitData"];

    RateLimitInfo info;
    info.limitPerHour = json::getInt(rateLimitData, "limitPerHour");
    info.pointsSpentThisHour = json::getInt(rateLimitData, "pointsSpentThisHour");
    info.pointsResetIn = json::getInt(rateLimitData, "pointsResetIn");
    info.userPointsResetIn = json::getInt(rateLimitData, "userPointsResetIn", -1);
    info.lastUpdated = std::chrono::system_clock::now();

    if (m_rateLimiter) {
        m_rateLimiter->update(info.limitPerHour, info.pointsSpentThisHour, info.pointsResetIn);
        RateLimitInfo tracked = m_rateLimiter->getInfo();
        tracked.userPointsResetIn = info.userPointsResetIn;
        return tracked;
    }
    return info;
}

std::string GraphQLClient::httpsPost(
    const std::string& path,
    const std::string& body,
    const std::string& authToken
) {
    auto response = m_httpClient.post(
        credentials::API_HOST,
        path,
        body,
        "application/json",
        authToken
    );

    if (response.isError()) {
        std::string errorMsg = "HTTP request failed with status " + std::to_string(response.statusCode) + ": " + response.body;
        AWOW_LOG_ERROR("WCL", errorMsg);
        throw http::HttpException(errorMsg, response.statusCode);
    }

    if (m_rateLimiter) {
        m_rateLimiter->recordRequest();
    }

    return response.body;
}

std::string GraphQLClient::buildRequestBody(
    const std::string& query,
    const rapidjson::Document& variables
) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();

    doc.AddMember("query", rapidjson::Value(query.c_str(), allocator), allocator);

    if (variables.IsObject() && !variables.ObjectEmpty()) {
        rapidjson::Value varsCopy;
        varsCopy.CopyFrom(variables, allocator);
        doc.AddMember("variables", varsCopy, allocator);
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

rapidjson::Document GraphQLClient::parseResponse(const std::string& responseBody) {
    rapidjson::Document doc;
    doc.Parse(responseBody.c_str());

    if (doc.HasParseError()) {
        std::string errorMsg = "Failed to parse GraphQL response: " +
            std::string(rapidjson::GetParseError_En(doc.GetParseError()));
        std::string truncatedBody = responseBody.length() > kLoggedBodyLength ?
            responseBody.substr(0, kLoggedBodyLength) + "..." : responseBody;
        AWOW_LOG_ERROR("WCL", errorMsg + " | Response: " + truncatedBody);
        throw GraphQLException(errorMsg);
    }

    if (doc.HasMember("errors") && doc["errors"].IsArray()) {
        const auto& errors = doc["errors"];
        std::ostringstream errorMsgStream;
        errorMsgStream << "GraphQL errors: ";

        bool first = true;
        for (rapidjson::SizeType i = 0; i < errors.Size(); ++i) {
            if (!first) errorMsgStream << "; ";
            first = false;

            if (errors[i].HasMember("message") && errors[i]["message"].IsString()) {
                errorMsgStream << errors[i]["message"].GetString();
            } else {
                errorMsgStream << "Unknown error";
            }
        }

        std::string errorMsg = errorMsgStream.str();
        AWOW_LOG_ERROR("WCL", errorMsg);
        throw GraphQLException(errorMsg);
    }

    return doc;
}

std::string GraphQLClient::getAuthToken() {
    if (m_userOAuthClient) {
        std::string token = m_userOAuthClient->getToken();
        if (token.empty()) {
            std::string errorMsg = "User OAuth token not available - authentication required";
            AWOW_LOG_ERROR("WCL", errorMsg);
            throw GraphQLException(errorMsg);
        }
        return token;
    }

    if (m_oauthClient) {
        return m_oauthClient->getToken();
    }

    std::string errorMsg = "No OAuth client configured";
    AWOW_LOG_ERROR("WCL", errorMsg);
    throw GraphQLException(errorMsg);
}

} // namespace warcraftlogs
