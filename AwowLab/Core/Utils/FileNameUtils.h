#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// Helpers for file names the user types in. The app keeps text as UTF-8, but
// a narrow std::string handed to the filesystem on Windows is read in the
// ANSI code page, so names go through pathFromUtf8/utf8FromPath.
namespace awow::files {

// Characters Windows refuses in a file name, plus control characters.
inline bool isForbiddenFileNameChar(unsigned char c) {
    if (c < 0x20) return true;
    switch (c) {
        case '<': case '>': case ':': case '"':
        case '/': case '\\': case '|': case '?': case '*':
            return true;
        default:
            return false;
    }
}

// True when name is a single file name with nothing that could leave the
// folder it is joined to: no separators, no drive colon, not "." or "..".
// Windows also drops trailing dots and spaces, so those are refused too
// rather than quietly naming a different file.
inline bool isPlainFileName(std::string_view name) {
    if (name.empty() || name == "." || name == "..") return false;
    for (char c : name) {
        if (isForbiddenFileNameChar(static_cast<unsigned char>(c))) return false;
    }
    char last = name.back();
    return last != '.' && last != ' ';
}

// Turns a display name into a file stem: spaces become underscores, ASCII
// letters are lowercased, characters Windows forbids are dropped. Anything
// else, non-ASCII letters included, is kept so different names stay
// different files. Returns fallback if nothing usable is left.
inline std::string toFileStem(std::string_view name, std::string_view fallback) {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        auto uc = static_cast<unsigned char>(c);
        if (isForbiddenFileNameChar(uc)) continue;
        if (c == ' ') {
            result += '_';
        } else if (uc >= 'A' && uc <= 'Z') {
            result += static_cast<char>(uc - 'A' + 'a');
        } else {
            result += c;
        }
    }
    // Trailing dots would be stripped by Windows; "." and ".." are not names
    while (!result.empty() && result.back() == '.') {
        result.pop_back();
    }
    if (result.empty()) {
        result = fallback;
    }
    return result;
}

// True when path names an .awsnap snapshot. The bare extension on its own
// is not a file name, so it does not count.
inline bool isSnapshotPath(std::string_view path) {
    constexpr std::string_view extension = ".awsnap";
    return path.size() > extension.size() && path.ends_with(extension);
}

inline std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

inline std::string utf8FromPath(const std::filesystem::path& path) {
    std::u8string u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

} // namespace awow::files
