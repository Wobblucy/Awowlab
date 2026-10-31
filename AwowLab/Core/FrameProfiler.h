#pragma once
// FrameProfiler.h - Per-Frame Performance Profiling
//
// Usage:
//   #include "Core/FrameProfiler.h"
//
//   // At start of frame:
//   FRAME_PROFILE_BEGIN_FRAME();
//
//   // Time a section:
//   { FRAME_PROFILE_SCOPE("RenderScene"); /* code */ }
//
//   // At end of frame, render overlay:
//   FRAME_PROFILE_RENDER_OVERLAY();
//

#include "Core/DebugConfig.h"
#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <imgui.h>

#if AWOW_DEBUG_PERF_MONITOR

namespace perf {

struct FrameTiming {
    std::string name;
    double startMs;
    double durationMs;
    int depth;
};

struct PersistentTiming {
    std::string name;
    double avgMs;
    double lastSeenTime;  // ImGui::GetTime() when last updated
    int depth;
};

class FrameProfiler {
public:
    static FrameProfiler& instance() {
        static FrameProfiler inst;
        return inst;
    }

    void beginFrame() {
        timings_.clear();
        frameStart_ = std::chrono::high_resolution_clock::now();
        currentDepth_ = 0;
        frameCount_++;
    }

    void beginScope(const char* name) {
        auto now = std::chrono::high_resolution_clock::now();
        double startMs = std::chrono::duration<double, std::milli>(now - frameStart_).count();
        timings_.push_back({name, startMs, 0.0, currentDepth_});
        scopeStack_.push_back(timings_.size() - 1);
        currentDepth_++;
    }

    void endScope() {
        if (scopeStack_.empty()) return;
        auto now = std::chrono::high_resolution_clock::now();
        size_t idx = scopeStack_.back();
        scopeStack_.pop_back();
        currentDepth_--;

        double endMs = std::chrono::duration<double, std::milli>(now - frameStart_).count();
        timings_[idx].durationMs = endMs - timings_[idx].startMs;

        // Update persistent timing with rolling average
        const auto& t = timings_[idx];
        std::string key = std::string(t.depth, ' ') + t.name;  // Include depth in key for uniqueness

        auto it = persistentTimings_.find(key);
        if (it != persistentTimings_.end()) {
            it->second.avgMs = it->second.avgMs * 0.9 + t.durationMs * 0.1;
            it->second.lastSeenTime = ImGui::GetTime();
            it->second.depth = t.depth;
        } else {
            persistentTimings_[key] = {t.name, t.durationMs, ImGui::GetTime(), t.depth};
        }
    }

    void renderOverlay() {
        auto frameEnd = std::chrono::high_resolution_clock::now();
        double totalFrameMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart_).count();

        // Update rolling average for frame time
        avgFrameMs_ = avgFrameMs_ * 0.95 + totalFrameMs * 0.05;

        double currentTime = ImGui::GetTime();

        // Remove entries older than 3 seconds
        for (auto it = persistentTimings_.begin(); it != persistentTimings_.end(); ) {
            if (currentTime - it->second.lastSeenTime > PERSIST_DURATION_SEC) {
                it = persistentTimings_.erase(it);
            } else {
                ++it;
            }
        }

        ImGui::SetNextWindowPos(ImVec2(10, 60), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(280, 500), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.8f);
        if (ImGui::Begin("Frame Profiler", nullptr, ImGuiWindowFlags_NoFocusOnAppearing)) {

            float fps = avgFrameMs_ > 0 ? 1000.0f / static_cast<float>(avgFrameMs_) : 0.0f;

            // Color FPS based on performance
            ImVec4 fpsColor = fps >= 60 ? ImVec4(0, 1, 0, 1) :
                              fps >= 30 ? ImVec4(1, 1, 0, 1) :
                                          ImVec4(1, 0, 0, 1);

            ImGui::TextColored(fpsColor, "FPS: %.1f (%.2f ms)", fps, avgFrameMs_);
            ImGui::Separator();

            // Collect and sort entries by depth then name for consistent ordering
            std::vector<std::pair<std::string, PersistentTiming>> sorted;
            sorted.reserve(persistentTimings_.size());
            for (const auto& [key, timing] : persistentTimings_) {
                sorted.emplace_back(key, timing);
            }
            std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
                return a.first < b.first;
            });

            // Show all persistent timings
            for (const auto& [key, timing] : sorted) {
                // Indent based on depth
                std::string indent(timing.depth * 2, ' ');

                // Fade out entries that haven't been seen recently
                float age = static_cast<float>(currentTime - timing.lastSeenTime);
                float alpha = (age < 0.5f) ? 1.0f : std::max(0.3f, 1.0f - (age - 0.5f) / 2.5f);

                // Color based on time (red if > 5ms, yellow if > 2ms)
                ImVec4 color = timing.avgMs > 5.0 ? ImVec4(1, 0.3f, 0.3f, alpha) :
                               timing.avgMs > 2.0 ? ImVec4(1, 1, 0.3f, alpha) :
                                                    ImVec4(0.7f, 0.7f, 0.7f, alpha);

                ImGui::TextColored(color, "%s%s: %.2f ms", indent.c_str(), timing.name.c_str(), timing.avgMs);
            }
        }
        ImGui::End();
    }

private:
    FrameProfiler() = default;

    static constexpr double PERSIST_DURATION_SEC = 3.0;

    std::chrono::high_resolution_clock::time_point frameStart_;
    std::vector<FrameTiming> timings_;
    std::vector<size_t> scopeStack_;
    std::unordered_map<std::string, PersistentTiming> persistentTimings_;
    int currentDepth_ = 0;
    uint64_t frameCount_ = 0;
    double avgFrameMs_ = 16.67;
};

class ScopedFrameTimer {
public:
    ScopedFrameTimer(const char* name) {
        FrameProfiler::instance().beginScope(name);
    }
    ~ScopedFrameTimer() {
        FrameProfiler::instance().endScope();
    }
};

} // namespace perf

#define FRAME_PROFILE_BEGIN_FRAME() perf::FrameProfiler::instance().beginFrame()
#define FRAME_PROFILE_CONCAT_IMPL(a, b) a##b
#define FRAME_PROFILE_CONCAT(a, b) FRAME_PROFILE_CONCAT_IMPL(a, b)
#define FRAME_PROFILE_SCOPE(name) perf::ScopedFrameTimer FRAME_PROFILE_CONCAT(_frameTimer, __LINE__)(name)
#define FRAME_PROFILE_RENDER_OVERLAY() perf::FrameProfiler::instance().renderOverlay()

#else

#define FRAME_PROFILE_BEGIN_FRAME() ((void)0)
#define FRAME_PROFILE_SCOPE(name) ((void)0)
#define FRAME_PROFILE_RENDER_OVERLAY() ((void)0)

#endif
