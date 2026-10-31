#pragma once

#include <string>
#include <functional>
#include <cstdint>

namespace awow::updater {

// totalBytes is 0 when the server sends no Content-Length.
using DownloadProgressCallback = std::function<void(size_t bytesReceived, size_t totalBytes)>;

// HTTPS downloader on WinHTTP, kept free of third-party libraries so the
// updater binary stays small.
class HttpDownloader {
public:
    HttpDownloader();
    ~HttpDownloader();

    HttpDownloader(const HttpDownloader&) = delete;
    HttpDownloader& operator=(const HttpDownloader&) = delete;

    // For small responses such as JSON.
    bool downloadToString(const std::string& url, std::string& outContent);

    bool downloadToFile(const std::string& url,
                        const std::string& destPath,
                        DownloadProgressCallback progress = nullptr);

    const std::string& getLastError() const { return lastError_; }

    // Connect and read timeout.
    void setTimeout(int timeoutMs) { timeoutMs_ = timeoutMs; }

    int getLastStatusCode() const { return lastStatusCode_; }

    struct UrlParts {
        std::string host;
        uint16_t port = 443;
        std::string path;
    };

    // Fails, with getLastError set, if the URL is not https or the port is
    // not a number from 1 to 65535.
    bool parseUrl(const std::string& url, UrlParts& parts);

    static constexpr int MAX_REDIRECTS = 5;

private:
    bool performRequest(const UrlParts& parts,
                        std::string* outString,
                        const std::string* outFile,
                        DownloadProgressCallback progress,
                        int redirectsLeft = MAX_REDIRECTS);

    std::string lastError_;
    int timeoutMs_ = 60000;
    int lastStatusCode_ = 0;
};

} // namespace awow::updater
