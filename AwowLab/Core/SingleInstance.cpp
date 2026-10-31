#include "SingleInstance.h"

#ifdef _WIN32
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <string>
#include <mutex>
#include <queue>

namespace awow {

// Unique identifier for WM_COPYDATA messages
static const ULONG_PTR AWOWLAB_COPYDATA_ID = 0x41574F57;  // "AWOW"

// Named mutex for single-instance detection
static const wchar_t* MUTEX_NAME = L"AwowLab_SingleInstance_Mutex";

// Window title to search for (partial match)
static const wchar_t* WINDOW_TITLE_PREFIX = L"AwowLab";

class SingleInstance::Impl {
public:
    HANDLE mutex_ = nullptr;
    HWND hwnd_ = nullptr;
    WNDPROC originalWndProc_ = nullptr;
    std::function<void(const std::string&)> fileReceivedCallback_;
    std::queue<std::string> pendingFiles_;
    std::mutex queueMutex_;

    static Impl* instance_;  // For window proc callback

    ~Impl() {
        if (mutex_) {
            CloseHandle(mutex_);
            mutex_ = nullptr;
        }
        // Restore original window proc if we subclassed
        if (hwnd_ && originalWndProc_) {
            SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWndProc_));
        }
        instance_ = nullptr;
    }

    bool tryAcquire() {
        mutex_ = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
        if (mutex_ == nullptr) {
            return false;
        }
        DWORD lastError = GetLastError();
        if (lastError == ERROR_ALREADY_EXISTS) {
            // Another instance has the mutex
            CloseHandle(mutex_);
            mutex_ = nullptr;
            return false;
        }
        return true;
    }

    static bool isAnotherInstanceRunning() {
        HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, MUTEX_NAME);
        if (mutex != nullptr) {
            CloseHandle(mutex);
            return true;
        }
        return false;
    }

    static HWND findExistingWindow() {
        // EnumWindows callback data
        struct EnumData {
            HWND foundHwnd = nullptr;
        } data;

        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* data = reinterpret_cast<EnumData*>(lParam);

            wchar_t title[256];
            if (GetWindowTextW(hwnd, title, 256) > 0) {
                if (wcsstr(title, WINDOW_TITLE_PREFIX) != nullptr) {
                    // Check if window is visible
                    if (IsWindowVisible(hwnd)) {
                        // Make sure it's a real app window, not a console
                        // GLFW windows have the "GLFW30" class name
                        wchar_t className[256];
                        if (GetClassNameW(hwnd, className, 256) > 0) {
                            if (wcsstr(className, L"GLFW") != nullptr) {
                                data->foundHwnd = hwnd;
                                return FALSE;  // Stop enumeration
                            }
                        }
                    }
                }
            }
            return TRUE;  // Continue enumeration
        }, reinterpret_cast<LPARAM>(&data));

        return data.foundHwnd;
    }

    static bool sendFileToExistingInstance(const std::string& filePath) {
        HWND existingHwnd = findExistingWindow();
        if (existingHwnd == nullptr) {
            return false;
        }

        // Bring window to foreground
        SetForegroundWindow(existingHwnd);
        if (IsIconic(existingHwnd)) {
            ShowWindow(existingHwnd, SW_RESTORE);
        }

        // Send file path via WM_COPYDATA
        COPYDATASTRUCT cds;
        cds.dwData = AWOWLAB_COPYDATA_ID;
        cds.cbData = static_cast<DWORD>(filePath.size() + 1);
        cds.lpData = const_cast<char*>(filePath.c_str());

        // Use SendMessageTimeout to avoid hanging if the other window is unresponsive
        DWORD_PTR result;
        LRESULT sent = SendMessageTimeoutW(
            existingHwnd,
            WM_COPYDATA,
            0,
            reinterpret_cast<LPARAM>(&cds),
            SMTO_ABORTIFHUNG,
            3000,  // 3 second timeout
            &result
        );

        return sent != 0;
    }

    void handleCopyData(COPYDATASTRUCT* cds) {
        if (cds->dwData != AWOWLAB_COPYDATA_ID) {
            return;
        }

        if (cds->lpData != nullptr && cds->cbData > 0) {
            std::string filePath(static_cast<const char*>(cds->lpData), cds->cbData - 1);

            std::lock_guard<std::mutex> lock(queueMutex_);
            pendingFiles_.push(filePath);

            if (fileReceivedCallback_) {
                fileReceivedCallback_(filePath);
            }
        }
    }

    static LRESULT CALLBACK subclassedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_COPYDATA && instance_ != nullptr) {
            COPYDATASTRUCT* cds = reinterpret_cast<COPYDATASTRUCT*>(lParam);
            instance_->handleCopyData(cds);
            return TRUE;  // Message handled
        }

        // Call original window proc
        if (instance_ && instance_->originalWndProc_) {
            return CallWindowProcW(instance_->originalWndProc_, hwnd, msg, wParam, lParam);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void installWindowHook(void* glfwWindow) {
        if (glfwWindow == nullptr) {
            return;
        }

        hwnd_ = glfwGetWin32Window(static_cast<GLFWwindow*>(glfwWindow));
        if (hwnd_ == nullptr) {
            return;
        }

        instance_ = this;

        // Subclass the window to intercept WM_COPYDATA
        originalWndProc_ = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(subclassedWndProc))
        );
    }

    std::string getAndClearPendingFile() {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (pendingFiles_.empty()) {
            return "";
        }
        std::string file = pendingFiles_.front();
        pendingFiles_.pop();
        return file;
    }

    bool hasPendingFile() const {
        // Unlocked read; a stale answer only delays the poll by a frame
        return !pendingFiles_.empty();
    }
};

SingleInstance::Impl* SingleInstance::Impl::instance_ = nullptr;

// Public interface implementation (Windows)

SingleInstance::SingleInstance() : pImpl_(new Impl()) {}

SingleInstance::~SingleInstance() {
    delete pImpl_;
}

bool SingleInstance::tryAcquire() {
    return pImpl_->tryAcquire();
}

bool SingleInstance::isAnotherInstanceRunning() {
    return Impl::isAnotherInstanceRunning();
}

bool SingleInstance::sendFileToExistingInstance(const std::string& filePath) {
    return Impl::sendFileToExistingInstance(filePath);
}

void SingleInstance::setFileReceivedCallback(std::function<void(const std::string&)> callback) {
    pImpl_->fileReceivedCallback_ = std::move(callback);
}

void SingleInstance::installWindowHook(void* glfwWindow) {
    pImpl_->installWindowHook(glfwWindow);
}

void SingleInstance::processPendingFiles() {
    // The callback is called synchronously in handleCopyData, so this is a no-op
    // But we keep it for potential future use or if we want to batch process
}

std::string SingleInstance::getAndClearPendingFile() {
    return pImpl_->getAndClearPendingFile();
}

bool SingleInstance::hasPendingFile() const {
    return pImpl_->hasPendingFile();
}

} // namespace awow

#else

// Non-Windows stub implementation
namespace awow {

class SingleInstance::Impl {
public:
    bool tryAcquire() { return true; }
    static bool isAnotherInstanceRunning() { return false; }
    static bool sendFileToExistingInstance(const std::string&) { return false; }
    void installWindowHook(void*) {}
    std::string getAndClearPendingFile() { return ""; }
    bool hasPendingFile() const { return false; }
    std::function<void(const std::string&)> fileReceivedCallback_;
};

SingleInstance::SingleInstance() : pImpl_(new Impl()) {}
SingleInstance::~SingleInstance() { delete pImpl_; }
bool SingleInstance::tryAcquire() { return pImpl_->tryAcquire(); }
bool SingleInstance::isAnotherInstanceRunning() { return Impl::isAnotherInstanceRunning(); }
bool SingleInstance::sendFileToExistingInstance(const std::string& filePath) { return Impl::sendFileToExistingInstance(filePath); }
void SingleInstance::setFileReceivedCallback(std::function<void(const std::string&)> callback) { pImpl_->fileReceivedCallback_ = std::move(callback); }
void SingleInstance::installWindowHook(void*) {}
void SingleInstance::processPendingFiles() {}
std::string SingleInstance::getAndClearPendingFile() { return pImpl_->getAndClearPendingFile(); }
bool SingleInstance::hasPendingFile() const { return pImpl_->hasPendingFile(); }

} // namespace awow

#endif
