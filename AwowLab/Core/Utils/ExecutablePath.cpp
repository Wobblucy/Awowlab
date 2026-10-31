#include "ExecutablePath.h"

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#else
    #include <unistd.h>
    #include <limits.h>
#endif

namespace awow {

std::filesystem::path getExecutableDirectoryPath() {
#if defined(_WIN32) || defined(_WIN64)
    // Wide API so an install folder outside the ANSI code page survives, and
    // a growing buffer so one deeper than MAX_PATH is not cut short.
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        DWORD len = GetModuleFileNameW(NULL, path.data(), static_cast<DWORD>(path.size()));
        if (len == 0) {
            return {};
        }
        if (len < path.size()) {
            path.resize(len);
            break;
        }
        if (path.size() >= 32768) {
            return {};
        }
        path.resize(path.size() * 2);
    }
    return std::filesystem::path(path).parent_path();
#else
    char path[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
    if (count != -1) {
        return std::filesystem::path(std::string(path, count)).parent_path();
    }
    return {};
#endif
}

std::string getExecutableDirectory() {
    std::filesystem::path dir = getExecutableDirectoryPath();
    if (dir.empty()) {
        return "";
    }
#if defined(_WIN32) || defined(_WIN64)
    // Callers open files through narrow-string APIs, which read the string in
    // the ANSI code page. When that code page cannot spell the folder the
    // converted path would hold '?' and open nothing, so hand back a relative
    // path instead: main() makes this folder the working directory at startup.
    const std::wstring& wide = dir.native();
    const UINT codePage = GetACP();
    BOOL lossy = FALSE;
    // CP_UTF8 rejects the lossy-conversion flag and out parameter
    const DWORD flags = (codePage == CP_UTF8) ? 0 : WC_NO_BEST_FIT_CHARS;
    BOOL* lossyOut = (codePage == CP_UTF8) ? nullptr : &lossy;
    int size = WideCharToMultiByte(codePage, flags, wide.c_str(), static_cast<int>(wide.size()),
                                   nullptr, 0, nullptr, lossyOut);
    if (size <= 0 || lossy) {
        return ".\\";
    }
    std::string narrow(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(codePage, flags, wide.c_str(), static_cast<int>(wide.size()),
                        narrow.data(), size, nullptr, nullptr);
    if (narrow.back() != '\\') {
        narrow += '\\';
    }
    return narrow;
#else
    return dir.string() + "/";
#endif
}

} // namespace awow
