#pragma once

// Compile-time switches for debug-only features. Each switch is off in
// release builds regardless of its own value.

#ifndef AWOW_DEBUG_ENABLED
    #ifdef NDEBUG
        #define AWOW_DEBUG_ENABLED 0
    #else
        #define AWOW_DEBUG_ENABLED 1
    #endif
#endif

// Log Vulkan swapchain recreation and sync frames
#define AWOW_DEBUG_VULKAN_RESOURCES     (0 && AWOW_DEBUG_ENABLED)

// Phase timings and memory snapshots for each encounter load
#define AWOW_DEBUG_PERF_MONITOR         (1 && AWOW_DEBUG_ENABLED)

// Usage: AWOW_DEBUG_LOG(VULKAN_RESOURCES, "Recreated %d images", count);
#define AWOW_DEBUG_LOG(feature, fmt, ...) \
    do { \
        if constexpr (AWOW_DEBUG_##feature) { \
            std::printf("[DEBUG:%s] " fmt "\n", #feature, ##__VA_ARGS__); \
        } \
    } while(0)
