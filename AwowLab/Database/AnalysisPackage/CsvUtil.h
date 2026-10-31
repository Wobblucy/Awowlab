#pragma once

#include "Core/Utils/FileNameUtils.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace awow::analysis {

namespace fs = std::filesystem;

// A field is quoted only when it would otherwise break the row. Actor and spell
// names can contain commas, so they go through here; numbers never do.
inline std::string csvField(std::string_view text) {
    bool needsQuotes = text.find_first_of(",\"\n\r") != std::string_view::npos;
    if (!needsQuotes) return std::string(text);

    std::string out;
    out.reserve(text.size() + 2);
    out += '"';
    for (char c : text) {
        if (c == '"') out += '"';   // doubled, per RFC 4180
        out += c;
    }
    out += '"';
    return out;
}

// Resource amounts carry four decimals in the log and can be fractional, so
// they are written as fixed-point rather than letting the stream pick.
inline std::string amountField(double value) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4) << value;
    return ss.str();
}

// Same escaping rules as CSV but for JSON string values.
inline std::string jsonString(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out += '"';
    for (char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                // Control characters have to be escaped or the JSON is invalid
                if (static_cast<unsigned char>(c) < 0x20) {
                    std::ostringstream esc;
                    esc << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(c));
                    out += esc.str();
                } else {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}


// Shared by the fight and baseline packages, which both stamp a manifest, name
// a zip after the day it was built, and add finished files to an archive.

inline std::string isoTimestampUtc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

inline std::string dateStampUtc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y%m%d", &tm);
    return buf;
}

// Anything going into a filename gets reduced to characters that survive a
// round trip through Windows, a zip entry and a shell.
inline std::string slugify(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool lastWasDash = false;
    for (char c : text) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if ((uc >= 'a' && uc <= 'z') || (uc >= '0' && uc <= '9')) {
            out += c; lastWasDash = false;
        } else if (uc >= 'A' && uc <= 'Z') {
            out += static_cast<char>(c - 'A' + 'a'); lastWasDash = false;
        } else if (!lastWasDash && !out.empty()) {
            out += '_'; lastWasDash = true;
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

// Paths cross this module as UTF-8 strings, which is what the folder picker
// returns. Going through u8string keeps a folder like C:\Users\<Cyrillic name>
// intact: fs::path(std::string) reads the bytes in the ANSI code page, and
// path::string() throws for any name that code page cannot spell.
using awow::files::pathFromUtf8;
using awow::files::utf8FromPath;

} // namespace awow::analysis
