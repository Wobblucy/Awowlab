#include "LiveLogManager.h"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

LiveLogManager::LiveLogManager() = default;

LiveLogManager::~LiveLogManager() {
    stop();
}

bool LiveLogManager::start(const std::filesystem::path& logsFolder) {
    if (running_.load()) {
        return false;  // Already running
    }

    // Verify folder exists
    if (!std::filesystem::exists(logsFolder) || !std::filesystem::is_directory(logsFolder)) {
        if (onError_) {
            onError_("Logs folder does not exist: " + logsFolder.string());
        }
        return false;
    }

    logsFolder_ = logsFolder;
    stopRequested_.store(false);
    pollRequested_.store(false);

    // Create session
    session_ = std::make_unique<LiveLogSession>();

    // Wire up session callbacks
    session_->setOnPullStart([this](const PullSegment& pull) {
        if (onPullStart_) onPullStart_(pull);
    });

    session_->setOnPullEnd([this](const PullSegment& pull) {
        if (onPullEnd_) onPullEnd_(pull);
    });

    session_->setOnDataUpdate([this]() {
        if (onDataUpdate_) onDataUpdate_();
    });

    // Start watching the folder
    if (!watcher_.start(logsFolder)) {
        if (onError_) {
            onError_("Failed to start watching folder: " + logsFolder.string());
        }
        session_.reset();
        return false;
    }

    // Attach without the initial segment scan. The scan reads the whole log
    // and takes seconds on a large raid log, which here would hold up the
    // render loop's start. The poll thread runs it on its first iteration
    // instead, at its lower priority and with the spinner showing.
    auto activeLog = watcher_.getActiveLogPath();
    if (activeLog) {
        if (!session_->attach(*activeLog, /*scanExisting=*/false)) {
            // Not fatal; attach happens when a log file appears
        } else {
            pendingInitialScan_.store(true);
        }
    }

    // Start the poll thread
    running_.store(true);
    pollThread_ = std::thread(&LiveLogManager::pollLoop, this);

    return true;
}

void LiveLogManager::stop() {
    if (!running_.load()) {
        return;
    }

    stopRequested_.store(true);

    // Wait for poll thread to finish
    if (pollThread_.joinable()) {
        pollThread_.join();
    }

    running_.store(false);

    // Stop the watcher
    watcher_.stop();

    // Detach session
    if (session_) {
        session_->detach();
    }
}

std::optional<std::filesystem::path> LiveLogManager::getActiveLogPath() const {
    return watcher_.getActiveLogPath();
}

std::chrono::seconds LiveLogManager::getSecondsSinceLastWrite() const {
    return watcher_.getSecondsSinceLastWrite();
}

void LiveLogManager::pollNow() {
    pollRequested_.store(true);
}

void LiveLogManager::pollLoop() {
    // Below normal priority so WoW gets the CPU first while a large batch
    // from a busy fight is being parsed. One step down still keeps up with
    // the log.
#ifdef _WIN32
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif

    std::filesystem::path lastLogPath;

    // The initial scan deferred by start(). lastLogPath is set so the
    // rotation check below does not treat the same file as new.
    if (pendingInitialScan_.exchange(false) && session_ && session_->isAttached()) {
        lastLogPath = session_->getLogPath();
        session_->scanForSegments();
    }

    while (!stopRequested_.load()) {
        // Check if the active log file has changed (new day = new file)
        auto currentLog = watcher_.getActiveLogPath();
        if (currentLog && *currentLog != lastLogPath) {
            handleLogFileChange(*currentLog);
            lastLogPath = *currentLog;
        }

        // Poll every interval, not only when the watcher reports a write:
        // ReadDirectoryChangesW coalesces and can drop notifications under
        // load. poll() returns at once when the file has not grown, so this
        // costs nothing when idle. The watcher signal only wakes the loop
        // early; hasNewWrite() is still consumed so that keeps working.
        watcher_.hasNewWrite();
        pollRequested_.exchange(false);

        if (session_ && session_->isAttached()) {
            session_->poll();
        }

        // Short sleeps so the stop flag is noticed quickly
        auto sleepEnd = std::chrono::steady_clock::now() + pollInterval_;
        while (std::chrono::steady_clock::now() < sleepEnd && !stopRequested_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Check for immediate poll request
            if (pollRequested_.load()) {
                break;
            }
        }
    }
}

void LiveLogManager::handleLogFileChange(const std::filesystem::path& newLogPath) {
    if (!session_) return;

    // A new file starts a fresh session: attach() clears the pull history,
    // even if the log rotated in the middle of a key.

    std::filesystem::path currentPath = session_->getLogPath();

    if (currentPath != newLogPath) {
        session_->detach();

        if (!session_->attach(newLogPath)) {
            if (onError_) {
                onError_("Failed to attach to log file: " + newLogPath.string());
            }
        }
    }
}
