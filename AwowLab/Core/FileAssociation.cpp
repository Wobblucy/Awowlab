#include "FileAssociation.h"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>  // For SHChangeNotify
#endif

namespace awow {
namespace FileAssociation {

// Registry path constants
static const wchar_t* EXTENSION_KEY = L"Software\\Classes\\.awsnap";
static const wchar_t* PROG_ID = L"AwowLab.Snapshot";
static const wchar_t* PROG_ID_KEY = L"Software\\Classes\\AwowLab.Snapshot";
static const wchar_t* PROG_ID_ICON_KEY = L"Software\\Classes\\AwowLab.Snapshot\\DefaultIcon";
static const wchar_t* PROG_ID_COMMAND_KEY = L"Software\\Classes\\AwowLab.Snapshot\\shell\\open\\command";
static const wchar_t* DESCRIPTION = L"AwowLab Combat Snapshot";

#ifdef _WIN32

std::string getExecutablePath() {
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::string(path);
    }
    return "";
}

static std::wstring getExecutablePathW() {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::wstring(path);
    }
    return L"";
}

static bool setRegistryStringValue(HKEY hRootKey, const wchar_t* keyPath, const wchar_t* valueName, const std::wstring& value) {
    HKEY hKey;
    LONG result = RegCreateKeyExW(
        hRootKey,
        keyPath,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        nullptr,
        &hKey,
        nullptr
    );

    if (result != ERROR_SUCCESS) {
        return false;
    }

    result = RegSetValueExW(
        hKey,
        valueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.length() + 1) * sizeof(wchar_t))
    );

    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

static bool getRegistryStringValue(HKEY hRootKey, const wchar_t* keyPath, const wchar_t* valueName, std::wstring& value) {
    HKEY hKey;
    LONG result = RegOpenKeyExW(
        hRootKey,
        keyPath,
        0,
        KEY_READ,
        &hKey
    );

    if (result != ERROR_SUCCESS) {
        return false;
    }

    wchar_t buffer[MAX_PATH * 2];
    DWORD bufferSize = sizeof(buffer);
    DWORD dataType;

    result = RegQueryValueExW(
        hKey,
        valueName,
        nullptr,
        &dataType,
        reinterpret_cast<BYTE*>(buffer),
        &bufferSize
    );

    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS || dataType != REG_SZ) {
        return false;
    }

    value = buffer;
    return true;
}

static void deleteRegistryKey(HKEY hRootKey, const wchar_t* keyPath) {
    RegDeleteTreeW(hRootKey, keyPath);
}

bool isRegistered() {
    std::wstring progId;
    if (!getRegistryStringValue(HKEY_CURRENT_USER, EXTENSION_KEY, nullptr, progId)) {
        return false;
    }

    // Check if it points to our ProgID
    if (progId != PROG_ID) {
        return false;
    }

    // Verify command points to our executable
    std::wstring command;
    if (!getRegistryStringValue(HKEY_CURRENT_USER, PROG_ID_COMMAND_KEY, nullptr, command)) {
        return false;
    }

    std::wstring exePath = getExecutablePathW();
    if (exePath.empty()) {
        return false;
    }

    // Check if the command contains our executable path
    return command.find(exePath) != std::wstring::npos;
}

bool registerAssociation() {
    std::wstring exePath = getExecutablePathW();
    if (exePath.empty()) {
        return false;
    }

    // 1. Set .awsnap extension to point to our ProgID
    if (!setRegistryStringValue(HKEY_CURRENT_USER, EXTENSION_KEY, nullptr, PROG_ID)) {
        return false;
    }

    // 2. Create ProgID with description
    if (!setRegistryStringValue(HKEY_CURRENT_USER, PROG_ID_KEY, nullptr, DESCRIPTION)) {
        return false;
    }

    // 3. Set default icon (use first icon from executable)
    std::wstring iconPath = exePath + L",0";
    if (!setRegistryStringValue(HKEY_CURRENT_USER, PROG_ID_ICON_KEY, nullptr, iconPath)) {
        return false;
    }

    // 4. Set open command
    std::wstring command = L"\"" + exePath + L"\" \"%1\"";
    if (!setRegistryStringValue(HKEY_CURRENT_USER, PROG_ID_COMMAND_KEY, nullptr, command)) {
        return false;
    }

    // Notify shell of association change
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    return true;
}

bool unregisterAssociation() {
    // Delete our keys (order matters - delete children first)
    deleteRegistryKey(HKEY_CURRENT_USER, PROG_ID_KEY);
    deleteRegistryKey(HKEY_CURRENT_USER, EXTENSION_KEY);

    // Notify shell of association change
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    return true;
}

#else

// Non-Windows stubs
std::string getExecutablePath() {
    return "";
}

bool isRegistered() {
    return true;  // Pretend registered on non-Windows
}

bool registerAssociation() {
    return true;  // No-op on non-Windows
}

bool unregisterAssociation() {
    return true;  // No-op on non-Windows
}

#endif

} // namespace FileAssociation
} // namespace awow
