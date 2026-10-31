#pragma once
#include <cstdint>
#include <functional>
#include <algorithm>

class PlaybackController {
public:
    using ResetCallback = std::function<void()>;

    PlaybackController();

    // Load time range from database (supports negative timestamps for pre-encounter events)
    void loadTimeRange(int32_t minTime, int32_t maxTime);

    // deltaTime is in seconds, clamped to kMaxFrameDeltaSeconds
    void update(double deltaTime);
    static constexpr double kMaxFrameDeltaSeconds = 0.1;

    // Playback controls
    void play();
    void pause();
    void togglePlayPause();
    bool isPlaying() const { return isPlaying_; }

    // Speed controls
    void setSpeed(float speed);
    float getSpeed() const { return playbackSpeed_; }

    // Time controls (supports negative timestamps for pre-encounter events)
    void setCurrentTime(int32_t time);
    int32_t getCurrentTime() const { return currentTime_; }
    int32_t getMinTime() const { return minTimestamp_; }
    int32_t getMaxTime() const { return maxTimestamp_; }

    // Time window for event queries
    void setTimeWindow(int32_t windowMs) { timeWindowMs_ = windowMs; }
    int32_t getTimeWindow() const { return timeWindowMs_; }

    // Callback when playback loops back to start
    void setResetCallback(ResetCallback cb) { resetCallback_ = std::move(cb); }

private:
    int32_t currentTime_;
    int32_t minTimestamp_;
    int32_t maxTimestamp_;
    int32_t timeWindowMs_;

    bool isPlaying_;
    float playbackSpeed_;
    double accumulatedTimeMs_;  // sub-millisecond remainder

    ResetCallback resetCallback_;
};
