#include "Core/Overlay/PullEndHook.h"

#include <mutex>

namespace overlay {

namespace {

std::mutex& hookMutex() {
    static std::mutex m;
    return m;
}

PullEndHook& hookSlot() {
    static PullEndHook hook;
    return hook;
}

std::mutex& bannerMutex() {
    static std::mutex m;
    return m;
}

PullEndBanner& bannerSlot() {
    static PullEndBanner banner;
    return banner;
}

} // namespace

void setPullEndHook(PullEndHook hook) {
    std::lock_guard<std::mutex> lock(hookMutex());
    hookSlot() = std::move(hook);
}

bool hasPullEndHook() {
    std::lock_guard<std::mutex> lock(hookMutex());
    return static_cast<bool>(hookSlot());
}

void firePullEnd(const PullSegment& segment, const std::string& logPath) {
    // Copy the callable out before invoking it: holding the lock across a
    // callback that may take its own would be a deadlock waiting to happen.
    PullEndHook hook;
    {
        std::lock_guard<std::mutex> lock(hookMutex());
        hook = hookSlot();
    }
    if (hook) hook(segment, logPath);
}

void publishPullEndBanner(const PullEndBanner& banner) {
    std::lock_guard<std::mutex> lock(bannerMutex());
    bannerSlot() = banner;
}

PullEndBanner consumePullEndBanner() {
    std::lock_guard<std::mutex> lock(bannerMutex());
    PullEndBanner out = bannerSlot();
    bannerSlot().ready = false;
    return out;
}

} // namespace overlay
