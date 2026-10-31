#include "AwowLab.h"
#include "Core/Application.h"
#include "Core/SingleInstance.h"
#include "Core/ErrorLogger.h"
#include "Core/Utils/ExecutablePath.h"
#include "Core/Utils/FileNameUtils.h"

#include <string>
#include <filesystem>
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>  // For CommandLineToArgvW
#if !defined(NDEBUG)
#include <dbghelp.h>   // For MiniDumpWriteDump (debug builds only)
#pragma comment(lib, "dbghelp.lib")
#endif
#endif

namespace {

// A double-clicked .awsnap starts us with the snapshot's folder as the
// working directory; data files are looked up next to the executable.
void setWorkingDirectoryToExe() {
#ifdef _WIN32
    // Wide path: the ANSI one cannot reach a folder outside the code page
    std::filesystem::path exeDir = awow::getExecutableDirectoryPath();
    if (!exeDir.empty()) {
        SetCurrentDirectoryW(exeDir.c_str());
    }
#endif
}

// path itself if it names an existing .awsnap file, otherwise empty
std::string existingSnapshotOrEmpty(std::string path) {
    if (awow::files::isSnapshotPath(path) && std::filesystem::exists(path)) {
        return path;
    }
    return "";
}

// The .awsnap path from argv[1], or empty if there is none or it does not exist
std::string parseSnapshotPath(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        return "";
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return "";
    }

    std::string path(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, path.data(), size, nullptr, nullptr);
    return existingSnapshotOrEmpty(std::move(path));
}

std::string parseSnapshotPath(int argc, char* argv[]) {
    if (argc < 2) {
        return "";
    }
    return existingSnapshotOrEmpty(argv[1]);
}

#if defined(_WIN32) && !defined(NDEBUG)
// Logs structured exceptions (access violations and the like) and writes a
// minidump. Debug builds only, to keep release builds from tripping
// antivirus false positives.
LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* exceptionInfo) {
    const char* exceptionType = "Unknown";
    DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;

    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:      exceptionType = "Access Violation"; break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: exceptionType = "Array Bounds Exceeded"; break;
        case EXCEPTION_BREAKPOINT:            exceptionType = "Breakpoint"; break;
        case EXCEPTION_DATATYPE_MISALIGNMENT: exceptionType = "Datatype Misalignment"; break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:  exceptionType = "Float Denormal Operand"; break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:    exceptionType = "Float Divide by Zero"; break;
        case EXCEPTION_FLT_INEXACT_RESULT:    exceptionType = "Float Inexact Result"; break;
        case EXCEPTION_FLT_INVALID_OPERATION: exceptionType = "Float Invalid Operation"; break;
        case EXCEPTION_FLT_OVERFLOW:          exceptionType = "Float Overflow"; break;
        case EXCEPTION_FLT_STACK_CHECK:       exceptionType = "Float Stack Check"; break;
        case EXCEPTION_FLT_UNDERFLOW:         exceptionType = "Float Underflow"; break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:   exceptionType = "Illegal Instruction"; break;
        case EXCEPTION_IN_PAGE_ERROR:         exceptionType = "In Page Error"; break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    exceptionType = "Integer Divide by Zero"; break;
        case EXCEPTION_INT_OVERFLOW:          exceptionType = "Integer Overflow"; break;
        case EXCEPTION_INVALID_DISPOSITION:   exceptionType = "Invalid Disposition"; break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: exceptionType = "Noncontinuable Exception"; break;
        case EXCEPTION_PRIV_INSTRUCTION:      exceptionType = "Privileged Instruction"; break;
        case EXCEPTION_SINGLE_STEP:           exceptionType = "Single Step"; break;
        case EXCEPTION_STACK_OVERFLOW:        exceptionType = "Stack Overflow"; break;
        default: break;
    }

    char crashMsg[512];
    snprintf(crashMsg, sizeof(crashMsg),
        "Windows Exception: %s (0x%08lX) at address 0x%p",
        exceptionType, code, exceptionInfo->ExceptionRecord->ExceptionAddress);

    ErrorLogger::instance().logCrashUnsafe(crashMsg);

    try {
        char dumpPath[MAX_PATH];
        GetModuleFileNameA(NULL, dumpPath, MAX_PATH);
        std::string dumpFile = std::string(dumpPath);
        size_t lastSlash = dumpFile.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            dumpFile = dumpFile.substr(0, lastSlash + 1) + "awowlab_crash.dmp";
        }

        HANDLE hFile = CreateFileA(dumpFile.c_str(), GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mdei;
            mdei.ThreadId = GetCurrentThreadId();
            mdei.ExceptionPointers = exceptionInfo;
            mdei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                hFile, MiniDumpNormal, &mdei, NULL, NULL);
            CloseHandle(hFile);
        }
    } catch (...) {
    }

    return EXCEPTION_CONTINUE_SEARCH;  // Let default handler terminate the process
}

void initializeCrashHandler() {
    SetUnhandledExceptionFilter(UnhandledExceptionHandler);
}
#endif

}  // namespace

#if defined(_WIN32) && defined(NDEBUG)
// Release builds use WinMain so no console window opens

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    setWorkingDirectoryToExe();

    ErrorLogger::instance().initialize(std::filesystem::current_path());

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string snapshotPath;
    if (argv) {
        snapshotPath = parseSnapshotPath(argc, argv);
        LocalFree(argv);
    }

    if (!snapshotPath.empty() && awow::SingleInstance::isAnotherInstanceRunning()) {
        awow::SingleInstance::sendFileToExistingInstance(snapshotPath);
        return 0;
    }

    try {
        // 0x0 lets Application use the saved size, or 80% of the screen
        Application app(0, 0, "AwowLab - Combat Log Visualizer");

        if (!snapshotPath.empty()) {
            app.setInitialSnapshot(snapshotPath);
        }

        return app.run();
    }
    catch (const std::exception& e) {
        ErrorLogger::instance().logCrashUnsafe(std::string("Unhandled C++ exception: ") + e.what());
        return -1;
    }
    catch (...) {
        ErrorLogger::instance().logCrashUnsafe("Unhandled unknown exception");
        return -2;
    }
}

#else

int main(int argc, char* argv[]) {
    setWorkingDirectoryToExe();

#ifdef _WIN32
    initializeCrashHandler();
#endif

    ErrorLogger::instance().initialize(std::filesystem::current_path());

    std::string snapshotPath = parseSnapshotPath(argc, argv);

#ifdef _WIN32
    if (!snapshotPath.empty() && awow::SingleInstance::isAnotherInstanceRunning()) {
        awow::SingleInstance::sendFileToExistingInstance(snapshotPath);
        return 0;
    }
#endif

    try {
        // 0x0 lets Application use the saved size, or 80% of the screen
        Application app(0, 0, "AwowLab - Combat Log Visualizer");

        if (!snapshotPath.empty()) {
            app.setInitialSnapshot(snapshotPath);
        }

        return app.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
        ErrorLogger::instance().logCrashUnsafe(std::string("Unhandled C++ exception: ") + e.what());
        return -1;
    }
    catch (...) {
        std::cerr << "Unhandled unknown exception" << std::endl;
        ErrorLogger::instance().logCrashUnsafe("Unhandled unknown exception");
        return -2;
    }
}

#endif
