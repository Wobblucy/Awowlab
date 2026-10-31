// pch_base.h - Base precompiled header for non-UI modules (Parser, Database, etc.)
// STL and Windows headers only; no Vulkan or ImGui
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <string>
#include <string_view>
#include <memory>
#include <functional>
#include <optional>
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <array>
#include <span>
