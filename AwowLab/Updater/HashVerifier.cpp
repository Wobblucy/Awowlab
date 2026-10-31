#include "HashVerifier.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "bcrypt.lib")

namespace awow::updater {

thread_local std::string HashVerifier::lastError_;

namespace {

constexpr size_t kDigestSize = 32;          // SHA-256
constexpr size_t kFileReadChunk = 64 * 1024;

// One SHA-256 computation. The BCrypt handles are released however the
// caller leaves.
class Sha256 {
public:
    Sha256() {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg_, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
            alg_ = nullptr;
            error_ = "Failed to open BCrypt algorithm provider";
            return;
        }
        if (!BCRYPT_SUCCESS(BCryptCreateHash(alg_, &hash_, nullptr, 0, nullptr, 0, 0))) {
            hash_ = nullptr;
            error_ = "Failed to create hash object";
        }
    }

    ~Sha256() {
        if (hash_) BCryptDestroyHash(hash_);
        if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
    }

    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    // Empty when the hash object was created.
    const std::string& setupError() const { return error_; }

    bool add(const uint8_t* data, size_t size) {
        return BCRYPT_SUCCESS(BCryptHashData(hash_, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0));
    }

    bool finish(std::vector<uint8_t>& digest) {
        digest.resize(kDigestSize);
        return BCRYPT_SUCCESS(BCryptFinishHash(hash_, digest.data(), static_cast<ULONG>(digest.size()), 0));
    }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::string error_;
};

} // namespace

std::string HashVerifier::bytesToHex(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    for (uint8_t b : bytes) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

std::string HashVerifier::hashData(const uint8_t* data, size_t size) {
    Sha256 sha;
    if (!sha.setupError().empty()) {
        lastError_ = sha.setupError();
        return "";
    }

    if (!sha.add(data, size)) {
        lastError_ = "Failed to hash data";
        return "";
    }

    std::vector<uint8_t> digest;
    if (!sha.finish(digest)) {
        lastError_ = "Failed to finish hash";
        return "";
    }
    return bytesToHex(digest);
}

std::string HashVerifier::hashFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        lastError_ = "Failed to open file: " + filePath;
        return "";
    }

    Sha256 sha;
    if (!sha.setupError().empty()) {
        lastError_ = sha.setupError();
        return "";
    }

    std::vector<uint8_t> buffer(kFileReadChunk);
    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        std::streamsize bytesRead = file.gcount();
        if (bytesRead > 0 && !sha.add(buffer.data(), static_cast<size_t>(bytesRead))) {
            lastError_ = "Failed to hash file data";
            return "";
        }
    }

    std::vector<uint8_t> digest;
    if (!sha.finish(digest)) {
        lastError_ = "Failed to finish hash";
        return "";
    }
    return bytesToHex(digest);
}

bool HashVerifier::verifyFile(const std::string& filePath, const std::string& expectedHash) {
    std::string actualHash = hashFile(filePath);
    if (actualHash.empty()) {
        return false;
    }

    auto toLower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };

    if (toLower(actualHash) != toLower(expectedHash)) {
        lastError_ = "Hash mismatch: expected " + expectedHash + ", got " + actualHash;
        return false;
    }

    return true;
}

const std::string& HashVerifier::getLastError() {
    return lastError_;
}

} // namespace awow::updater
