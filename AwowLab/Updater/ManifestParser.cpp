#include "ManifestParser.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>
#include <sstream>
#include <algorithm>
#include <charconv>

namespace awow::updater {

thread_local std::string ManifestParser::lastError_;

namespace {

// Each reader sets out only when the member exists with the expected type,
// so a missing or mistyped field keeps its default.
void readString(const rapidjson::Value& obj, const char* key, std::string& out) {
    auto it = obj.FindMember(key);
    if (it != obj.MemberEnd() && it->value.IsString()) {
        out = it->value.GetString();
    }
}

void readInt(const rapidjson::Value& obj, const char* key, int& out) {
    auto it = obj.FindMember(key);
    if (it != obj.MemberEnd() && it->value.IsInt()) {
        out = it->value.GetInt();
    }
}

void readSize(const rapidjson::Value& obj, const char* key, size_t& out) {
    auto it = obj.FindMember(key);
    if (it != obj.MemberEnd() && it->value.IsUint64()) {
        out = it->value.GetUint64();
    }
}

bool hasString(const rapidjson::Value& obj, const char* key) {
    auto it = obj.FindMember(key);
    return it != obj.MemberEnd() && it->value.IsString();
}

bool hasArray(const rapidjson::Value& obj, const char* key) {
    auto it = obj.FindMember(key);
    return it != obj.MemberEnd() && it->value.IsArray();
}

} // namespace

bool ManifestParser::parseVersionInfo(const std::string& json, VersionInfo& outInfo) {
    rapidjson::Document doc;
    doc.Parse(json.c_str());

    if (doc.HasParseError()) {
        lastError_ = "JSON parse error at offset " + std::to_string(doc.GetErrorOffset());
        return false;
    }

    if (!doc.IsObject()) {
        lastError_ = "version.json must be an object";
        return false;
    }

    if (!hasString(doc, "version")) {
        lastError_ = "Missing or invalid 'version' field";
        return false;
    }
    readString(doc, "version", outInfo.version);
    readString(doc, "release_date", outInfo.releaseDate);

    if (!hasString(doc, "manifest_url")) {
        lastError_ = "Missing or invalid 'manifest_url' field";
        return false;
    }
    readString(doc, "manifest_url", outInfo.manifestUrl);
    readString(doc, "changelog_url", outInfo.changelogUrl);

    return true;
}

bool ManifestParser::parseManifest(const std::string& json, Manifest& outManifest) {
    rapidjson::Document doc;
    doc.Parse(json.c_str());

    if (doc.HasParseError()) {
        lastError_ = "JSON parse error at offset " + std::to_string(doc.GetErrorOffset());
        return false;
    }

    if (!doc.IsObject()) {
        lastError_ = "manifest.json must be an object";
        return false;
    }

    readString(doc, "version", outManifest.version);
    readInt(doc, "updater_version", outManifest.updaterVersion);

    if (hasArray(doc, "files")) {
        const auto& files = doc["files"];
        outManifest.files.reserve(files.Size());

        for (const auto& fileObj : files.GetArray()) {
            if (!fileObj.IsObject() || !hasString(fileObj, "path")) continue;

            FileEntry entry;
            readString(fileObj, "path", entry.path);
            readString(fileObj, "sha256", entry.sha256);
            readSize(fileObj, "size", entry.size);
            readString(fileObj, "url", entry.url);
            outManifest.files.push_back(std::move(entry));
        }
    }

    if (hasArray(doc, "archives")) {
        const auto& archives = doc["archives"];
        outManifest.archives.reserve(archives.Size());

        for (const auto& archiveObj : archives.GetArray()) {
            if (!archiveObj.IsObject() || !hasString(archiveObj, "extract_to")) continue;

            ArchiveEntry entry;
            readString(archiveObj, "extract_to", entry.extractTo);

            if (hasArray(archiveObj, "chunks")) {
                readString(archiveObj, "group", entry.group);

                const auto& chunks = archiveObj["chunks"];
                entry.chunks.reserve(chunks.Size());

                for (const auto& chunkObj : chunks.GetArray()) {
                    if (!chunkObj.IsObject()) continue;

                    ChunkEntry chunk;
                    readInt(chunkObj, "part", chunk.part);
                    readString(chunkObj, "sha256", chunk.sha256);
                    readSize(chunkObj, "size", chunk.size);
                    readString(chunkObj, "url", chunk.url);
                    entry.chunks.push_back(std::move(chunk));
                }
            } else {
                // Single-file archive, the older format.
                readString(archiveObj, "sha256", entry.sha256);
                readSize(archiveObj, "size", entry.size);
                readString(archiveObj, "url", entry.url);
            }

            outManifest.archives.push_back(std::move(entry));
        }
    }

    if (hasArray(doc, "folders")) {
        const auto& folders = doc["folders"];
        outManifest.folders.reserve(folders.Size());

        for (const auto& folder : folders.GetArray()) {
            if (folder.IsString()) {
                outManifest.folders.push_back(folder.GetString());
            }
        }
    }

    if (doc.HasMember("optional_packs") && doc["optional_packs"].IsObject()) {
        const auto& packs = doc["optional_packs"];
        for (auto it = packs.MemberBegin(); it != packs.MemberEnd(); ++it) {
            const auto& packObj = it->value;
            if (!packObj.IsObject()) continue;

            OptionalPack pack;
            pack.id = it->name.GetString();
            readString(packObj, "path", pack.path);
            readString(packObj, "sha256", pack.sha256);
            readSize(packObj, "size", pack.size);
            readString(packObj, "url", pack.url);
            readString(packObj, "display_name", pack.displayName);

            outManifest.optionalPacks[pack.id] = std::move(pack);
        }
    }

    return true;
}

bool ManifestParser::parseLocalVersion(const std::string& json, LocalVersionInfo& outInfo) {
    rapidjson::Document doc;
    doc.Parse(json.c_str());

    if (doc.HasParseError()) {
        // A missing or empty file just means nothing is installed yet.
        outInfo.version = "";
        return true;
    }

    if (!doc.IsObject()) {
        return true;
    }

    readString(doc, "version", outInfo.version);

    if (doc.HasMember("installed_files") && doc["installed_files"].IsObject()) {
        const auto& files = doc["installed_files"];
        for (auto it = files.MemberBegin(); it != files.MemberEnd(); ++it) {
            if (it->value.IsString()) {
                outInfo.installedFiles[it->name.GetString()] = it->value.GetString();
            }
        }
    }

    return true;
}

std::string ManifestParser::generateLocalVersion(const LocalVersionInfo& info) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();

    doc.AddMember("version",
                  rapidjson::Value(info.version.c_str(), allocator),
                  allocator);

    rapidjson::Value filesObj(rapidjson::kObjectType);
    for (const auto& [path, hash] : info.installedFiles) {
        filesObj.AddMember(
            rapidjson::Value(path.c_str(), allocator),
            rapidjson::Value(hash.c_str(), allocator),
            allocator
        );
    }
    doc.AddMember("installed_files", filesObj, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

std::unordered_map<int, std::string> ManifestParser::parseChunkHashes(const std::string& json) {
    std::unordered_map<int, std::string> hashes;

    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("chunks") || !doc["chunks"].IsObject()) {
        return hashes;
    }

    const auto& chunks = doc["chunks"];
    for (auto it = chunks.MemberBegin(); it != chunks.MemberEnd(); ++it) {
        if (!it->value.IsString()) {
            continue;
        }

        const char* name = it->name.GetString();
        const char* nameEnd = name + it->name.GetStringLength();
        int part = 0;
        auto [end, ec] = std::from_chars(name, nameEnd, part);
        if (ec != std::errc() || end != nameEnd) {
            continue;
        }

        hashes[part] = it->value.GetString();
    }

    return hashes;
}

int ManifestParser::compareVersions(const std::string& a, const std::string& b) {
    auto parseVersion = [](const std::string& ver) -> std::tuple<int, int, int> {
        int major = 0, minor = 0, patch = 0;
        std::stringstream ss(ver);
        char dot;

        ss >> major;
        if (ss >> dot >> minor) {
            ss >> dot >> patch;
        }

        return {major, minor, patch};
    };

    auto [aMajor, aMinor, aPatch] = parseVersion(a);
    auto [bMajor, bMinor, bPatch] = parseVersion(b);

    if (aMajor != bMajor) return aMajor < bMajor ? -1 : 1;
    if (aMinor != bMinor) return aMinor < bMinor ? -1 : 1;
    if (aPatch != bPatch) return aPatch < bPatch ? -1 : 1;

    return 0;
}

const std::string& ManifestParser::getLastError() {
    return lastError_;
}

} // namespace awow::updater
