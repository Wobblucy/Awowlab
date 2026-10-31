#include "WclSettings.h"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#endif

namespace {

// Entry names in the settings file. The plain-text token name is what
// older builds serialized; current builds only ever remove it.
constexpr const char* kQueriesUsed = "wclQueriesUsed";
constexpr const char* kWindowStartTime = "wclWindowStartTime";
constexpr const char* kAccessTokenLegacy = "wclAccessToken";
constexpr const char* kAccessTokenProtected = "wclAccessTokenProtected";
constexpr const char* kTokenExpiresAt = "wclTokenExpiresAt";

// Parse one raw-JSON entry from extraValues; returns a document with a
// parse error when the entry is absent or malformed.
rapidjson::Document parseEntry(const UnifiedSettings::AllSettings& settings,
                               const char* name) {
    rapidjson::Document doc;
    auto it = settings.extraValues.find(name);
    if (it == settings.extraValues.end()) {
        doc.Parse("");  // guaranteed parse error -> "not present"
    } else {
        doc.Parse(it->second.c_str());
    }
    return doc;
}

std::string encodeString(const std::string& value) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.String(value.c_str(), static_cast<rapidjson::SizeType>(value.size()));
    return buffer.GetString();
}

#ifdef _WIN32

// Wrap the token with the Windows user-account key (DPAPI). Only the same
// Windows user on the same machine can unwrap the result, so the token
// stays confidential even though the settings file key is public in the
// open-source overlay.
bool wrapWithUserKey(const std::string& plain, std::vector<uint8_t>& blob) {
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
    input.cbData = static_cast<DWORD>(plain.size());

    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"AwowLab WCL token", nullptr, nullptr,
                          nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return false;
    }
    blob.assign(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    return true;
}

// Reverse of wrapWithUserKey. Fails (returns false) for blobs written by
// a different user or machine, or for damaged data.
bool unwrapWithUserKey(const std::vector<uint8_t>& blob, std::string& plain) {
    DATA_BLOB input;
    input.pbData = const_cast<BYTE*>(blob.data());
    input.cbData = static_cast<DWORD>(blob.size());

    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return false;
    }
    plain.assign(reinterpret_cast<const char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return true;
}

// The settings file stores strings, so the wrapped bytes travel as base64.
std::string toBase64(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) {
        return {};
    }
    DWORD textLen = 0;
    if (!CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              nullptr, &textLen)) {
        return {};
    }
    std::string text(textLen, '\0');
    if (!CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              text.data(), &textLen)) {
        return {};
    }
    text.resize(textLen);  // drop the terminator the sizing call counted
    return text;
}

bool fromBase64(const std::string& text, std::vector<uint8_t>& bytes) {
    if (text.empty()) {
        return false;
    }
    DWORD byteLen = 0;
    if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()),
                              CRYPT_STRING_BASE64, nullptr, &byteLen,
                              nullptr, nullptr)) {
        return false;
    }
    bytes.resize(byteLen);
    if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()),
                              CRYPT_STRING_BASE64, bytes.data(), &byteLen,
                              nullptr, nullptr)) {
        return false;
    }
    bytes.resize(byteLen);
    return true;
}

#else

// The app only ships on Windows; without DPAPI the token is simply not
// persisted, and the user re-authenticates each run.
bool wrapWithUserKey(const std::string&, std::vector<uint8_t>&) { return false; }
bool unwrapWithUserKey(const std::vector<uint8_t>&, std::string&) { return false; }
std::string toBase64(const std::vector<uint8_t>&) { return {}; }
bool fromBase64(const std::string&, std::vector<uint8_t>&) { return false; }

#endif

} // anonymous namespace

WclSettings WclSettings::readFrom(const UnifiedSettings::AllSettings& settings) {
    WclSettings out;

    auto queries = parseEntry(settings, kQueriesUsed);
    if (!queries.HasParseError() && queries.IsInt()) {
        out.queriesUsed = queries.GetInt();
    }

    auto windowStart = parseEntry(settings, kWindowStartTime);
    if (!windowStart.HasParseError() && windowStart.IsInt64()) {
        out.windowStartTime = windowStart.GetInt64();
    }

    auto protectedToken = parseEntry(settings, kAccessTokenProtected);
    if (!protectedToken.HasParseError() && protectedToken.IsString()) {
        std::vector<uint8_t> blob;
        std::string plain;
        if (fromBase64(protectedToken.GetString(), blob) &&
            unwrapWithUserKey(blob, plain)) {
            out.accessToken = std::move(plain);
        }
        // Unwrap failure means the blob came from another user/machine or
        // is damaged. Leave the token empty; the app re-authenticates.
    } else {
        // Settings files from older builds still hold the token as plain
        // text. Read it so updating doesn't force a re-login; writeTo
        // replaces it with the protected entry.
        auto legacy = parseEntry(settings, kAccessTokenLegacy);
        if (!legacy.HasParseError() && legacy.IsString()) {
            out.accessToken = legacy.GetString();
        }
    }

    auto expires = parseEntry(settings, kTokenExpiresAt);
    if (!expires.HasParseError() && expires.IsInt64()) {
        out.tokenExpiresAt = expires.GetInt64();
    }

    return out;
}

void WclSettings::writeTo(UnifiedSettings::AllSettings& settings) const {
    settings.extraValues[kQueriesUsed] = std::to_string(queriesUsed);
    settings.extraValues[kWindowStartTime] = std::to_string(windowStartTime);
    settings.extraValues[kTokenExpiresAt] = std::to_string(tokenExpiresAt);

    // The token only ever leaves this function wrapped with the user's
    // Windows key; the plain-text entry older builds wrote is dropped.
    settings.extraValues.erase(kAccessTokenLegacy);
    if (accessToken.empty()) {
        settings.extraValues.erase(kAccessTokenProtected);
    } else {
        std::vector<uint8_t> blob;
        if (wrapWithUserKey(accessToken, blob)) {
            settings.extraValues[kAccessTokenProtected] = encodeString(toBase64(blob));
        }
        // If wrapping fails, keep whatever protected entry was already
        // there rather than losing the stored token.
    }
}

std::string& awow::wclAuthError() {
    static std::string message;
    return message;
}

void awow::moveWclTokenToProtectedStorage() {
    auto& cache = SettingsCache::instance();
    auto& settings = cache.get();
    if (settings.extraValues.find(kAccessTokenLegacy) == settings.extraValues.end()) {
        return;  // nothing plain-text left to move
    }

    // readFrom picks up the plain-text token (or the protected one if both
    // exist); writeTo stores it protected and drops the plain entry.
    auto wcl = WclSettings::readFrom(settings);
    wcl.writeTo(settings);
    cache.forceSave();
}
