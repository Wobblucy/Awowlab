#include "HttpDownloader.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winhttp.h>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <memory>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace awow::updater {

namespace {

constexpr size_t kReadBufferSize = 64 * 1024;

struct WinHttpCloser {
    void operator()(HINTERNET handle) const { WinHttpCloseHandle(handle); }
};
using WinHttpHandle = std::unique_ptr<void, WinHttpCloser>;

// The Location header of a redirect, or empty if there is none.
std::string redirectLocation(HINTERNET request) {
    DWORD locationSize = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                        nullptr, &locationSize, WINHTTP_NO_HEADER_INDEX);
    if (locationSize == 0) {
        return {};
    }

    std::wstring location(locationSize / sizeof(wchar_t), L'\0');
    WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                        location.data(), &locationSize, WINHTTP_NO_HEADER_INDEX);

    // Location headers are ASCII, so narrowing each character is enough.
    std::string url;
    url.reserve(location.size());
    for (wchar_t wc : location) {
        if (wc == L'\0') break;
        url.push_back(static_cast<char>(wc));
    }
    return url;
}

} // namespace

HttpDownloader::HttpDownloader() = default;
HttpDownloader::~HttpDownloader() = default;

bool HttpDownloader::parseUrl(const std::string& url, UrlParts& parts) {
    // Update traffic is https-only; plain http would let anyone on the
    // network path tamper with downloads, so refuse it outright.
    std::string lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower.starts_with("https://")) {
        parts.port = 443;
    } else {
        lastError_ = "URL must start with https://";
        return false;
    }

    size_t schemeEnd = url.find("://");
    std::string rest = url.substr(schemeEnd + 3);

    size_t pathStart = rest.find('/');
    if (pathStart == std::string::npos) {
        parts.host = rest;
        parts.path = "/";
    } else {
        parts.host = rest.substr(0, pathStart);
        parts.path = rest.substr(pathStart);
    }

    size_t colonPos = parts.host.find(':');
    if (colonPos != std::string::npos) {
        std::string portText = parts.host.substr(colonPos + 1);
        unsigned int port = 0;
        auto [end, ec] = std::from_chars(portText.data(), portText.data() + portText.size(), port);
        if (ec != std::errc() || end != portText.data() + portText.size() ||
            port == 0 || port > 65535) {
            lastError_ = "Invalid port in URL: " + portText;
            return false;
        }
        parts.port = static_cast<uint16_t>(port);
        parts.host = parts.host.substr(0, colonPos);
    }

    return true;
}

bool HttpDownloader::downloadToFile(const std::string& url,
                                     const std::string& destPath,
                                     DownloadProgressCallback progress) {
    UrlParts parts;
    if (!parseUrl(url, parts)) {
        return false;
    }
    return performRequest(parts, nullptr, &destPath, progress);
}

bool HttpDownloader::performRequest(const UrlParts& parts,
                                     std::string* outString,
                                     const std::string* outFile,
                                     DownloadProgressCallback progress,
                                     int redirectsLeft) {
    lastStatusCode_ = 0;

    std::wstring wideHost(parts.host.begin(), parts.host.end());
    std::wstring widePath(parts.path.begin(), parts.path.end());

    WinHttpHandle session(WinHttpOpen(
        L"AwowLabUpdater/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    ));

    if (!session) {
        lastError_ = "Failed to open WinHTTP session";
        return false;
    }

    WinHttpSetTimeouts(session.get(), timeoutMs_, timeoutMs_, timeoutMs_, timeoutMs_);

    WinHttpHandle connection(WinHttpConnect(session.get(), wideHost.c_str(), parts.port, 0));
    if (!connection) {
        lastError_ = "Failed to connect to " + parts.host;
        return false;
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.get(),
        L"GET",
        widePath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    ));

    if (!request) {
        lastError_ = "Failed to create HTTP request";
        return false;
    }

    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        lastError_ = "Failed to send HTTP request";
        return false;
    }

    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        lastError_ = "Failed to receive HTTP response";
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(request.get(),
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
    lastStatusCode_ = static_cast<int>(statusCode);

    // GitHub release downloads redirect to a CDN.
    if (statusCode >= 300 && statusCode < 400) {
        std::string redirectUrl = redirectLocation(request.get());
        if (!redirectUrl.empty()) {
            // Closed before following, so a chain of redirects doesn't keep
            // a connection open for every hop.
            request.reset();
            connection.reset();
            session.reset();

            if (redirectsLeft <= 0) {
                lastError_ = "Too many redirects";
                return false;
            }

            UrlParts redirectParts;
            if (parseUrl(redirectUrl, redirectParts)) {
                return performRequest(redirectParts, outString, outFile, progress, redirectsLeft - 1);
            }
            lastError_ = "Invalid redirect URL";
            return false;
        }
    }

    if (statusCode != 200) {
        lastError_ = "HTTP error: " + std::to_string(statusCode);
        return false;
    }

    size_t totalBytes = 0;
    DWORD contentLength = 0;
    DWORD contentLengthSize = sizeof(contentLength);
    if (WinHttpQueryHeaders(request.get(),
                            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &contentLength, &contentLengthSize, WINHTTP_NO_HEADER_INDEX)) {
        totalBytes = contentLength;
    }

    std::ofstream outFileStream;
    if (outFile) {
        outFileStream.open(*outFile, std::ios::binary);
        if (!outFileStream) {
            lastError_ = "Failed to open output file: " + *outFile;
            return false;
        }
    }

    std::vector<char> buffer(kReadBufferSize);
    size_t bytesReceived = 0;

    while (true) {
        DWORD bytesAvailable = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &bytesAvailable)) {
            // A dropped connection mid-body lands here; treating it as the
            // end of the data would hand back a truncated download.
            lastError_ = "Failed to read HTTP data";
            return false;
        }

        if (bytesAvailable == 0) {
            break;
        }

        DWORD toRead = std::min(bytesAvailable, static_cast<DWORD>(buffer.size()));
        DWORD bytesRead = 0;
        if (!WinHttpReadData(request.get(), buffer.data(), toRead, &bytesRead)) {
            lastError_ = "Failed to read HTTP data";
            return false;
        }

        if (bytesRead == 0) {
            break;
        }

        if (outString) {
            outString->append(buffer.data(), bytesRead);
        }
        if (outFile) {
            outFileStream.write(buffer.data(), bytesRead);
            if (!outFileStream) {
                lastError_ = "Failed to write output file (disk full?): " + *outFile;
                return false;
            }
        }

        bytesReceived += bytesRead;

        if (progress) {
            progress(bytesReceived, totalBytes);
        }
    }

    if (outFile) {
        // Buffered bytes only reach the disk here, so a full disk can still
        // fail at this point.
        outFileStream.close();
        if (outFileStream.fail()) {
            lastError_ = "Failed to write output file (disk full?): " + *outFile;
            return false;
        }
    }

    return true;
}

bool HttpDownloader::downloadToString(const std::string& url, std::string& outContent) {
    outContent.clear();
    UrlParts parts;
    if (!parseUrl(url, parts)) {
        return false;
    }
    return performRequest(parts, &outContent, nullptr, nullptr);
}

} // namespace awow::updater
