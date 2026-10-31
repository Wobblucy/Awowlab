#pragma once

#include <string>
#include <functional>

namespace awow {

/**
 * Single-instance support for AwowLab.
 *
 * Uses a named mutex to detect if another instance is running.
 * Uses WM_COPYDATA to send file paths between instances.
 */
class SingleInstance {
public:
    SingleInstance();
    ~SingleInstance();

    // Non-copyable
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    /**
     * Try to acquire the single-instance lock.
     * @return true if this is the first instance, false if another instance is running
     */
    bool tryAcquire();

    /**
     * Check if another instance is running (without acquiring the lock).
     */
    static bool isAnotherInstanceRunning();

    /**
     * Send a file path to the existing instance.
     * @param filePath The file path to send
     * @return true if the message was sent successfully
     */
    static bool sendFileToExistingInstance(const std::string& filePath);

    /**
     * Set callback for when a file is received from another instance.
     * @param callback Function to call with the received file path
     */
    void setFileReceivedCallback(std::function<void(const std::string&)> callback);

    /**
     * Install the window message hook on the GLFW window.
     * Must be called after GLFW window is created.
     * @param glfwWindow The GLFW window handle (GLFWwindow*)
     */
    void installWindowHook(void* glfwWindow);

    /**
     * Process any pending file requests.
     * Call this from the main loop to check for incoming files.
     */
    void processPendingFiles();

    /**
     * Get the pending file path (if any) and clear it.
     * @return The file path, or empty string if none pending
     */
    std::string getAndClearPendingFile();

    /**
     * Check if there's a pending file.
     */
    bool hasPendingFile() const;

private:
    class Impl;
    Impl* pImpl_;
};

} // namespace awow
