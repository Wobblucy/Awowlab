#pragma once

#include "ManifestParser.h"
#include "AwowLabBuildOptions.h"
#include <string>
#include <vector>
#include <optional>
#include <functional>

namespace awow::updater {

class HttpDownloader;

struct UpdateResult {
    bool success = false;
    std::string errorMessage;
    int filesUpdated = 0;
    int filesSkipped = 0;       // already up to date
    int filesFailed = 0;
    bool needsRestart = false;  // the updater replaced itself; run it again to continue
};

// overallProgress runs from 0.0 to 1.0.
using UpdateProgressCallback = std::function<void(const std::string& status, float overallProgress)>;

// Finds the latest GitHub release, verifies its manifest, and downloads only
// the files whose hashes differ from what is installed.
class Updater {
public:
    // Bump when the updater gains something a release depends on. A manifest
    // whose updater_version is higher makes this updater fetch a newer copy
    // of itself first. 2 added the defaults/ migration.
    static constexpr int UPDATER_VERSION = 2;

    Updater();
    ~Updater() = default;

    void setRepository(const std::string& owner, const std::string& repo);

    // Where AwowLab.exe lives. Worked out from the updater's own location
    // by default: the parent of updater\ for the installed copy, otherwise
    // the folder the updater was run from. A fresh install sets it to the
    // folder the user picked.
    void setAppDirectory(const std::string& dir);
    const std::string& getAppDirectory() const { return appDir_; }

    // True when this is the copy inside an install's updater\ folder,
    // which is how the app launches it.
    bool runsFromInstall() const { return runsFromInstall_; }

    // The new version, or nullopt when everything is current (or on error).
    std::optional<std::string> checkForUpdate();

    // Permit installing a release older than the current one. Off by
    // default; meant for support and recovery.
    void setAllowDowngrade(bool allow) { allowDowngrade_ = allow; }

    std::string getInstalledVersion() const;

    UpdateResult performUpdate(UpdateProgressCallback progress = nullptr);

    UpdateResult downloadOptionalPack(const std::string& packId,
                                       UpdateProgressCallback progress = nullptr);

    // Empty until checkForUpdate() has loaded the manifest.
    std::vector<OptionalPack> getAvailableOptionalPacks() const;

    bool launchMainApp();

    const std::string& getLastError() const { return lastError_; }

private:
    bool loadLocalVersion();
    bool saveLocalVersion();
    bool fetchRemoteVersion();
    bool fetchManifest();
    bool isBlockedDowngrade(const std::string& availableVersion);
    std::string calculateLocalFileHash(const std::string& relativePath);

    // True if a newer updater was staged and this run should stop there.
    bool checkAndPerformSelfUpdate(UpdateProgressCallback progress, UpdateResult& result);

    // The steps of performUpdate, in the order it runs them.
    void createManifestFolders();
    void installFiles(HttpDownloader& downloader,
                      const UpdateProgressCallback& progress,
                      UpdateResult& result);
    // Downloads only the chunks whose hash changed.
    void installChunkedArchive(const ArchiveEntry& archive,
                               const std::string& extractDir,
                               HttpDownloader& downloader,
                               const UpdateProgressCallback& progress,
                               UpdateResult& result);
    void installSingleArchive(const ArchiveEntry& archive,
                              const std::string& extractDir,
                              HttpDownloader& downloader,
                              const UpdateProgressCallback& progress,
                              UpdateResult& result);

    // Clears map assets left in the folder roots by installs from before
    // the defaults/ layout (0.22.0).
    void migrateToDefaultsLayout(UpdateProgressCallback progress);

    std::string repoOwner_ = AWOWLAB_UPDATE_REPO_OWNER;
    std::string repoName_ = AWOWLAB_UPDATE_REPO_NAME;
    std::string appDir_;
    bool runsFromInstall_ = false;
    std::string lastError_;

    LocalVersionInfo localVersion_;
    VersionInfo remoteVersion_;
    Manifest manifest_;
    bool manifestLoaded_ = false;
    bool allowDowngrade_ = false;
};

} // namespace awow::updater
