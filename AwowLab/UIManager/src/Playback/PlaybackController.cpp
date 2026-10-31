#include "Playback/PlaybackController.h"

PlaybackController::PlaybackController()
    : currentTime_(0)
    , minTimestamp_(0)
    , maxTimestamp_(0)
    , timeWindowMs_(10000)
    , isPlaying_(false)
    , playbackSpeed_(1.0f)
    , accumulatedTimeMs_(0.0)
{
}

void PlaybackController::loadTimeRange(int32_t minTime, int32_t maxTime) {
    minTimestamp_ = minTime;
    maxTimestamp_ = maxTime;
    // Start at 0:00, the encounter start, when it is in range. Pre-pull events
    // have negative timestamps and are reached by rewinding.
    if (minTime <= 0 && maxTime >= 0) {
        currentTime_ = 0;
    } else {
        currentTime_ = minTime;
    }
}

void PlaybackController::update(double deltaTime) {
    if (!isPlaying_ || maxTimestamp_ <= minTimestamp_) {
        return;
    }

    // A frame that took this long was a stall (window minimized, a modal
    // menu, a load), not playback time the user watched go by
    deltaTime = std::clamp(deltaTime, 0.0, kMaxFrameDeltaSeconds);

    // Carry fractional milliseconds over, or high frame rates would lose time to truncation
    accumulatedTimeMs_ += deltaTime * 1000.0 * playbackSpeed_;

    if (accumulatedTimeMs_ >= 1.0) {
        int32_t timeAdvance = static_cast<int32_t>(accumulatedTimeMs_);
        currentTime_ += timeAdvance;
        accumulatedTimeMs_ -= static_cast<double>(timeAdvance);
    }

    // At the end, loop back to the encounter start (or minTimestamp if 0 is out of range)
    if (currentTime_ >= maxTimestamp_) {
        if (minTimestamp_ <= 0 && maxTimestamp_ >= 0) {
            currentTime_ = 0;
        } else {
            currentTime_ = minTimestamp_;
        }
        accumulatedTimeMs_ = 0.0;
        if (resetCallback_) {
            resetCallback_();
        }
    }
}

void PlaybackController::play() {
    isPlaying_ = true;
}

void PlaybackController::pause() {
    isPlaying_ = false;
}

void PlaybackController::togglePlayPause() {
    isPlaying_ = !isPlaying_;
}

void PlaybackController::setSpeed(float speed) {
    playbackSpeed_ = std::max(0.1f, std::min(10.0f, speed));
}

void PlaybackController::setCurrentTime(int32_t time) {
    currentTime_ = std::clamp(time, minTimestamp_, maxTimestamp_);
}
