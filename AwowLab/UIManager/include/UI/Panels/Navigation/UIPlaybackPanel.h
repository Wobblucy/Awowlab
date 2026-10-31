#pragma once
#include <memory>
#include <vector>
#include <cstdint>

class PlaybackController;
class DeathDatabase;
struct CameraSettings;
struct BossTimerSettings;

// Modern media player-style playback bar with custom rendering
class UIPlaybackPanel {
public:
    UIPlaybackPanel() = default;

    // Render the playback controls window (timeline, play/pause, speed)
    // deathDb is optional - if provided, death markers are shown on timeline
    // cameraSettings is for the follow checkbox (always visible on bar)
    // bossTimerSettings is for the boss timer toggle (optional)
    void render(const std::shared_ptr<PlaybackController>& playback,
                const DeathDatabase* deathDb,
                CameraSettings& cameraSettings,
                BossTimerSettings* bossTimerSettings = nullptr);

private:
    // Render subsections
    void renderTransportControls(const std::shared_ptr<PlaybackController>& playback);
    void renderTimeline(const std::shared_ptr<PlaybackController>& playback,
                       const DeathDatabase* deathDb);
    void renderSpeedControls(const std::shared_ptr<PlaybackController>& playback);
    void renderTimeDisplay(const std::shared_ptr<PlaybackController>& playback);

    // Helper to format time as M:SS or MM:SS (supports negative timestamps)
    static void formatTime(int32_t ms, char* buffer, size_t bufferSize);

    // Cached death positions for timeline markers (normalized 0-1)
    std::vector<float> deathPositions_;
    int32_t lastMinTime_ = 0;
    int32_t lastMaxTime_ = 0;
};
