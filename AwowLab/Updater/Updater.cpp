#include "Updater.h"
#include "HttpDownloader.h"
#include "HashVerifier.h"
#include "FileReplacer.h"
#include "ReleaseChecks.h"
#include "UpdateSigningKey.h"
#include "ZipExtractor.h"
#include "PathValidator.h"
#include <rapidjson/document.h>
#include <algorithm>
#include <cctype>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace awow::updater {

namespace {

// The running updater can't overwrite itself, so a new copy is staged in
// updater/pending and AwowLab.exe swaps it in on its next launch.
constexpr const char* kUpdaterManifestPath = "updater/AwowLabUpdater.exe";

std::string toWindowsPath(std::string path) {
    std::replace(path.begin(), path.end(), '/', '\\');
    return path;
}

// A chunked archive's .archive_hashes file as part -> SHA256. Empty if the
// file is missing or unreadable, which re-downloads every chunk.
std::unordered_map<int, std::string> readChunkHashes(const std::string& hashFilePath) {
    std::ifstream hashFile(hashFilePath);
    if (!hashFile) {
        return {};
    }
    std::stringstream buffer;
    buffer << hashFile.rdbuf();
    return ManifestParser::parseChunkHashes(buffer.str());
}

void writeChunkHashes(const std::string& hashFilePath, const std::string& group,
                      const std::unordered_map<int, std::string>& hashes) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(hashFilePath).parent_path(), ec);

    std::ofstream hashOut(hashFilePath, std::ios::trunc);
    if (!hashOut) {
        return;
    }
    hashOut << "{\n  \"group\": \"" << group << "\",\n  \"chunks\": {\n";
    bool first = true;
    for (const auto& [part, hash] : hashes) {
        if (!first) hashOut << ",\n";
        hashOut << "    \"" << part << "\": \"" << hash << "\"";
        first = false;
    }
    hashOut << "\n  }\n}\n";
}

// A single-file archive's .archive_hash, or empty if there is none.
std::string readArchiveHash(const std::string& hashFilePath) {
    std::string hash;
    std::ifstream hashFile(hashFilePath);
    if (hashFile) {
        std::getline(hashFile, hash);
        while (!hash.empty() && (hash.back() == '\n' || hash.back() == '\r' || hash.back() == ' ')) {
            hash.pop_back();
        }
    }
    return hash;
}

std::vector<const ChunkEntry*> changedChunks(const ArchiveEntry& archive,
                                             const std::unordered_map<int, std::string>& installed) {
    std::vector<const ChunkEntry*> changed;
    for (const auto& chunk : archive.chunks) {
        auto it = installed.find(chunk.part);
        if (it == installed.end() || it->second != chunk.sha256) {
            changed.push_back(&chunk);
        }
    }
    return changed;
}

// Renames from onto to, falling back to copy-and-delete when a rename isn't
// possible (another volume, or the target is briefly locked).
bool moveIntoPlace(const std::string& from, const std::string& to, std::error_code& ec) {
    std::filesystem::rename(from, to, ec);
    if (ec) {
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(from);
    }
    return !ec;
}

// remove_all only ever runs on these known archive destinations.
bool isReplaceableArchiveDir(const std::string& extractTo) {
    static const std::vector<std::string> knownDirs = {
        "PNG/Spell Icons", "PNG/Worldmaps", "PNG\\Spell Icons", "PNG\\Worldmaps",
        "PNG/Worldmaps/defaults", "PNG\\Worldmaps\\defaults",
        "PNG/EncounterMaps/defaults", "PNG\\EncounterMaps\\defaults",
        "shaders", "data", "PNG/EncounterMaps", "PNG\\EncounterMaps"
    };
    return std::find(knownDirs.begin(), knownDirs.end(), extractTo) != knownDirs.end();
}

} // namespace

Updater::Updater() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::filesystem::path exePath(path);
    std::filesystem::path parentDir = exePath.parent_path();

    // An existing install keeps the updater in AwowLab/updater/. A fresh
    // download runs it standalone; main() asks where to install before
    // anything is written, and only an install already beside it is used as is.
    if (parentDir.filename() == "updater") {
        appDir_ = parentDir.parent_path().string();
        runsFromInstall_ = true;
    } else {
        appDir_ = parentDir.string();
    }
}

void Updater::setRepository(const std::string& owner, const std::string& repo) {
    repoOwner_ = owner;
    repoName_ = repo;
}

void Updater::setAppDirectory(const std::string& dir) {
    appDir_ = dir;
}

bool Updater::loadLocalVersion() {
    std::string versionPath = appDir_ + "/version.json";

    std::ifstream file(versionPath);
    if (!file) {
        localVersion_.version = "";
        return true;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return ManifestParser::parseLocalVersion(buffer.str(), localVersion_);
}

bool Updater::saveLocalVersion() {
    std::string versionPath = appDir_ + "/version.json";
    std::string content = ManifestParser::generateLocalVersion(localVersion_);

    std::ofstream file(versionPath);
    if (!file) {
        lastError_ = "Failed to write version.json";
        return false;
    }

    file << content;
    return true;
}

bool Updater::fetchRemoteVersion() {
    std::string apiUrl = "https://api.github.com/repos/" + repoOwner_ + "/" + repoName_ + "/releases/latest";

    HttpDownloader downloader;
    std::string response;

    if (!downloader.downloadToString(apiUrl, response)) {
        lastError_ = "Failed to check for updates: " + downloader.getLastError();
        return false;
    }

    rapidjson::Document doc;
    doc.Parse(response.c_str());

    if (doc.HasParseError() || !doc.IsObject()) {
        lastError_ = "Failed to parse GitHub API response";
        return false;
    }

    if (!doc.HasMember("tag_name") || !doc["tag_name"].IsString()) {
        lastError_ = "No tag_name in release";
        return false;
    }

    std::string tagName = doc["tag_name"].GetString();
    remoteVersion_.version = (!tagName.empty() && tagName[0] == 'v') ? tagName.substr(1) : tagName;

    if (doc.HasMember("assets") && doc["assets"].IsArray()) {
        for (const auto& asset : doc["assets"].GetArray()) {
            if (!asset.HasMember("name") || !asset["name"].IsString() ||
                std::string(asset["name"].GetString()) != "version.json" ||
                !asset.HasMember("browser_download_url") ||
                !asset["browser_download_url"].IsString()) {
                continue;
            }

            std::string versionUrl = asset["browser_download_url"].GetString();
            std::string versionContent;
            if (downloader.downloadToString(versionUrl, versionContent)) {
                ManifestParser::parseVersionInfo(versionContent, remoteVersion_);
            }
        }
    }

    if (remoteVersion_.manifestUrl.empty()) {
        remoteVersion_.manifestUrl = "https://github.com/" + repoOwner_ + "/" + repoName_ +
                                      "/releases/download/" + tagName + "/manifest.json";
    }

    if (remoteVersion_.changelogUrl.empty()) {
        remoteVersion_.changelogUrl = "https://github.com/" + repoOwner_ + "/" + repoName_ +
                                       "/releases/tag/" + tagName;
    }

    return true;
}

bool Updater::fetchManifest() {
    if (remoteVersion_.manifestUrl.empty()) {
        lastError_ = "No manifest URL available";
        return false;
    }

    // version.json is not signed, so its manifest_url could point anywhere.
    // Only fetch from this repository's own release assets.
    if (!ReleaseChecks::isReleaseAssetUrl(remoteVersion_.manifestUrl, repoOwner_, repoName_)) {
        lastError_ = "Manifest URL is not a release of " + repoOwner_ + "/" + repoName_ +
                     ": " + remoteVersion_.manifestUrl;
        return false;
    }

    HttpDownloader downloader;
    std::string manifestContent;

    if (!downloader.downloadToString(remoteVersion_.manifestUrl, manifestContent)) {
        lastError_ = "Failed to download manifest: " + downloader.getLastError();
        return false;
    }

    // Check the release signature before trusting anything in the manifest.
    // Every file is hash-checked against the manifest, so this one check
    // covers the whole release. A release without a .sig is refused: a
    // signature that can simply be left off protects nothing.
    if (kSigningKeyConfigured) {
        std::string signatureContent;
        HttpDownloader sigDownloader;
        bool haveSignature = sigDownloader.downloadToString(
            remoteVersion_.manifestUrl + ".sig", signatureContent);

        std::string signatureError;
        if (!ReleaseChecks::checkManifestSignature(
                manifestContent,
                haveSignature ? &signatureContent : nullptr,
                kReleasePublicKey,
                signatureError)) {
            lastError_ = signatureError;
            if (!haveSignature) {
                lastError_ += ": " + sigDownloader.getLastError();
            }
            return false;
        }
    }

    if (!ManifestParser::parseManifest(manifestContent, manifest_)) {
        lastError_ = "Failed to parse manifest: " + ManifestParser::getLastError();
        return false;
    }

    // The earlier check used the tag name, which is not signed. Check the
    // version in the signed manifest too, so an old release can't be
    // replayed under a newer tag.
    if (isBlockedDowngrade(manifest_.version)) {
        return false;
    }

    manifestLoaded_ = true;
    return true;
}

std::string Updater::getInstalledVersion() const {
    return localVersion_.version;
}

bool Updater::isBlockedDowngrade(const std::string& availableVersion) {
    // Hash comparison alone would happily "update" to an older release if
    // someone replays an old manifest, so refuse to move backwards unless
    // support explicitly asked for it (--allow-downgrade).
    if (allowDowngrade_) {
        return false;
    }

    std::string error = ReleaseChecks::downgradeError(availableVersion, localVersion_.version);
    if (!error.empty()) {
        lastError_ = error;
        return true;
    }
    return false;
}

std::optional<std::string> Updater::checkForUpdate() {
    if (!loadLocalVersion()) {
        return std::nullopt;
    }

    if (!fetchRemoteVersion()) {
        return std::nullopt;
    }

    if (isBlockedDowngrade(remoteVersion_.version)) {
        return std::nullopt;
    }

    if (!fetchManifest()) {
        return std::nullopt;
    }

    for (const auto& file : manifest_.files) {
        std::string localHash = calculateLocalFileHash(file.path);
        if (localHash.empty() || localHash != file.sha256) {
            return remoteVersion_.version;
        }
    }

    for (const auto& archive : manifest_.archives) {
        std::string archiveDir = toWindowsPath(appDir_ + "/" + archive.extractTo);

        if (archive.isChunked()) {
            auto installed = readChunkHashes(archiveDir + "\\.archive_hashes");
            if (!changedChunks(archive, installed).empty()) {
                return remoteVersion_.version;
            }
        } else {
            std::string localHash = readArchiveHash(archiveDir + "\\.archive_hash");
            if (localHash.empty() || localHash != archive.sha256) {
                return remoteVersion_.version;
            }
        }
    }

    return std::nullopt;
}

std::string Updater::calculateLocalFileHash(const std::string& relativePath) {
    std::string fullPath = toWindowsPath(appDir_ + "/" + relativePath);

    if (!std::filesystem::exists(fullPath)) {
        return "";
    }

    return HashVerifier::hashFile(fullPath);
}

UpdateResult Updater::performUpdate(UpdateProgressCallback progress) {
    UpdateResult result;

    if (remoteVersion_.manifestUrl.empty()) {
        if (!fetchRemoteVersion()) {
            result.errorMessage = lastError_;
            return result;
        }
    }

    // Fresh installs have no local version, so this only matters when an
    // old release is replayed.
    loadLocalVersion();
    if (isBlockedDowngrade(remoteVersion_.version)) {
        result.errorMessage = lastError_;
        return result;
    }

    if (!manifestLoaded_ && !fetchManifest()) {
        result.errorMessage = lastError_;
        return result;
    }

    if (checkAndPerformSelfUpdate(progress, result)) {
        return result;
    }

    if (progress) {
        progress("Analyzing files...", 0.0f);
    }

    createManifestFolders();

    HttpDownloader downloader;
    installFiles(downloader, progress, result);

    // Migrate legacy installs: if .archive_hash exists in root folders,
    // delete stale v2 assets so archives re-extract to defaults/
    migrateToDefaultsLayout(progress);

    for (const auto& archive : manifest_.archives) {
        std::string extractDir = PathValidator::validateAndCanonicalize(appDir_, archive.extractTo);
        if (extractDir.empty()) {
            result.filesFailed++;
            result.errorMessage = "Security: Invalid archive path: " + archive.extractTo;
            continue;
        }

        extractDir = toWindowsPath(extractDir);

        if (archive.isChunked()) {
            installChunkedArchive(archive, extractDir, downloader, progress, result);
        } else {
            installSingleArchive(archive, extractDir, downloader, progress, result);
        }
    }

    localVersion_.version = manifest_.version;
    saveLocalVersion();

    result.success = (result.filesFailed == 0);

    if (progress) {
        if (result.filesUpdated == 0 && result.filesFailed == 0) {
            progress("Already up to date!", 1.0f);
        } else if (result.success) {
            progress("Update complete!", 1.0f);
        } else {
            progress("Update completed with errors", 1.0f);
        }
    }

    return result;
}

void Updater::createManifestFolders() {
    for (const auto& folder : manifest_.folders) {
        if (!PathValidator::isPathSafe(folder)) {
            continue;
        }

        std::string folderPath = PathValidator::validateAndCanonicalize(appDir_, folder);
        if (folderPath.empty()) {
            continue;
        }

        folderPath = toWindowsPath(folderPath);

        // Best effort; a missing folder is created again when a file lands in it.
        std::error_code ec;
        if (!std::filesystem::exists(folderPath)) {
            std::filesystem::create_directories(folderPath, ec);
        }
    }
}

void Updater::installFiles(HttpDownloader& downloader,
                           const UpdateProgressCallback& progress,
                           UpdateResult& result) {
    std::vector<const FileEntry*> filesToUpdate;

    for (const auto& file : manifest_.files) {
        std::string localHash = calculateLocalFileHash(file.path);

        if (!localHash.empty() && localHash == file.sha256) {
            result.filesSkipped++;
            continue;
        }

        filesToUpdate.push_back(&file);
    }

    int totalFiles = static_cast<int>(filesToUpdate.size());
    int currentFile = 0;

    for (const auto* file : filesToUpdate) {
        currentFile++;

        // validateAndCanonicalize rejects paths that escape appDir_.
        std::string targetPath = PathValidator::validateAndCanonicalize(appDir_, file->path);
        if (targetPath.empty()) {
            result.filesFailed++;
            result.errorMessage = "Security: Invalid path in manifest: " + file->path;
            continue;
        }

        if (progress) {
            std::string status = "Downloading " + file->path + " (" +
                                 std::to_string(currentFile) + "/" + std::to_string(totalFiles) + ")";
            float overallProgress = static_cast<float>(currentFile - 1) / totalFiles;
            progress(status, overallProgress);
        }

        std::string tempPath = toWindowsPath(FileReplacer::getTempPath(targetPath));
        targetPath = toWindowsPath(targetPath);

        bool isUpdaterFile = (file->path == kUpdaterManifestPath);
        std::string pendingPath;
        if (isUpdaterFile) {
            pendingPath = appDir_ + "\\updater\\pending\\AwowLabUpdater.exe";
        }

        std::string dirToCreate = isUpdaterFile ? pendingPath : targetPath;
        if (!FileReplacer::ensureDirectoryExists(dirToCreate)) {
            result.filesFailed++;
            result.errorMessage = "Failed to create directory for " + file->path + ": " + FileReplacer::getLastError();
            continue;
        }

        bool downloadSuccess = downloader.downloadToFile(file->url, tempPath,
            [&progress, currentFile, totalFiles, &file](size_t received, size_t total) {
                if (progress && total > 0) {
                    float fileProgress = static_cast<float>(received) / total;
                    float overallProgress = (static_cast<float>(currentFile - 1) + fileProgress) / totalFiles;
                    std::string status = "Downloading " + file->path + "...";
                    progress(status, overallProgress);
                }
            }
        );

        if (!downloadSuccess) {
            result.filesFailed++;
            result.errorMessage = "Failed to download " + file->path + ": " + downloader.getLastError();
            std::filesystem::remove(tempPath);
            continue;
        }

        if (isUpdaterFile) {
            if (HashVerifier::hashFile(tempPath) != file->sha256) {
                result.filesFailed++;
                result.errorMessage = "Hash mismatch for " + file->path;
                std::filesystem::remove(tempPath);
                continue;
            }
            std::error_code ec;
            if (!moveIntoPlace(tempPath, pendingPath, ec)) {
                result.filesFailed++;
                result.errorMessage = "Failed to stage updater: " + ec.message();
                continue;
            }
        } else if (!FileReplacer::atomicReplace(targetPath, tempPath, file->sha256)) {
            result.filesFailed++;
            result.errorMessage = "Failed to install " + file->path + ": " + FileReplacer::getLastError();
            continue;
        }

        localVersion_.installedFiles[file->path] = file->sha256;
        result.filesUpdated++;
    }
}

void Updater::installChunkedArchive(const ArchiveEntry& archive,
                                    const std::string& extractDir,
                                    HttpDownloader& downloader,
                                    const UpdateProgressCallback& progress,
                                    UpdateResult& result) {
    std::string hashFilePath = extractDir + "\\.archive_hashes";
    auto installedHashes = readChunkHashes(hashFilePath);

    std::vector<const ChunkEntry*> chunksToDownload = changedChunks(archive, installedHashes);
    if (chunksToDownload.empty()) {
        result.filesSkipped++;
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(extractDir, ec);

    int totalChunks = static_cast<int>(chunksToDownload.size());
    int currentChunk = 0;

    for (const auto* chunk : chunksToDownload) {
        currentChunk++;

        if (progress) {
            std::string status = "Downloading " + archive.group + " chunk " +
                                 std::to_string(currentChunk) + "/" + std::to_string(totalChunks);
            float chunkProgress = 0.8f + (static_cast<float>(currentChunk - 1) / totalChunks) * 0.15f;
            progress(status, chunkProgress);
        }

        std::string tempZipPath = appDir_ + "\\temp_chunk.zip";

        bool downloadSuccess = downloader.downloadToFile(chunk->url, tempZipPath,
            [&progress, &archive, currentChunk, totalChunks](size_t received, size_t total) {
                if (progress && total > 0) {
                    float fileProgress = static_cast<float>(received) / total;
                    float baseProgress = 0.8f + (static_cast<float>(currentChunk - 1) / totalChunks) * 0.15f;
                    float chunkRange = 0.15f / totalChunks;
                    progress("Downloading " + archive.group + "...", baseProgress + fileProgress * chunkRange * 0.7f);
                }
            }
        );

        if (!downloadSuccess) {
            result.filesFailed++;
            result.errorMessage = "Failed to download " + archive.group + " chunk " + std::to_string(chunk->part);
            std::filesystem::remove(tempZipPath);
            continue;
        }

        if (HashVerifier::hashFile(tempZipPath) != chunk->sha256) {
            result.filesFailed++;
            result.errorMessage = "Hash mismatch for " + archive.group + " chunk " + std::to_string(chunk->part);
            std::filesystem::remove(tempZipPath);
            continue;
        }

        bool extractSuccess = ZipExtractor::extract(tempZipPath, extractDir,
            [&progress, &archive, currentChunk, totalChunks](const std::string& /*currentFile*/, int fileIndex, int totalFiles) {
                if (progress && totalFiles > 0) {
                    float extractProgress = static_cast<float>(fileIndex) / totalFiles;
                    progress("Extracting " + archive.group + " chunk " + std::to_string(currentChunk) + "/" + std::to_string(totalChunks) +
                             " (" + std::to_string(fileIndex) + "/" + std::to_string(totalFiles) + ")",
                             0.85f + extractProgress * 0.1f);
                }
            }
        );

        if (!extractSuccess) {
            result.filesFailed++;
            result.errorMessage = "Failed to extract " + archive.group + " chunk: " + ZipExtractor::getLastError();
            std::filesystem::remove(tempZipPath);
            continue;
        }

        // Saved after every chunk so an interrupted update resumes where it stopped.
        installedHashes[chunk->part] = chunk->sha256;
        writeChunkHashes(hashFilePath, archive.group, installedHashes);

        std::filesystem::remove(tempZipPath);
    }

    result.filesUpdated++;
}

void Updater::installSingleArchive(const ArchiveEntry& archive,
                                   const std::string& extractDir,
                                   HttpDownloader& downloader,
                                   const UpdateProgressCallback& progress,
                                   UpdateResult& result) {
    std::string hashFilePath = extractDir + "\\.archive_hash";

    std::string localHash = readArchiveHash(hashFilePath);
    if (!localHash.empty() && localHash == archive.sha256) {
        result.filesSkipped++;
        return;
    }

    if (progress) {
        progress("Downloading " + archive.extractTo + "...", 0.8f);
    }

    std::string tempZipPath = appDir_ + "\\temp_archive.zip";

    bool downloadSuccess = downloader.downloadToFile(archive.url, tempZipPath,
        [&progress, &archive](size_t received, size_t total) {
            if (progress && total > 0) {
                float fileProgress = static_cast<float>(received) / total;
                progress("Downloading " + archive.extractTo + "...", 0.8f + fileProgress * 0.1f);
            }
        }
    );

    if (!downloadSuccess) {
        result.filesFailed++;
        result.errorMessage = "Failed to download " + archive.extractTo + ": " + downloader.getLastError();
        std::filesystem::remove(tempZipPath);
        return;
    }

    if (HashVerifier::hashFile(tempZipPath) != archive.sha256) {
        result.filesFailed++;
        result.errorMessage = "Hash mismatch for " + archive.extractTo;
        std::filesystem::remove(tempZipPath);
        return;
    }

    if (progress) {
        progress("Extracting " + archive.extractTo + "...", 0.9f);
    }

    std::error_code ec;
    if (isReplaceableArchiveDir(archive.extractTo) && std::filesystem::exists(extractDir)) {
        std::filesystem::remove_all(extractDir, ec);
    }

    bool extractSuccess = ZipExtractor::extract(tempZipPath, extractDir,
        [&progress, &archive](const std::string& /*currentFile*/, int fileIndex, int totalFiles) {
            if (progress && totalFiles > 0) {
                float extractProgress = static_cast<float>(fileIndex) / totalFiles;
                progress("Extracting " + archive.extractTo + "... (" +
                         std::to_string(fileIndex) + "/" + std::to_string(totalFiles) + ")",
                         0.9f + extractProgress * 0.05f);
            }
        }
    );

    if (!extractSuccess) {
        result.filesFailed++;
        result.errorMessage = "Failed to extract " + archive.extractTo + ": " + ZipExtractor::getLastError();
        std::filesystem::remove(tempZipPath);
        return;
    }

    std::filesystem::create_directories(std::filesystem::path(hashFilePath).parent_path(), ec);
    std::ofstream hashOut(hashFilePath, std::ios::trunc);
    if (hashOut) {
        hashOut << archive.sha256;
        hashOut.close();
    }

    std::filesystem::remove(tempZipPath);
    result.filesUpdated++;
}

UpdateResult Updater::downloadOptionalPack(const std::string& packId,
                                            UpdateProgressCallback progress) {
    UpdateResult result;

    if (remoteVersion_.manifestUrl.empty() && !fetchRemoteVersion()) {
        result.errorMessage = lastError_;
        return result;
    }

    if (!manifestLoaded_ && !fetchManifest()) {
        result.errorMessage = lastError_;
        return result;
    }

    auto it = manifest_.optionalPacks.find(packId);
    if (it == manifest_.optionalPacks.end()) {
        result.errorMessage = "Optional pack not found: " + packId;
        return result;
    }

    const OptionalPack& pack = it->second;

    if (progress) {
        progress("Downloading " + pack.displayName + "...", 0.0f);
    }

    std::string targetPath = PathValidator::validateAndCanonicalize(appDir_, pack.path);
    if (targetPath.empty()) {
        result.errorMessage = "Security: Invalid path in manifest: " + pack.path;
        return result;
    }

    std::string tempPath = toWindowsPath(FileReplacer::getTempPath(targetPath));
    targetPath = toWindowsPath(targetPath);

    FileReplacer::ensureDirectoryExists(targetPath);

    HttpDownloader downloader;
    bool downloadSuccess = downloader.downloadToFile(pack.url, tempPath,
        [&progress, &pack](size_t received, size_t total) {
            if (progress && total > 0) {
                float fileProgress = static_cast<float>(received) / total;
                progress("Downloading " + pack.displayName + "...", fileProgress);
            }
        }
    );

    if (!downloadSuccess) {
        result.errorMessage = "Download failed: " + downloader.getLastError();
        std::filesystem::remove(tempPath);
        return result;
    }

    if (!FileReplacer::atomicReplace(targetPath, tempPath, pack.sha256)) {
        result.errorMessage = "Installation failed: " + FileReplacer::getLastError();
        return result;
    }

    result.success = true;
    result.filesUpdated = 1;

    if (progress) {
        progress("Download complete!", 1.0f);
    }

    return result;
}

std::vector<OptionalPack> Updater::getAvailableOptionalPacks() const {
    std::vector<OptionalPack> packs;
    for (const auto& [id, pack] : manifest_.optionalPacks) {
        packs.push_back(pack);
    }
    return packs;
}

bool Updater::launchMainApp() {
    std::string appPath = appDir_ + "\\AwowLab.exe";

    if (!std::filesystem::exists(appPath)) {
        lastError_ = "AwowLab.exe not found at: " + appPath;
        return false;
    }

    // Removes the current directory from the DLL search path. The folder the
    // exe was loaded from is still searched first.
    SetDllDirectoryW(L"");

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    std::string cmdLine = "\"" + appPath + "\"";

    if (!CreateProcessA(
        nullptr,
        cmdLine.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        appDir_.c_str(),
        &si,
        &pi
    )) {
        lastError_ = "Failed to launch AwowLab.exe";
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return true;
}

bool Updater::checkAndPerformSelfUpdate(UpdateProgressCallback progress, UpdateResult& result) {
    // updater_version 0 (or absent) means no self-update is required.
    if (manifest_.updaterVersion <= UPDATER_VERSION) {
        return false;
    }

    auto updaterEntry = std::find_if(manifest_.files.begin(), manifest_.files.end(),
        [](const FileEntry& file) { return file.path == kUpdaterManifestPath; });
    if (updaterEntry == manifest_.files.end()) {
        return false;
    }

    if (progress) {
        progress("Updating updater...", 0.1f);
    }

    std::string pendingDir = appDir_ + "\\updater\\pending";
    std::string pendingPath = pendingDir + "\\AwowLabUpdater.exe";
    std::string tempPath = appDir_ + "\\temp_updater.exe";

    std::error_code ec;
    std::filesystem::create_directories(pendingDir, ec);

    HttpDownloader downloader;
    bool downloadSuccess = downloader.downloadToFile(updaterEntry->url, tempPath,
        [&progress](size_t received, size_t total) {
            if (progress && total > 0) {
                float fileProgress = static_cast<float>(received) / total;
                progress("Downloading updater...", 0.1f + fileProgress * 0.3f);
            }
        }
    );

    if (!downloadSuccess) {
        // Carry on with the normal update; the old updater can still install
        // content that isn't chunked.
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    if (HashVerifier::hashFile(tempPath) != updaterEntry->sha256) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    if (!moveIntoPlace(tempPath, pendingPath, ec)) {
        return false;
    }

    result.needsRestart = true;
    result.success = true;
    result.filesUpdated = 1;
    result.errorMessage = "Updater has been updated. Please close and check for updates again.";

    return true;
}

void Updater::migrateToDefaultsLayout(UpdateProgressCallback progress) {
    // Older installs extracted map assets straight into PNG/Worldmaps and
    // PNG/EncounterMaps, marked by an .archive_hash there. For those, delete the
    // loose .png files and the hash so the archive re-extracts into defaults/.
    // Files the user adds later are kept, because the hash is gone by then.

    const std::vector<std::string> dirsToMigrate = {
        "PNG\\Worldmaps",
        "PNG\\EncounterMaps"
    };

    if (progress) {
        progress("Checking for legacy asset layout...", 0.95f);
    }

    int filesRemoved = 0;

    for (const auto& relPath : dirsToMigrate) {
        std::string rootDir = appDir_ + "\\" + relPath;
        std::string hashFilePath = rootDir + "\\.archive_hash";

        // No hash file means a fresh install or one already migrated.
        if (!std::filesystem::exists(hashFilePath)) {
            continue;
        }

        if (progress) {
            progress("Migrating " + relPath + " to defaults layout...", 0.95f);
        }

        std::error_code ec;

        for (const auto& entry : std::filesystem::directory_iterator(rootDir, ec)) {
            if (ec) break;

            if (entry.is_directory()) {
                continue;
            }

            std::string filename = entry.path().filename().string();
            if (filename.length() < 4) {
                continue;
            }
            std::string ext = filename.substr(filename.length() - 4);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".png") {
                continue;
            }

            std::filesystem::remove(entry.path(), ec);
            if (!ec) {
                filesRemoved++;
            }
        }

        // Retried in case something briefly holds the file open.
        std::error_code hashEc;
        for (int attempt = 0; attempt < 3; ++attempt) {
            hashEc.clear();
            std::filesystem::remove(hashFilePath, hashEc);
            if (!std::filesystem::exists(hashFilePath)) {
                break;
            }
            Sleep(100);
        }
    }

    if (progress && filesRemoved > 0) {
        progress("Cleaned up " + std::to_string(filesRemoved) + " legacy files", 0.96f);
    }
}

} // namespace awow::updater
