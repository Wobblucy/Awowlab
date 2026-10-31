#include "Core/PerfMonitor.h"
#include <iomanip>
#include <sstream>

namespace perf {

// ScopedTimer Implementation

ScopedTimer::ScopedTimer(const std::string& phaseName)
    : phaseName_(phaseName)
    , startTime_(std::chrono::steady_clock::now())
    , memoryAtStart_(PerfMonitor::instance().getCurrentWorkingSet())
    , stopped_(false) {
}

ScopedTimer::~ScopedTimer() {
    if (!stopped_) {
        stop();
    }
}

ScopedTimer::ScopedTimer(ScopedTimer&& other) noexcept
    : phaseName_(std::move(other.phaseName_))
    , startTime_(other.startTime_)
    , memoryAtStart_(other.memoryAtStart_)
    , stopped_(other.stopped_) {
    other.stopped_ = true;  // Prevent double recording
}

ScopedTimer& ScopedTimer::operator=(ScopedTimer&& other) noexcept {
    if (this != &other) {
        if (!stopped_) {
            stop();
        }
        phaseName_ = std::move(other.phaseName_);
        startTime_ = other.startTime_;
        memoryAtStart_ = other.memoryAtStart_;
        stopped_ = other.stopped_;
        other.stopped_ = true;
    }
    return *this;
}

void ScopedTimer::stop() {
    if (stopped_) return;

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(endTime - startTime_);
    size_t memoryAtEnd = PerfMonitor::instance().getCurrentWorkingSet();

    PerfMonitor::instance().recordTiming(
        phaseName_,
        duration.count(),
        memoryAtStart_,
        memoryAtEnd
    );

    stopped_ = true;
}

// PerfMonitor Implementation

PerfMonitor& PerfMonitor::instance() {
    static PerfMonitor instance;
    return instance;
}

MemorySnapshot PerfMonitor::takeMemorySnapshot(const std::string& label) {
    MemorySnapshot snapshot;
    snapshot.timestamp = std::chrono::steady_clock::now();
    snapshot.label = label;

#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                             sizeof(pmc))) {
        snapshot.workingSetBytes = pmc.WorkingSetSize;
        snapshot.privateBytes = pmc.PrivateUsage;
        snapshot.peakWorkingSetBytes = pmc.PeakWorkingSetSize;
    }
#endif

    if (enabled_) {
        snapshots_.push_back(snapshot);
    }

    return snapshot;
}

size_t PerfMonitor::getCurrentWorkingSet() const {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                             sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
#endif
    return 0;
}

size_t PerfMonitor::getPeakWorkingSet() const {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                             sizeof(pmc))) {
        return pmc.PeakWorkingSetSize;
    }
#endif
    return 0;
}

void PerfMonitor::recordTiming(const std::string& phaseName, double durationMs,
                                size_t memBefore, size_t memAfter) {
    if (!enabled_) return;

    TimingResult result;
    result.phaseName = phaseName;
    result.durationMs = durationMs;
    result.memoryBeforeBytes = memBefore;
    result.memoryAfterBytes = memAfter;
    result.memoryDeltaBytes = static_cast<int64_t>(memAfter) - static_cast<int64_t>(memBefore);

    timings_.push_back(result);
}

void PerfMonitor::beginSession(const std::string& sessionName) {
    sessionName_ = sessionName;
    timings_.clear();
    snapshots_.clear();
    dbMetrics_.clear();
    sessionStart_ = std::chrono::steady_clock::now();
}

PerfSummary PerfMonitor::endSession() {
    PerfSummary summary = getCurrentSummary();
    return summary;
}

void PerfMonitor::recordDatabaseMetrics(const std::string& dbName, size_t recordCount,
                                         size_t estimatedSizeBytes, const std::string& notes) {
    if (!enabled_) return;

    DatabaseMetrics metrics;
    metrics.databaseName = dbName;
    metrics.recordCount = recordCount;
    metrics.estimatedSizeBytes = estimatedSizeBytes;
    metrics.notes = notes;

    dbMetrics_.push_back(metrics);
}

PerfSummary PerfMonitor::getCurrentSummary() const {
    PerfSummary summary;
    summary.timings = timings_;
    summary.memorySnapshots = snapshots_;
    summary.databaseMetrics = dbMetrics_;

    // Calculate totals
    for (const auto& timing : timings_) {
        summary.totalLoadTimeMs += timing.durationMs;
    }

    summary.peakMemoryBytes = getPeakWorkingSet();
    summary.finalMemoryBytes = getCurrentWorkingSet();

    return summary;
}

void PerfMonitor::printSummary() const {
    if (!enabled_) return;

    std::printf("\n");
    std::printf("================================================================================\n");
    std::printf("                        PERFORMANCE SUMMARY\n");
    std::printf("================================================================================\n");

    // Session info
    if (!sessionName_.empty()) {
        std::printf("Session: %s\n", sessionName_.c_str());
    }
    std::printf("\n");

    // Timing breakdown
    if (!timings_.empty()) {
        std::printf("--- TIMING BREAKDOWN ---\n");
        std::printf("%-40s %10s %12s %12s\n", "Phase", "Time", "Mem Before", "Mem Delta");
        std::printf("--------------------------------------------------------------------------------\n");

        double totalMs = 0.0;
        for (const auto& timing : timings_) {
            std::printf("%-40s %10s %12s %+12s\n",
                timing.phaseName.c_str(),
                formatMs(timing.durationMs).c_str(),
                formatBytes(timing.memoryBeforeBytes).c_str(),
                formatBytes(static_cast<size_t>(std::abs(timing.memoryDeltaBytes))).c_str()
            );
            totalMs += timing.durationMs;
        }
        std::printf("--------------------------------------------------------------------------------\n");
        std::printf("%-40s %10s\n", "TOTAL", formatMs(totalMs).c_str());
        std::printf("\n");
    }

    // Memory snapshots
    if (!snapshots_.empty()) {
        std::printf("--- MEMORY SNAPSHOTS ---\n");
        std::printf("%-30s %15s %15s\n", "Label", "Working Set", "Private Bytes");
        std::printf("--------------------------------------------------------------------------------\n");

        for (const auto& snapshot : snapshots_) {
            std::printf("%-30s %15s %15s\n",
                snapshot.label.c_str(),
                formatBytes(snapshot.workingSetBytes).c_str(),
                formatBytes(snapshot.privateBytes).c_str()
            );
        }
        std::printf("\n");
    }

    // Database metrics
    if (!dbMetrics_.empty()) {
        std::printf("--- DATABASE METRICS ---\n");
        std::printf("%-25s %15s %15s   %s\n", "Database", "Records", "Est. Size", "Notes");
        std::printf("--------------------------------------------------------------------------------\n");

        size_t totalRecords = 0;
        size_t totalSize = 0;
        for (const auto& db : dbMetrics_) {
            std::printf("%-25s %15zu %15s   %s\n",
                db.databaseName.c_str(),
                db.recordCount,
                formatBytes(db.estimatedSizeBytes).c_str(),
                db.notes.c_str()
            );
            totalRecords += db.recordCount;
            totalSize += db.estimatedSizeBytes;
        }
        std::printf("--------------------------------------------------------------------------------\n");
        std::printf("%-25s %15zu %15s\n", "TOTAL", totalRecords, formatBytes(totalSize).c_str());
        std::printf("\n");
    }

    // Summary
    std::printf("--- MEMORY SUMMARY ---\n");
    std::printf("Current Working Set: %s\n", formatBytes(getCurrentWorkingSet()).c_str());
    std::printf("Peak Working Set:    %s\n", formatBytes(getPeakWorkingSet()).c_str());
    std::printf("================================================================================\n\n");
}

std::string PerfMonitor::formatBytes(size_t bytes) {
    std::ostringstream oss;

    if (bytes >= 1024 * 1024 * 1024) {
        oss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
    } else if (bytes >= 1024 * 1024) {
        oss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0)) << " MB";
    } else if (bytes >= 1024) {
        oss << std::fixed << std::setprecision(2) << (bytes / 1024.0) << " KB";
    } else {
        oss << bytes << " B";
    }

    return oss.str();
}

std::string PerfMonitor::formatMs(double ms) {
    std::ostringstream oss;

    if (ms >= 1000.0) {
        oss << std::fixed << std::setprecision(2) << (ms / 1000.0) << " s";
    } else {
        oss << std::fixed << std::setprecision(1) << ms << " ms";
    }

    return oss.str();
}

} // namespace perf
