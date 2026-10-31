#pragma once
#include <string>

// Forward declare GLFWwindow to avoid including GLFW headers in the header file
struct GLFWwindow;

class WindowManager {
public:
    WindowManager(int width, int height, const std::string& title);
    ~WindowManager();

    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    // Initialize GLFW and create window
    bool initialize();

    // Main loop control
    bool shouldClose() const;
    void pollEvents() const;

    // Getters
    GLFWwindow* getWindow() const { return window; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }

    // Setters
    void setWindowTitle(const std::string& newTitle);

    // Check if framebuffer was resized
    bool wasResized() const { return framebufferResized; }
    void resetResizedFlag() { framebufferResized = false; }

    // Callbacks
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

private:
    GLFWwindow* window;
    int width;
    int height;
    std::string title;
    bool framebufferResized;

    void cleanup();
    void saveWindowSize();
    void loadWindowIcon();
};
