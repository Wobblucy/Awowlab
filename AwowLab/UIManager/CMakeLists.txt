cmake_minimum_required(VERSION 3.12)
project(UIManager LANGUAGES CXX)

# Find Vulkan
find_package(Vulkan REQUIRED)

# Fetch GLFW
include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)

# Configure GLFW options
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw)

# Fetch TinyObjLoader (header-only OBJ parser). Pinned to the commit the
# release branch pointed at when we last took it; the branch itself moves.
FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG 45636bdcef1a4fec140346b90c0b50bf0bc3e23b
)
FetchContent_MakeAvailable(tinyobjloader)

# Disable warnings-as-errors for tinyobjloader (third-party dependency)
if(TARGET tinyobjloader)
    target_compile_options(tinyobjloader PRIVATE /W0)
endif()

# Fetch ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5
)
FetchContent_MakeAvailable(imgui)

# Create ImGui library
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

# ImPlot - plotting widgets for the benchmark report card. Vendored in-tree
# (see third_party/implot/VENDORED.md) rather than fetched, so the build stays
# offline-capable and any local patch is visible in this repo's history.
add_library(implot STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/implot/implot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/implot/implot_items.cpp
)

target_include_directories(implot PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/implot
)

target_link_libraries(implot PUBLIC imgui)

# Third-party source: its warnings are not ours to fix, and the project builds
# with warnings-as-errors.
if(MSVC)
    target_compile_options(implot PRIVATE /W0)
endif()

# Meter/overlay UI core - the subset of UIManager the live overlay needs.
# Kept as its own static library so the standalone overlay executable can
# link just these files; this source list is the overlay's boundary for
# UIManager code.
add_library(UIMeterCore STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Color/ActorColorGenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/MeterBarRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/AwlUI/Theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/AwlUI/Widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Overlay/OverlayWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Overlay/UIOverlayControls.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Overlay/UIStalenessIndicator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Combat/BreakdownGraphRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Combat/BreakdownSpellTableRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Combat/UIActorBreakdownPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Combat/UIAvoidanceBreakdownPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Combat/UIDeathRecapPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Combat/UIMeterPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Combat/UIMeterPanel_Breakdowns.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Combat/UIMeterPanel_Views.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Settings/UIMobWeightPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Settings/UIPhaseEditorPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/SpellContextMenu.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/SpellGroupSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/SpellLinkOpener.cpp
)

target_include_directories(UIMeterCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(UIMeterCore
    PUBLIC
        glfw
        Vulkan::Vulkan
        imgui
        Parser
        Database
)

target_precompile_headers(UIMeterCore PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/../pch.h>"
)

set_target_properties(UIMeterCore PROPERTIES
    UNITY_BUILD ON
    UNITY_BUILD_BATCH_SIZE 16
)

# Build UIManager as a static library
add_library(UIManager STATIC
    # Core - Window and UI Management
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/BrowserHelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/WindowManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/UIManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/ImGuiManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/MapVisualizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/FrameDataProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/RenderingCoordinator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/UIWindowManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/SpellStateComputer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/MapChangeController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/HostileSpellCollector.cpp

    # Vulkan Core - Instance and Surface
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/Core/VulkanInstance.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/Core/VulkanDebug.cpp

    # Vulkan Device - Physical and Logical Device
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/Device/VulkanDevice.cpp

    # Vulkan Commands - Command Pools and Buffers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/Commands/VulkanCommands.cpp

    # Vulkan Sync - Synchronization Primitives
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/Sync/VulkanSync.cpp

    # Vulkan Swapchain - Swapchain and Depth Buffer
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/Swapchain/VulkanSwapchain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/Swapchain/VulkanDepthBuffer.cpp

    # Vulkan Pipeline - Graphics Pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/Pipeline/VulkanPipeline3D.cpp

    # Vulkan Buffers - Buffer Management
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/Buffers/VulkanBufferManager3D.cpp

    # Vulkan Rendering - High-level Rendering
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/Rendering/VulkanRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/Rendering/VulkanRenderer/DatabaseLoading.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/Rendering/VulkanRenderer/FrameRendering.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/Rendering/VulkanRenderer/SwapchainManagement.cpp

    # Vulkan Textures - Texture Management (main app only; the overlay
    # renders through the ImGui Vulkan backend, which manages its own
    # font atlas, so UIMeterCore needs no texture upload code)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/Textures/VulkanTextureManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/SpellIconLoader.cpp

    # Benchmark report card (main app only - the overlay has no encounter to
    # compare against, and this is the only code that pulls in ImPlot)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Benchmark/BenchmarkCharts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Benchmark/UIBenchmarkReportCard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Benchmark/UIClassificationReview.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Benchmark/UIMentorPreview.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Benchmark/UIPrepareWizard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Benchmark/UIMentorPicker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Benchmark/ReportContextBuilder.cpp

    # Camera System
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Camera/Camera3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Camera/CameraController.cpp

    # Animation System
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Animation/ActorPositionInterpolator.cpp

    # Color System

    # Playback System
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Playback/PlaybackController.cpp

    # Input System
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Input/MouseRaycaster.cpp

    # Rendering Components
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Rendering/Minimap2D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Rendering/SceneRenderer3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Rendering/Rendering3D/FloorRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Rendering/Rendering3D/ActorRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Rendering/Rendering3D/SelectionRingRenderer.cpp

    # UI Panels
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Navigation/UIPlaybackPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/UICreaturesPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/UIActorSelector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Cooldowns/UISpellHistory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/RaidNoteIconMenu.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/SpecIconLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/ClassIconCache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Navigation/UIEncounterSelector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Frames/UIRaidFramePanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Frames/UIEnemyGridPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/UIActorAppearancePanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/UIActorAppearancePanel_Individual.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/UIActorAppearancePanel_Enemies.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/UIActorAppearancePanel_CustomModels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/UIActorAppearancePanel_Widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/UINameplateSettingsPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/ActorAppearanceSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/WorldMapTextureManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/EncounterTextureManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Navigation/UILandingPage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/LandingPage/ThemeSetup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/LandingPage/CardRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Debug/UIDeathsPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Debug/UIAuraDebugPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/WarcraftLogs/UIWarcraftLogsPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/WarcraftLogs/UIWarcraftLogsUrlDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Navigation/UICachedFightsPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/SpellGCDDatabase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/SpellAssignmentSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/UISpellAssignmentPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Cooldowns/UICooldownPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Cooldowns/UIRaidCooldownPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Frames/UIPlayerBuffPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Frames/UIPlayerDebuffPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/NameplateSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/FacingIndicatorSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/RaidFrameConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/BossTimerSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Navigation/BossTimerStrip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Navigation/UIUpcomingBossCasts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Settings/UIEnemyCastFilterPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Settings/UISettingsHub.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Navigation/UIHelpPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/UISettingsExportPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/RaidNotes/UIRaidNotesPanel.cpp

    # PlayAlong Module
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayAlong/UserCloneActor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayAlong/PlayAlongInputHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI/Panels/Baseline/UISpecSelector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayAlong/UIPlayerSelector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayAlong/PlayAlongVisualizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayAlong/PlayAlongVisualizer_Database.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayAlong/PlayAlongVisualizer_Input.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayAlong/PlayAlongVisualizer_UI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayAlong/PlayAlongVisualizer_3D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayAlong/UIPlayAlongCombinedGCD.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayAlong/UIPlayAlongStatsPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayAlong/PlayAlongKeybindSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayAlong/UIKeybindConfigModal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PlayAlong/PlayAlongFeedbackSystem.cpp

    # Custom Model Loading (TinyObjLoader)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Geometry/BakedModelPack.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Geometry/CustomModelLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Geometry/CustomShapeRegistry.cpp
)

target_include_directories(UIManager
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${tinyobjloader_SOURCE_DIR}
)

# Link against the meter core plus GLFW, Vulkan, ImGui, Parser, Database,
# warcraftlogs_manager, AnnotationManager, RaidNotes, and AssetManager
target_link_libraries(UIManager
    PUBLIC
        UIMeterCore
        glfw
        Vulkan::Vulkan
        imgui
        implot
        Parser
        Database
        warcraftlogs_manager
        AnnotationManager
        RaidNotes
        AssetManager
    PRIVATE
        awowlab_build_options  # Whether this build has the updater
)

# Precompiled header for faster compilation (STL, Vulkan, ImGui compiled once)
target_precompile_headers(UIManager PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/../pch.h>"
)

# Unity build for faster compilation (batches source files together)
set_target_properties(UIManager PROPERTIES
    UNITY_BUILD ON
    UNITY_BUILD_BATCH_SIZE 16
)

# VulkanTextureManager.cpp compiles the stb_image implementation
# (STB_IMAGE_IMPLEMENTATION). Keep it out of the unity batches so the
# define can't leak into other files that include stb_image.h.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/Textures/VulkanTextureManager.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)
