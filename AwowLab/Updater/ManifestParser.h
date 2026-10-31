#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace awow::updater {

struct FileEntry {
    std::string path;       // relative, e.g. "AwowLab.exe", "shaders/sphere.spv"
    std::string sha256;     // lowercase hex
    size_t size = 0;
    std::string url;
};

struct OptionalPack {
    std::string id;         // e.g. "langpack_ko_KR"
    std::string path;
    std::string sha256;
    size_t size = 0;
    std::string url;
    std::string displayName;
};

struct ChunkEntry {
    int part = 0;           // 1-based
    std::string sha256;
    size_t size = 0;
    std::string url;
};

// A ZIP that extracts into a directory, for large sets of files such as
// spell icons and world maps. Either a single ZIP (sha256/size/url set,
// chunks empty) or a chunked group (group and chunks set), where only the
// chunks whose hash changed are downloaded again.
struct ArchiveEntry {
    std::string extractTo;  // e.g. "PNG/Spell Icons"
    std::string sha256;     // single-file archives only
    size_t size = 0;        // single-file archives only
    std::string url;        // single-file archives only

    std::string group;      // e.g. "SpellIcons"
    std::vector<ChunkEntry> chunks;

    [[nodiscard]] bool isChunked() const { return !chunks.empty(); }
};

struct Manifest {
    std::string version;
    int updaterVersion = 0;                 // oldest updater that can install this; 0 = any
    std::vector<FileEntry> files;
    std::vector<ArchiveEntry> archives;
    std::vector<std::string> folders;       // directories to create, e.g. "custom_models"
    std::unordered_map<std::string, OptionalPack> optionalPacks;  // by id
};

// version.json published next to each release.
struct VersionInfo {
    std::string version;
    std::string releaseDate;    // ISO date
    std::string manifestUrl;
    std::string changelogUrl;
};

// version.json written into the install folder.
struct LocalVersionInfo {
    std::string version;
    std::unordered_map<std::string, std::string> installedFiles;  // path -> sha256
};

class ManifestParser {
public:
    static bool parseVersionInfo(const std::string& json, VersionInfo& outInfo);
    static bool parseManifest(const std::string& json, Manifest& outManifest);
    static bool parseLocalVersion(const std::string& json, LocalVersionInfo& outInfo);
    static std::string generateLocalVersion(const LocalVersionInfo& info);

    // Reads a chunked archive's .archive_hashes file into part -> SHA256,
    // or an empty map if it can't be parsed. The file lives in the install
    // folder and may be damaged or edited, so entries whose part isn't a
    // number are skipped; those chunks are simply downloaded again.
    static std::unordered_map<int, std::string> parseChunkHashes(const std::string& json);

    // -1, 0 or 1 as a is older than, equal to or newer than b.
    static int compareVersions(const std::string& a, const std::string& b);

    static const std::string& getLastError();

private:
    static thread_local std::string lastError_;
};

} // namespace awow::updater
