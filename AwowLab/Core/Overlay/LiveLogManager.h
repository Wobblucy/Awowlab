#pragma once
#include "ActiveLogWatcher.h"
#include "LiveLogSession.h"
#include <filesystem>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>

// Runs live log monitoring on a background thread: ActiveLogWatcher reports
// writes to the combat log, LiveLogSession parses the new data, and the data
// update callback fires afterwards.
//
//   LiveLogManager manager;
//   manager.setOnDataUpdate([&]() { refreshUI(); });
//   manager.start("C:/.../_retail_/Logs");
//
//   // Later...
//   manager.stop();
//
class LiveLogManager {
public:
    using DataUpdateCallback = std::function<void()>;
    using PullChangeCallback = std::function<void(const PullSegment&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    LiveLogManager();
    ~LiveLogManager();

    LiveLogManager(const LiveLogManager&) = delete;
    LiveLogManager& operator=(const LiveLogManager&) = delete;

    // Start live logging mode by watching a WoW Logs folder
    // Returns true if started successfully
    bool start(const std::filesystem::path& logsFolder);

    // Stop live logging and clean up
    void stop();

    // Check if currently running
    bool isRunning() const { return running_.load(); }

    // Get the logs folder being watched
    const std::filesystem::path& getLogsFolder() const { return logsFolder_; }

    // Get the currently active log file (if any)
    std::optional<std::filesystem::path> getActiveLogPath() const;

    // Get seconds since last write (for staleness indicator)
    std::chrono::seconds getSecondsSinceLastWrite() const;

    // Get the live log session (for data queries)
    const LiveLogSession* getSession() const { return session_.get(); }
    LiveLogSession* getSession() { return session_.get(); }

    void setOnDataUpdate(DataUpdateCallback callback) { onDataUpdate_ = std::move(callback); }
    void setOnPullStart(PullChangeCallback callback) { onPullStart_ = std::move(callback); }
    void setOnPullEnd(PullChangeCallback callback) { onPullEnd_ = std::move(callback); }
    void setOnError(ErrorCallback callback) { onError_ = std::move(callback); }

    // Set polling interval (default 2 seconds)
    void setPollInterval(std::chrono::milliseconds interval) { pollInterval_ = interval; }

    // Force an immediate poll (useful after UI refresh button)
    void pollNow();

private:
    std::filesystem::path logsFolder_;
    ActiveLogWatcher watcher_;
    std::unique_ptr<LiveLogSession> session_;

    std::thread pollThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> pollRequested_{false};

    // Set by start() when an existing log needs its initial segment scan.
    // pollLoop runs the scan, so the UI thread can draw the spinner.
    std::atomic<bool> pendingInitialScan_{false};

    std::chrono::milliseconds pollInterval_{2000};

    DataUpdateCallback onDataUpdate_;
    PullChangeCallback onPullStart_;
    PullChangeCallback onPullEnd_;
    ErrorCallback onError_;

    // Poll loop running in background thread
    void pollLoop();

    // Handle when the active log file changes (new day = new file)
    void handleLogFileChange(const std::filesystem::path& newLogPath);
};
