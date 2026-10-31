#include "Core/Benchmark/BenchmarkStore.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace benchmark::store {

namespace {

// Next to the settings file rather than inside it, in the same folder so a
// backup of "User Settings" includes it.
constexpr const char* kDirectory = "User Settings/benchmark";

std::string gRootOverride;

std::filesystem::path pathFor(const char* filename) {
    if (!gRootOverride.empty()) {
        return std::filesystem::path(gRootOverride) / filename;
    }
    return std::filesystem::path(kDirectory) / filename;
}

} // namespace

void setRootDirectoryForTesting(const std::string& root) {
    gRootOverride = root;
}

std::string classificationsPath() {
    return pathFor("classifications.json").string();
}

std::string mentorsPath() {
    return pathFor("mentors.json").string();
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool writeFile(const std::string& path, const std::string& contents) {
    const std::filesystem::path target(path);

    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) return false;

    // Written to a sibling temporary first, then moved into place. A crash
    // mid-write would otherwise leave a truncated file that parses as empty,
    // silently discarding every ruling the user had made.
    std::filesystem::path temp = target;
    temp += ".tmp";

    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file << contents;
        if (!file) return false;
    }

    std::filesystem::rename(temp, target, ec);
    if (ec) {
        // Rename across an existing file fails on some filesystems; removing
        // the old one first is the documented fallback.
        std::filesystem::remove(target, ec);
        std::filesystem::rename(temp, target, ec);
    }
    if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

} // namespace benchmark::store
