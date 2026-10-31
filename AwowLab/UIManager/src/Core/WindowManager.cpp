#include "Core/WindowManager.h"
#include "Core/ErrorLogger.h"
#include "UI/UISettings.h"
#include "Core/Utils/ExecutablePath.h"
#include <GLFW/glfw3.h>

// For loading window icon (implementation in VulkanTextureManager.cpp)
#include "stb_image.h"

WindowManager::WindowManager(int width, int height, const std::string& title)
    : window(nullptr)
    , width(width)
    , height(height)
    , title(title)
    , framebufferResized(false)
{
}

WindowManager::~WindowManager() {
    saveWindowSize();
    cleanup();
}

// GLFW error callback - logs errors to our error system
static void glfwErrorCallback(int error, const char* description) {
    AWOW_LOG_ERROR("GLFW", "Error " + std::to_string(error) + ": " + std::string(description));
}

bool WindowManager::initialize() {
    // Set before glfwInit so init failures are reported too
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        AWOW_LOG_ERROR("GLFW", "Failed to initialize GLFW - windowing system initialization failed");
        return false;
    }

    if (glfwVulkanSupported()) {
        AWOW_LOG_INFO("GLFW", "Vulkan support: YES");
    } else {
        AWOW_LOG_ERROR("GLFW", "Vulkan support: NO - vulkan-1.dll may be missing or GPU drivers need update");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // Vulkan, so no OpenGL context
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window) {
        AWOW_LOG_ERROR("GLFW", "Failed to create GLFW window (" + std::to_string(width) + "x" + std::to_string(height) + ")");
        glfwTerminate();
        return false;
    }

    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    loadWindowIcon();

    return true;
}

bool WindowManager::shouldClose() const {
    return glfwWindowShouldClose(window);
}

void WindowManager::pollEvents() const {
    glfwPollEvents();
}

void WindowManager::setWindowTitle(const std::string& newTitle) {
    title = newTitle;
    if (window) {
        glfwSetWindowTitle(window, title.c_str());
    }
}

void WindowManager::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto manager = reinterpret_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (manager) {
        manager->framebufferResized = true;
        manager->width = width;
        manager->height = height;
    }
}

void WindowManager::cleanup() {
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
}

void WindowManager::saveWindowSize() {
    if (!window) return;

    // Window size, not framebuffer size, which differs on high-DPI displays
    int windowWidth, windowHeight;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);

    bool isMaximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) != 0;

    // A minimized window reports 0x0
    if (windowWidth > 0 && windowHeight > 0) {
        WindowSizeSettings settings;
        settings.width = windowWidth;
        settings.height = windowHeight;
        settings.maximized = isMaximized;
        settings.saveToFile();
    }
}

void WindowManager::loadWindowIcon() {
    if (!window) return;

    std::string iconPath = awow::getExecutableDirectory() + "AwowLab_icon.png";

    int iconWidth, iconHeight, channels;
    unsigned char* pixels = stbi_load(iconPath.c_str(), &iconWidth, &iconHeight, &channels, 4);

    if (pixels) {
        GLFWimage icon;
        icon.width = iconWidth;
        icon.height = iconHeight;
        icon.pixels = pixels;

        glfwSetWindowIcon(window, 1, &icon);
        stbi_image_free(pixels);
    } else {
        AWOW_LOG_WARNING("ICON", "Failed to load window icon from: " + iconPath + " - using default icon");
    }
}
