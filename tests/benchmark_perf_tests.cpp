#include <catch2/catch_test_macros.hpp>

#include "Core/Benchmark/LivePullBridge.h"

#include <chrono>
#include <cstdio>
#include <filesystem>

using namespace benchmark;

// How long the post-wipe report takes to appear.
//
// The live session records only the first cast of each spell, so the benchmark
// re-reads the finished pull from the log by its byte range. That is a real
// file read plus a full parse on every wipe, and if it is slow the report
// arrives late enough to be useless - the group has already pulled again.
//
// These are timings, not correctness checks. They print numbers and only fail
// on a budget generous enough that tripping it means something is genuinely
// wrong rather than that the machine was briefly busy.
TEST_CASE("Re-parsing a pull is fast enough to report on immediately",
          "[benchmark][perf]") {
    const std::filesystem::path log = "fixtures/combat/vexamus.txt";
    if (!std::filesystem::exists(log)) {
        WARN("fixtures/combat/vexamus.txt missing - skipping timing");
        return;
    }

    const auto bytes = std::filesystem::file_size(log);

    // Warm the file cache first: the first read of a cold file measures the
    // disk, which is not what this is about.
    { LivePullEvents warm; extractLivePull(warm, log, 0, bytes); }

    constexpr int kRuns = 5;
    double totalMs = 0.0;
    double worstMs = 0.0;
    size_t casts = 0;
    size_t combat = 0;

    for (int i = 0; i < kRuns; ++i) {
        const auto start = std::chrono::steady_clock::now();
        LivePullEvents pull;
        extractLivePull(pull, log, 0, bytes);
        const auto end = std::chrono::steady_clock::now();

        REQUIRE(pull.valid);
        casts = pull.data.spellCastEvents.size();
        combat = pull.data.combatEvents.size();

        const double ms =
            std::chrono::duration<double, std::milli>(end - start).count();
        totalMs += ms;
        if (ms > worstMs) worstMs = ms;
    }

    const double avgMs = totalMs / kRuns;
    std::printf("\n[perf] re-parse %.1f MB: avg %.1f ms, worst %.1f ms "
                "(%zu casts, %zu combat events)\n",
                bytes / (1024.0 * 1024.0), avgMs, worstMs, casts, combat);
    std::printf("[perf] throughput: %.0f MB/s\n",
                (bytes / (1024.0 * 1024.0)) / (avgMs / 1000.0));

    // One second is the point at which the report stops feeling like a
    // response to the wipe. A raid pull is a few MB, so this budget has room
    // for a log several times this size before it is at risk.
    CHECK(avgMs < 1000.0);
}

TEST_CASE("Re-parse cost tracks the pull, not the log",
          "[benchmark][perf]") {
    const std::filesystem::path log = "fixtures/combat/vexamus.txt";
    if (!std::filesystem::exists(log)) {
        WARN("fixtures/combat/vexamus.txt missing - skipping timing");
        return;
    }

    const auto bytes = std::filesystem::file_size(log);
    const size_t quarter = bytes / 4;

    { LivePullEvents warm; extractLivePull(warm, log, 0, quarter); }   // Warm

    const auto timeRange = [&log](size_t from, size_t to) {
        const auto start = std::chrono::steady_clock::now();
        LivePullEvents pull;
        extractLivePull(pull, log, from, to);
        const auto end = std::chrono::steady_clock::now();
        (void)pull;
        return std::chrono::duration<double, std::milli>(end - start).count();
    };

    const double quarterMs = timeRange(0, quarter);
    const double wholeMs = timeRange(0, bytes);

    std::printf("[perf] quarter of the file: %.1f ms, whole file: %.1f ms\n",
                quarterMs, wholeMs);

    // Reading a quarter of the range must not cost what reading all of it
    // does. This is the property that keeps a long raid night cheap: the cost
    // is set by the pull that just ended, never by how much log came before.
    CHECK(quarterMs < wholeMs);
}

TEST_CASE("Reading a roster is far cheaper than parsing the pull",
          "[benchmark][perf][roster]") {
    // The point of readRoster. Filling the "which of these is you" dropdown
    // used to parse the entire pull, which on a real raid fight is hundreds of
    // thousands of events for twenty lines of answer.
    const std::filesystem::path log = "bigpull.txt";
    if (!std::filesystem::exists(log)) {
        WARN("bigpull.txt missing; skipping roster timing");
        return;
    }

    const auto bytes = std::filesystem::file_size(log);

    { LivePullEvents warm; extractLivePull(warm, log, 0, bytes); }   // Warm cache

    const auto rosterStart = std::chrono::steady_clock::now();
    const auto roster = readRoster(log, 0, bytes);
    const auto rosterEnd = std::chrono::steady_clock::now();
    const double rosterMs =
        std::chrono::duration<double, std::milli>(rosterEnd - rosterStart).count();

    const auto parseStart = std::chrono::steady_clock::now();
    LivePullEvents pull;
    extractLivePull(pull, log, 0, bytes);
    const auto parseEnd = std::chrono::steady_clock::now();
    const double parseMs =
        std::chrono::duration<double, std::milli>(parseEnd - parseStart).count();

    WARN("roster " << rosterMs << " ms vs full parse " << parseMs << " ms ("
         << roster.size() << " players)");

    REQUIRE_FALSE(roster.empty());

    // An order of magnitude at least, or the scan is reading too much.
    CHECK(rosterMs * 10.0 < parseMs);
}
