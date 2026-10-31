#pragma once
// PerfMonitor.h - Performance Monitoring for Debug Builds
//
// Usage:
//   #include "Core/PerfMonitor.h"
//
//   // Scoped timing (auto-records on destruction)
//   {
//       AWOW_PERF_SCOPE("ParseEvents");
//       // ... code to time ...
//   }
//
//   // Memory snapshots
//   AWOW_PERF_MEMORY_SNAPSHOT("After Loading");
//
//   // Database metrics
//   AWOW_PERF_DB_METRICS("CombatDatabase", recordCount, estimatedBytes, "");
//
//   // Print summary
//   perf::PerfMonitor::instance().printSummary();
//

#include "Core/DebugConfig.h"
#include <chrono>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace perf {

// Memory snapshot from Windows API
struct MemorySnapshot {
    size_t workingSetBytes = 0;        // Physical RAM used (Working Set)
    size_t privateBytes = 0;           // Private committed memory
    size_t peakWorkingSetBytes = 0;    // Peak physical RAM since process start
    std::chrono::steady_clock::time_point timestamp;
    std::string label;
};

// Timing result for a single phase
struct TimingResult {
    std::string phaseName;
    double durationMs = 0.0;
    size_t memoryBeforeBytes = 0;
    size_t memoryAfterBytes = 0;
    int64_t memoryDeltaBytes = 0;      // Can be negative if memory freed
};

// Database-specific metrics
struct DatabaseMetrics {
    std::string databaseName;
    size_t recordCount = 0;
    size_t estimatedSizeBytes = 0;
    std::string notes;
};

// Summary of all profiled phases
struct PerfSummary {
    std::vector<TimingResult> timings;
    std::vector<MemorySnapshot> memorySnapshots;
    std::vector<DatabaseMetrics> databaseMetrics;

    double totalLoadTimeMs = 0.0;
    size_t peakMemoryBytes = 0;
    size_t finalMemoryBytes = 0;
};

// RAII Scoped Timer - auto-records on destruction
class ScopedTimer {
public:
    explicit ScopedTimer(const std::string& phaseName);
    ~ScopedTimer();

    // Disable copy
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    // Allow move
    ScopedTimer(ScopedTimer&& other) noexcept;
    ScopedTimer& operator=(ScopedTimer&& other) noexcept;

    // Manual stop (for cases where destructor timing isn't ideal)
    void stop();

private:
    std::string phaseName_;
    std::chrono::steady_clock::time_point startTime_;
    size_t memoryAtStart_ = 0;
    bool stopped_ = false;
};

// Global performance monitor singleton
class PerfMonitor {
public:
    static PerfMonitor& instance();

    // Disable copy/move
    PerfMonitor(const PerfMonitor&) = delete;
    PerfMonitor& operator=(const PerfMonitor&) = delete;

    // Take a labeled memory snapshot (uses Windows GetProcessMemoryInfo)
    MemorySnapshot takeMemorySnapshot(const std::string& label = "");

    // Get current working set size (fast, no snapshot storage)
    size_t getCurrentWorkingSet() const;

    // Get peak working set since process start
    size_t getPeakWorkingSet() const;

    // Record a completed timing phase
    void recordTiming(const std::string& phaseName, double durationMs,
                      size_t memBefore = 0, size_t memAfter = 0);

    // Start a new profiling session (clears previous data)
    void beginSession(const std::string& sessionName = "");

    // End session and generate summary
    PerfSummary endSession();

    // Record metrics for a specific database
    void recordDatabaseMetrics(const std::string& dbName, size_t recordCount,
                               size_t estimatedSizeBytes, const std::string& notes = "");

    // Get current accumulated summary (without ending session)
    PerfSummary getCurrentSummary() const;

    // Print summary to stdout
    void printSummary() const;

    // Enable/disable profiling (allows zero-overhead when disabled)
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

private:
    PerfMonitor() = default;

    bool enabled_ = true;
    std::string sessionName_;

    std::vector<TimingResult> timings_;
    std::vector<MemorySnapshot> snapshots_;
    std::vector<DatabaseMetrics> dbMetrics_;

    std::chrono::steady_clock::time_point sessionStart_;

    // Helper to format bytes as human-readable string
    static std::string formatBytes(size_t bytes);
    static std::string formatMs(double ms);
};

// Convenience Macros (compile to nothing when AWOW_DEBUG_PERF_MONITOR is 0)

#if AWOW_DEBUG_PERF_MONITOR

#define AWOW_PERF_SCOPE(name) \
    perf::ScopedTimer _perfTimer_##__LINE__(name)

#define AWOW_PERF_MEMORY_SNAPSHOT(label) \
    perf::PerfMonitor::instance().takeMemorySnapshot(label)

#define AWOW_PERF_DB_METRICS(dbName, count, size, notes) \
    perf::PerfMonitor::instance().recordDatabaseMetrics(dbName, count, size, notes)

#define AWOW_PERF_BEGIN_SESSION(name) \
    perf::PerfMonitor::instance().beginSession(name)

#define AWOW_PERF_END_SESSION() \
    perf::PerfMonitor::instance().endSession()

#define AWOW_PERF_PRINT_SUMMARY() \
    perf::PerfMonitor::instance().printSummary()

#else

#define AWOW_PERF_SCOPE(name) ((void)0)
#define AWOW_PERF_MEMORY_SNAPSHOT(label) ((void)0)
#define AWOW_PERF_DB_METRICS(dbName, count, size, notes) ((void)0)
#define AWOW_PERF_BEGIN_SESSION(name) ((void)0)
#define AWOW_PERF_END_SESSION() ((void)0)
#define AWOW_PERF_PRINT_SUMMARY() ((void)0)

#endif

} // namespace perf
