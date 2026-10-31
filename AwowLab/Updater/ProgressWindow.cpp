#include "ProgressWindow.h"
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

namespace awow::updater {

static const wchar_t* WINDOW_CLASS_NAME = L"AwowLabUpdaterWindow";

ProgressWindow::ProgressWindow() {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);
}

ProgressWindow::~ProgressWindow() {
    close();
}

void ProgressWindow::registerWindowClass() {
    if (windowClassRegistered_) return;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);

    if (RegisterClassExW(&wc)) {
        windowClassRegistered_ = true;
    }
}

LRESULT CALLBACK ProgressWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ProgressWindow* pThis = nullptr;

    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = static_cast<ProgressWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<ProgressWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        switch (msg) {
            case WM_COMMAND:
                if (LOWORD(wParam) == ID_CANCEL_BUTTON) {
                    pThis->cancelled_ = true;
                }
                break;

            case WM_CLOSE:
                pThis->cancelled_ = true;
                return 0;

            case WM_DESTROY:
                return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ProgressWindow::create(const std::string& title) {
    registerWindowClass();

    std::wstring wideTitle(title.begin(), title.end());

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - WINDOW_WIDTH) / 2;
    int y = (screenHeight - WINDOW_HEIGHT) / 2;

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST,
        WINDOW_CLASS_NAME,
        wideTitle.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, WINDOW_WIDTH, WINDOW_HEIGHT,
        nullptr, nullptr, GetModuleHandle(nullptr), this
    );

    if (!hwnd_) {
        return false;
    }

    statusLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        L"Checking for updates...",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 20, WINDOW_WIDTH - 40, 25,
        static_cast<HWND>(hwnd_), nullptr, GetModuleHandle(nullptr), nullptr
    );

    progressBar_ = CreateWindowExW(
        0,
        PROGRESS_CLASSW,
        nullptr,
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        20, 55, WINDOW_WIDTH - 40, 25,
        static_cast<HWND>(hwnd_), nullptr, GetModuleHandle(nullptr), nullptr
    );

    // 0-1000 rather than 0-100 so the bar moves smoothly.
    SendMessage(static_cast<HWND>(progressBar_), PBM_SETRANGE32, 0, 1000);

    cancelButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Cancel",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        (WINDOW_WIDTH - 80) / 2, 95, 80, 30,
        static_cast<HWND>(hwnd_),
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CANCEL_BUTTON)),
        GetModuleHandle(nullptr), nullptr
    );

    HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessage(static_cast<HWND>(statusLabel_), WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(static_cast<HWND>(cancelButton_), WM_SETFONT, (WPARAM)hFont, TRUE);

    ShowWindow(static_cast<HWND>(hwnd_), SW_SHOW);
    UpdateWindow(static_cast<HWND>(hwnd_));

    return true;
}

void ProgressWindow::setProgress(float progress, const std::string& status) {
    if (!hwnd_) return;

    int pos = static_cast<int>(progress * 1000.0f);
    pos = (std::max)(0, (std::min)(1000, pos));
    SendMessage(static_cast<HWND>(progressBar_), PBM_SETPOS, pos, 0);

    std::wstring wideStatus(status.begin(), status.end());
    SetWindowTextW(static_cast<HWND>(statusLabel_), wideStatus.c_str());

    UpdateWindow(static_cast<HWND>(hwnd_));
}

bool ProgressWindow::processMessages() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            cancelled_ = true;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return !cancelled_;
}

void ProgressWindow::close() {
    if (hwnd_) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
        progressBar_ = nullptr;
        statusLabel_ = nullptr;
        cancelButton_ = nullptr;
    }
}

void ProgressWindow::showMessage(const std::string& message,
                                  const std::string& title,
                                  bool isError) {
    std::wstring wideMessage(message.begin(), message.end());
    std::wstring wideTitle(title.begin(), title.end());

    UINT flags = MB_OK | (isError ? MB_ICONERROR : MB_ICONINFORMATION);
    MessageBoxW(static_cast<HWND>(hwnd_), wideMessage.c_str(), wideTitle.c_str(), flags);
}

bool ProgressWindow::askYesNo(const std::string& message, const std::string& title) {
    std::wstring wideMessage(message.begin(), message.end());
    std::wstring wideTitle(title.begin(), title.end());

    int result = MessageBoxW(static_cast<HWND>(hwnd_), wideMessage.c_str(),
                             wideTitle.c_str(), MB_YESNO | MB_ICONQUESTION);
    return result == IDYES;
}

} // namespace awow::updater
