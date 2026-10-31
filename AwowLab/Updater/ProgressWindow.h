#pragma once

#include <string>
#include <Windows.h>

namespace awow::updater {

// Plain Win32 window with a status line, a progress bar and a Cancel button.
class ProgressWindow {
public:
    ProgressWindow();
    ~ProgressWindow();

    ProgressWindow(const ProgressWindow&) = delete;
    ProgressWindow& operator=(const ProgressWindow&) = delete;

    bool create(const std::string& title = "AwowLab Updater");

    // progress runs from 0.0 to 1.0.
    void setProgress(float progress, const std::string& status);

    // Pumps pending window messages; call it regularly during long work.
    // Returns false once the window is closed or WM_QUIT arrives.
    bool processMessages();

    bool isCancelled() const { return cancelled_; }

    void close();

    void showMessage(const std::string& message,
                     const std::string& title = "AwowLab Updater",
                     bool isError = false);

    bool askYesNo(const std::string& message,
                  const std::string& title = "AwowLab Updater");

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void registerWindowClass();

    HWND hwnd_ = nullptr;
    HWND progressBar_ = nullptr;
    HWND statusLabel_ = nullptr;
    HWND cancelButton_ = nullptr;
    bool cancelled_ = false;
    bool windowClassRegistered_ = false;

    static constexpr int WINDOW_WIDTH = 450;
    static constexpr int WINDOW_HEIGHT = 150;
    static constexpr int ID_CANCEL_BUTTON = 1001;
};

} // namespace awow::updater
