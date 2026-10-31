#include "Core/ImGuiManager.h"
#include <implot.h>
#include "Core/MapVisualizer.h"
#include "Core/UnifiedSettings.h"
#include "Core/ErrorLogger.h"
#include "UI/AwlUI/Theme.h"
#include "../../Database/EventDatabase.h"
#include "../../Database/OutputWriter.h"
#include "AnnotationManager/include/TriggerEvaluators/TriggerEvaluatorCommon.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <cstdio>

namespace {

constexpr const char* kRegularFontPath = "data/fonts/NotoSans-Regular.ttf";

// Fixed base size; user scaling is applied through FontGlobalScale
constexpr float kBaseFontSize = 16.0f;

// A real 36px font for headers, because SetWindowFontScale blurs
constexpr float kHeaderFontSize = 36.0f;

// Latin, Latin Extended and Cyrillic cover the European client locales
constexpr ImWchar kLatinCyrillicRanges[] = {
    0x0020, 0x00FF,  // Basic Latin + Latin-1 Supplement (Western European languages)
    0x0100, 0x017F,  // Latin Extended-A (Polish, Czech, Hungarian, etc.)
    0x0180, 0x024F,  // Latin Extended-B
    0x0400, 0x04FF,  // Cyrillic (Russian, Ukrainian, etc.)
    0x2000, 0x206F,  // General Punctuation (includes bullet, em dash, etc.)
    0
};

// CJK fonts, bundled first and then the Windows system font
constexpr const char* kChineseSimplifiedFontPaths[] = {"data/fonts/NotoSansSC-Regular.ttf", "C:\\Windows\\Fonts\\msyh.ttc"};
constexpr const char* kChineseTraditionalFontPaths[] = {"data/fonts/NotoSansTC-Regular.ttf", "C:\\Windows\\Fonts\\msjh.ttc"};
constexpr const char* kKoreanFontPaths[] = {"data/fonts/NotoSansKR-Regular.ttf", "C:\\Windows\\Fonts\\malgun.ttf"};

// AddFontFromFileTTF on a missing file trips ImGui's font-atlas error path,
// so every optional font is checked first
bool fileExists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

// Merges the first font that exists into the font added before it
template <size_t N>
void mergeFirstAvailableFont(ImGuiIO& io, const char* const (&paths)[N], float fontSize, const ImWchar* ranges) {
    ImFontConfig mergeConfig;
    mergeConfig.MergeMode = true;
    mergeConfig.PixelSnapH = true;

    for (const char* path : paths) {
        if (fileExists(path)) {
            io.Fonts->AddFontFromFileTTF(path, fontSize, &mergeConfig, ranges);
            return;
        }
    }
}

// awlui font handles - Medium for section headers / labels that want more
// visual weight than Regular, and Mono for numeric spans in the meter
// drill-down. Both are optional: if the TTF isn't present in data/fonts/,
// the load is skipped and widgets fall through to the current font.
//
// Called from both the initial load and the CJK atlas rebuild: Clear()
// destroys every ImFont, so the handles awlui holds must be re-added and
// re-captured each time the atlas is rebuilt.
void addAwluiFonts(ImGuiIO& io, float baseFontSize, const ImWchar* ranges) {
    ImFont* mediumFont = nullptr;
    if (fileExists("data/fonts/NotoSans-Medium.ttf")) {
        mediumFont = io.Fonts->AddFontFromFileTTF(
            "data/fonts/NotoSans-Medium.ttf", baseFontSize, nullptr, ranges);
    }
    ImFont* monoFont = nullptr;
    if (fileExists("data/fonts/NotoSansMono-Regular.ttf")) {
        monoFont = io.Fonts->AddFontFromFileTTF(
            "data/fonts/NotoSansMono-Regular.ttf", baseFontSize - 1.0f, nullptr, ranges);
    }
    awlui::theme::CaptureFonts(mediumFont, monoFont);
}

// Neutral surfaces with a reserved amber accent. See AwowLab/UIManager/include/UI/AwlUI/Theme.h for the
// "accent is reserved" rules; the color tokens below come from
// awlui::theme so the base theme and custom widgets share a
// palette (unmigrated Combos read the same as migrated Buttons).
void applyAppTheme() {
    ImGui::StyleColorsDark();

    namespace T = awlui::theme;
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = T::RadiusCard;
    style.FrameRounding     = T::RadiusControl;
    style.PopupRounding     = T::RadiusControl;
    style.ScrollbarRounding = T::RadiusPill;
    style.GrabRounding      = T::RadiusPill;
    style.TabRounding       = T::RadiusControl;
    style.ChildRounding     = T::RadiusControl;

    style.WindowPadding    = ImVec2(T::SpaceL, T::SpaceL);   // 16,16
    style.FramePadding     = ImVec2(T::SpaceM, 6.0f);        // 12,6
    style.ItemSpacing      = ImVec2(T::SpaceM, T::SpaceS);   // 12,8
    style.ItemInnerSpacing = ImVec2(T::SpaceS, T::SpaceXS);  // 8,4
    style.ScrollbarSize    = 12.0f;
    style.GrabMinSize      = 12.0f;

    // No built-in borders: custom widgets draw their own
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize  = 0.0f;
    style.PopupBorderSize  = T::BorderThin;   // keep popup outline for legibility
    style.TabBorderSize    = 0.0f;

    style.AntiAliasedLines = true;
    style.AntiAliasedFill  = true;

    ImVec4* colors = style.Colors;

    // Translucent window/child backgrounds let game content show through
    // when the overlay is stacked over WoW
    auto withAlpha = [](ImVec4 c, float a) {
        return ImVec4(c.x, c.y, c.z, a);
    };

    // Popups and menus stay opaque so a picker is readable mid-fight
    colors[ImGuiCol_WindowBg]  = withAlpha(T::Surface0, 0.80f);
    colors[ImGuiCol_ChildBg]   = withAlpha(T::Surface0, 0.55f);
    colors[ImGuiCol_PopupBg]   = T::Surface1;
    colors[ImGuiCol_MenuBarBg] = T::Surface1;

    colors[ImGuiCol_Border]       = T::BorderSubtle;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.15f);

    // Frames (built-in combos, inputs): neutral idle, subtle hover, subtle press. No amber tint -
    // reserved for the ButtonActive state below.
    colors[ImGuiCol_FrameBg]        = T::Surface2;
    colors[ImGuiCol_FrameBgHovered] = T::Surface3;
    colors[ImGuiCol_FrameBgActive]  = T::Surface3;

    colors[ImGuiCol_TitleBg]          = T::Surface1;
    colors[ImGuiCol_TitleBgActive]    = T::Surface2;
    colors[ImGuiCol_TitleBgCollapsed] = withAlpha(T::Surface1, 0.70f);

    // Tabs: the active tab keeps amber, matching the underline on
    // migrated awlui::TabBar.
    colors[ImGuiCol_Tab]                = T::Surface1;
    colors[ImGuiCol_TabHovered]         = T::Surface3;
    colors[ImGuiCol_TabActive]          = T::AccentActive;
    colors[ImGuiCol_TabUnfocused]       = T::Surface1;
    colors[ImGuiCol_TabUnfocusedActive] = T::Surface2;

    // Headers (Selectable, CollapsingHeader): active is amber, matching
    // the keyboard nav focus signal.
    colors[ImGuiCol_Header]        = T::Surface2;
    colors[ImGuiCol_HeaderHovered] = T::Surface3;
    colors[ImGuiCol_HeaderActive]  = T::AccentActive;

    // Built-in buttons: active is amber. awlui::Button(Primary) draws its own amber fill even at
    // idle - this base color only affects unmigrated call sites.
    colors[ImGuiCol_Button]        = T::Surface2;
    colors[ImGuiCol_ButtonHovered] = T::Surface3;
    colors[ImGuiCol_ButtonActive]  = T::AccentActive;

    colors[ImGuiCol_ScrollbarBg]          = withAlpha(T::Surface0, 0.40f);
    colors[ImGuiCol_ScrollbarGrab]        = T::Surface3;
    colors[ImGuiCol_ScrollbarGrabHovered] = withAlpha(T::TextSecondary, 0.60f);
    colors[ImGuiCol_ScrollbarGrabActive]  = T::TextSecondary;

    // Sliders, checkmarks, plots and nav focus use the reserved amber
    colors[ImGuiCol_SliderGrab]       = T::Accent;
    colors[ImGuiCol_SliderGrabActive] = T::AccentActive;

    colors[ImGuiCol_CheckMark] = T::Accent;

    // Separators turn amber while a splitter is dragged
    colors[ImGuiCol_Separator]        = T::BorderSubtle;
    colors[ImGuiCol_SeparatorHovered] = T::BorderStrong;
    colors[ImGuiCol_SeparatorActive]  = T::AccentActive;

    colors[ImGuiCol_ResizeGrip]        = withAlpha(T::TextSecondary, 0.30f);
    colors[ImGuiCol_ResizeGripHovered] = withAlpha(T::TextSecondary, 0.60f);
    colors[ImGuiCol_ResizeGripActive]  = T::Accent;

    colors[ImGuiCol_Text]           = T::TextPrimary;
    colors[ImGuiCol_TextDisabled]   = T::TextMuted;
    colors[ImGuiCol_TextSelectedBg] = T::AccentSubtle;

    colors[ImGuiCol_PlotLines]            = T::Accent;
    colors[ImGuiCol_PlotLinesHovered]     = T::AccentHover;
    colors[ImGuiCol_PlotHistogram]        = T::Accent;
    colors[ImGuiCol_PlotHistogramHovered] = T::AccentHover;

    colors[ImGuiCol_TableHeaderBg]     = T::Surface2;
    colors[ImGuiCol_TableBorderStrong] = T::BorderStrong;
    colors[ImGuiCol_TableBorderLight]  = T::BorderSubtle;
    colors[ImGuiCol_TableRowBg]        = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]     = withAlpha(T::Surface1, 0.40f);

    colors[ImGuiCol_DragDropTarget]        = T::Accent;
    colors[ImGuiCol_NavHighlight]          = T::Accent;
    colors[ImGuiCol_NavWindowingHighlight] = T::AccentHover;
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.02f, 0.02f, 0.02f, 0.50f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.01f, 0.01f, 0.01f, 0.65f);
}

} // namespace

ImGuiManager::ImGuiManager()
    : descriptorPool(VK_NULL_HANDLE)
    , isWhite(false)
    , mapVisualizer(std::make_unique<MapVisualizer>())
{
}

ImGuiManager::~ImGuiManager() {
}

bool ImGuiManager::initialize(GLFWwindow* window, VkInstance instance,
                              VkPhysicalDevice physicalDevice, VkDevice device,
                              uint32_t queueFamily, VkQueue graphicsQueue,
                              VkRenderPass renderPass, uint32_t minImageCount, uint32_t imageCount) {

    // Kept for rebuilding the font atlas later
    device_ = device;
    graphicsQueue_ = graphicsQueue;

    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptorPool) != VK_SUCCESS) {
        AWOW_LOG_ERROR("ImGui", "Failed to create ImGui descriptor pool");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // ImPlot keeps its own context and every one of its calls asserts on it,
    // so it has to be created alongside ImGui's rather than lazily by whoever
    // draws the first chart.
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Fonts[0]: base font
    ImFont* font = io.Fonts->AddFontFromFileTTF(kRegularFontPath, kBaseFontSize, nullptr, kLatinCyrillicRanges);
    if (!font) {
        AWOW_LOG_WARNING("ImGui", "Could not load NotoSans-Regular.ttf, falling back to default font");
        io.Fonts->AddFontDefault();
    }

    // A CJK locale gets the CJK fonts up front. Other locales load just the
    // glyphs they need later, when player names call for them. Both Chinese
    // variants use the ChineseSimplifiedCommon range (~2,500 characters
    // instead of 20k+ for the full set) to keep memory down.
    const std::string& locale = SettingsCache::instance().get().locale;
    if (isCjkLocale(locale)) {
        const ImWchar* chineseRanges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
        mergeFirstAvailableFont(io, kChineseSimplifiedFontPaths, kBaseFontSize, chineseRanges);
        mergeFirstAvailableFont(io, kChineseTraditionalFontPaths, kBaseFontSize, chineseRanges);
        mergeFirstAvailableFont(io, kKoreanFontPaths, kBaseFontSize, io.Fonts->GetGlyphRangesKorean());
        fullCjkLoaded_ = true;
    }

    // Fonts[1]: header font. CJK is only merged at the base size to save
    // memory, so CJK header text falls back to the scaled base font.
    io.Fonts->AddFontFromFileTTF(kRegularFontPath, kHeaderFontSize, nullptr, kLatinCyrillicRanges);

    addAwluiFonts(io, kBaseFontSize, kLatinCyrillicRanges);

    applyAppTheme();

    io.FontGlobalScale = SettingsCache::instance().get().fontScale;

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = device;
    init_info.QueueFamily = queueFamily;
    init_info.Queue = graphicsQueue;
    init_info.DescriptorPool = descriptorPool;
    init_info.RenderPass = renderPass;
    init_info.MinImageCount = minImageCount;
    init_info.ImageCount = imageCount;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.UseDynamicRendering = false;
    init_info.CheckVkResultFn = [](VkResult result) {
        if (result != VK_SUCCESS) {
            AWOW_LOG_ERROR("ImGui", "Vulkan call failed in the ImGui backend, result=" +
                           std::to_string(static_cast<int>(result)));
        }
    };

    ImGui_ImplVulkan_Init(&init_info);

    return true;
}

void ImGuiManager::newFrame(bool renderMapVisualizer) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Skipped while the landing page or encounter selector is up
    if (renderMapVisualizer && mapVisualizer) {
        mapVisualizer->render();
    }
}

void ImGuiManager::render(VkCommandBuffer commandBuffer) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

bool ImGuiManager::initialize3DRendering(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass,
                                          VkQueue graphicsQueue, VkCommandPool commandPool) {
    if (mapVisualizer) {
        return mapVisualizer->initialize3D(device, physicalDevice, renderPass, graphicsQueue, commandPool);
    }
    return false;
}

bool ImGuiManager::initializeSpellIcons(VkDevice device, VkPhysicalDevice physicalDevice,
                                         VkQueue graphicsQueue, VkCommandPool commandPool) {
    if (mapVisualizer) {
        return mapVisualizer->initializeSpellIcons(device, physicalDevice, graphicsQueue,
                                                    commandPool, descriptorPool);
    }
    return false;
}

void ImGuiManager::render3D(VkDevice device, VkCommandBuffer commandBuffer, VkExtent2D extent) {
    if (mapVisualizer) {
        mapVisualizer->render3D(device, commandBuffer, extent);
    }
}

void ImGuiManager::cleanup(VkDevice device) {
    if (mapVisualizer) {
        mapVisualizer->cleanup3D(device);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    // Destroyed before ImGui's, mirroring the creation order.
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    }
}

void ImGuiManager::loadEventDatabase(const EventDatabase* db) {
    if (mapVisualizer) {
        mapVisualizer->loadEventDatabase(db);
    }
}

void ImGuiManager::loadSpellCastDatabase(const SpellCastDatabase* db) {
    if (mapVisualizer) {
        mapVisualizer->loadSpellCastDatabase(db);
    }
}

bool ImGuiManager::loadSpellGCDData(const std::string& csvPath) {
    if (mapVisualizer) {
        return mapVisualizer->loadSpellGCDData(csvPath);
    }
    return false;
}

void ImGuiManager::loadResourceDatabase(const ResourceDatabase* db) {
    if (mapVisualizer) {
        mapVisualizer->loadResourceDatabase(db);
    }
}

void ImGuiManager::loadCombatDatabase(const CombatDatabase* db) {
    if (mapVisualizer) {
        mapVisualizer->loadCombatDatabase(db);
    }
}

void ImGuiManager::loadDeathDatabase(const DeathDatabase* db) {
    if (mapVisualizer) {
        mapVisualizer->loadDeathDatabase(db);
    }
}

void ImGuiManager::loadAuraDatabase(const AuraDatabase* db) {
    if (mapVisualizer) {
        mapVisualizer->loadAuraDatabase(db);
    }
}

void ImGuiManager::loadDispelDatabase(const DispelDatabase* db) {
    if (mapVisualizer) {
        mapVisualizer->loadDispelDatabase(db);
    }
}

void ImGuiManager::loadAvoidanceDatabase(const AvoidanceDatabase* db) {
    if (mapVisualizer) {
        mapVisualizer->loadAvoidanceDatabase(db);
    }
}

void ImGuiManager::loadEmoteDatabase(const EmoteDatabase* db) {
    if (mapVisualizer) {
        mapVisualizer->loadEmoteDatabase(db);
    }
}

void ImGuiManager::loadCombatantInfo(const std::vector<CombatantInfoEvent>& combatantInfo) {
    if (mapVisualizer) {
        mapVisualizer->loadCombatantInfo(combatantInfo);
    }
}

void ImGuiManager::generateFallbackColors() {
    if (mapVisualizer) {
        mapVisualizer->generateFallbackColors();
    }
}

void ImGuiManager::loadMapChangeEvents(const std::vector<MapChangeEvent>& mapChanges, uint32_t encounterId) {
    if (mapVisualizer) {
        mapVisualizer->loadMapChangeEvents(mapChanges, encounterId);
    }
}

void ImGuiManager::loadSpellNameMap(const std::unordered_map<uint32_t, std::string>& spellNameMap) {
    if (mapVisualizer) {
        mapVisualizer->loadSpellNameMap(spellNameMap);
    }
}

void ImGuiManager::loadGuidToNameMap(const std::unordered_map<std::string, std::string>& guidToNameMap) {
    // Player names may need CJK glyphs the atlas doesn't have yet
    if (!fullCjkLoaded_ && !guidToNameMap.empty()) {
        std::vector<std::string> names;
        names.reserve(guidToNameMap.size());
        for (const auto& [guid, name] : guidToNameMap) {
            names.push_back(name);
        }
        loadCjkFontsForStrings(names);
    }

    if (mapVisualizer) {
        mapVisualizer->loadGuidToNameMap(guidToNameMap);
    }
}

void ImGuiManager::loadActorNpcIdMap(const std::unordered_map<int32_t, uint32_t>& actorIdToNpcId) {
    if (mapVisualizer) {
        mapVisualizer->loadActorNpcIdMap(actorIdToNpcId);
    }
}

void ImGuiManager::loadHealthScalingData(uint64_t avgPlayerHealth,
                                          const std::unordered_map<std::string, uint64_t>& actorMaxHealth) {
    if (mapVisualizer) {
        mapVisualizer->loadHealthScalingData(avgPlayerHealth, actorMaxHealth);
    }
}

void ImGuiManager::setExtractedData(const ExtractedData* data) {
    if (mapVisualizer) {
        mapVisualizer->setExtractedData(data);
    }
}

void ImGuiManager::setExportCallback(std::function<void(
    const ExtractedData*,
    const std::vector<Annotation>&,
    const std::vector<AutoAnnotationTrigger>&,
    const std::vector<CachedAnnotationEvent>&,
    int32_t, int32_t)> callback) {
    if (mapVisualizer) {
        mapVisualizer->setExportCallback(std::move(callback));
    }
}

bool ImGuiManager::shouldReturnToEncounterSelector() const {
    if (mapVisualizer) {
        return mapVisualizer->shouldReturnToEncounterSelector();
    }
    return false;
}

bool ImGuiManager::shouldReturnToLandingPage() const {
    if (mapVisualizer) {
        return mapVisualizer->shouldReturnToLandingPage();
    }
    return false;
}

void ImGuiManager::resetNavigationFlags() {
    if (mapVisualizer) {
        mapVisualizer->resetNavigationFlags();
    }
}

bool ImGuiManager::shouldRefreshEncounters() const {
    if (mapVisualizer) {
        return mapVisualizer->shouldRefreshEncounters();
    }
    return false;
}

void ImGuiManager::resetRefreshFlag() {
    if (mapVisualizer) {
        mapVisualizer->resetRefreshFlag();
    }
}

void ImGuiManager::unloadDatabases() {
    if (mapVisualizer) {
        mapVisualizer->unloadDatabases();
    }
}

SpellIconLoader* ImGuiManager::getSpellIconLoader() {
    return mapVisualizer ? mapVisualizer->getSpellIconLoader() : nullptr;
}

AutoAnnotationTriggerManager* ImGuiManager::getAutoAnnotationTriggerManager() {
    return mapVisualizer ? mapVisualizer->getAutoAnnotationTriggerManager() : nullptr;
}

void ImGuiManager::processTextureCleanup() {
    if (mapVisualizer) {
        mapVisualizer->processTextureCleanup();
    }
}

bool ImGuiManager::isCjkLocale(const std::string& locale) {
    return locale.starts_with("zh_") ||  // zh_CN, zh_TW, zh_HK
           locale.starts_with("ko_") ||  // ko_KR
           locale.starts_with("ja_");    // ja_JP
}

std::set<uint32_t> ImGuiManager::scanForCjkCharacters(const std::vector<std::string>& strings) {
    std::set<uint32_t> cjkChars;

    for (const auto& str : strings) {
        size_t i = 0;
        while (i < str.size()) {
            uint32_t codepoint = 0;
            unsigned char c = str[i];

            if ((c & 0x80) == 0) {
                codepoint = c;
                i += 1;
            } else if ((c & 0xE0) == 0xC0) {
                if (i + 1 < str.size()) {
                    codepoint = ((c & 0x1F) << 6) | (str[i + 1] & 0x3F);
                }
                i += 2;
            } else if ((c & 0xF0) == 0xE0) {
                // 3-byte sequences cover the CJK ranges
                if (i + 2 < str.size()) {
                    codepoint = ((c & 0x0F) << 12) |
                                ((str[i + 1] & 0x3F) << 6) |
                                (str[i + 2] & 0x3F);
                }
                i += 3;
            } else if ((c & 0xF8) == 0xF0) {
                if (i + 3 < str.size()) {
                    codepoint = ((c & 0x07) << 18) |
                                ((str[i + 1] & 0x3F) << 12) |
                                ((str[i + 2] & 0x3F) << 6) |
                                (str[i + 3] & 0x3F);
                }
                i += 4;
            } else {
                i += 1; // Invalid, skip
            }

            if ((codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||  // CJK Unified
                (codepoint >= 0x3400 && codepoint <= 0x4DBF) ||  // CJK Extension A
                (codepoint >= 0xAC00 && codepoint <= 0xD7A3) ||  // Hangul
                (codepoint >= 0x3040 && codepoint <= 0x309F) ||  // Hiragana
                (codepoint >= 0x30A0 && codepoint <= 0x30FF)) {  // Katakana
                cjkChars.insert(codepoint);
            }
        }
    }

    return cjkChars;
}

bool ImGuiManager::loadCjkFontsForStrings(const std::vector<std::string>& strings) {
    // A CJK locale already loaded the full ranges at startup
    if (fullCjkLoaded_) {
        return false;
    }

    auto neededChars = scanForCjkCharacters(strings);
    if (neededChars.empty()) {
        return false;
    }

    bool allLoaded = true;
    for (uint32_t cp : neededChars) {
        if (loadedCjkCodepoints_.find(cp) == loadedCjkCodepoints_.end()) {
            allLoaded = false;
            break;
        }
    }

    if (allLoaded) {
        return false;
    }

    for (uint32_t cp : neededChars) {
        loadedCjkCodepoints_.insert(cp);
    }

    rebuildFontAtlas(loadedCjkCodepoints_);
    return true;
}

void ImGuiManager::rebuildFontAtlas(const std::set<uint32_t>& cjkCodepoints) {
    if (device_ == VK_NULL_HANDLE) {
        AWOW_LOG_WARNING("ImGui", "Cannot rebuild font atlas - no Vulkan device");
        return;
    }

    // The font texture may still be in use by an in-flight frame
    vkDeviceWaitIdle(device_);

    ImGuiIO& io = ImGui::GetIO();

    io.Fonts->Clear();

    std::vector<ImWchar> customCjkRange;
    if (!cjkCodepoints.empty()) {
        // ImGui ranges are [start, end] pairs terminated by 0; sparse
        // codepoints each get their own [cp, cp] pair
        for (uint32_t cp : cjkCodepoints) {
            customCjkRange.push_back(static_cast<ImWchar>(cp));
            customCjkRange.push_back(static_cast<ImWchar>(cp));
        }
        customCjkRange.push_back(0);
    }

    if (!io.Fonts->AddFontFromFileTTF(kRegularFontPath, kBaseFontSize, nullptr, kLatinCyrillicRanges)) {
        AWOW_LOG_WARNING("ImGui", "Could not load NotoSans-Regular.ttf, falling back to default font");
        io.Fonts->AddFontDefault();
    }

    if (!customCjkRange.empty()) {
        // The Chinese font covers most CJK; the Korean one adds Hangul
        mergeFirstAvailableFont(io, kChineseSimplifiedFontPaths, kBaseFontSize, customCjkRange.data());
        mergeFirstAvailableFont(io, kKoreanFontPaths, kBaseFontSize, customCjkRange.data());
    }

    io.Fonts->AddFontFromFileTTF(kRegularFontPath, kHeaderFontSize, nullptr, kLatinCyrillicRanges);

    // Clear() above freed the Medium and Mono fonts awlui points at
    addAwluiFonts(io, kBaseFontSize, kLatinCyrillicRanges);

    io.Fonts->Build();

    ImGui_ImplVulkan_DestroyFontsTexture();
    ImGui_ImplVulkan_CreateFontsTexture();
}
