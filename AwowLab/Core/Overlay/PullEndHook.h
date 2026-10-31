#pragma once

#include <functional>
#include <string>

struct PullSegment;

// A seam for reacting to a finished live pull without the overlay knowing what
// reacts.
//
// The standalone overlay (AwowLabOverlay.exe) builds from Core/Overlay without
// the main app's benchmark and replay code. Having the main app register a
// callback keeps those includes out of the overlay sources entirely.
//
// Nothing registers in the standalone build, so the hook is simply never
// called there.
namespace overlay {

// Fired when a pull ends. Runs on the poll thread, which may hold the
// session's actor-map lock, so an implementation must not block or call back
// into locking session APIs - hand the work to another thread.
using PullEndHook = std::function<void(const PullSegment& segment,
                                       const std::string& logPath)>;

void setPullEndHook(PullEndHook hook);
void firePullEnd(const PullSegment& segment, const std::string& logPath);
bool hasPullEndHook();

// Text the overlay shows after a pull was analysed, published by whoever did
// the analysing. Kept as plain strings so the overlay needs no knowledge of
// what produced them.
struct PullEndBanner {
    bool ready = false;
    bool favourable = false;   // Tints the banner; false reads as behind
    std::string headline;
    std::string detail;
};

void publishPullEndBanner(const PullEndBanner& banner);

// Returns and clears the pending banner. Called from the render thread.
PullEndBanner consumePullEndBanner();

} // namespace overlay
