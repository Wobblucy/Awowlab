#pragma once

struct DebugSettings {
    bool dumpDatabasesRequested = false;  // Set to true to trigger CSV dump
    bool vsyncEnabled = true;             // VSync/FPS cap (false = uncapped)
    bool vsyncChangePending = false;      // Set when vsync toggle needs swapchain recreate
};
