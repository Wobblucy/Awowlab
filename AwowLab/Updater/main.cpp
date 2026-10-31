#include "Updater.h"
#include "ProgressWindow.h"
#include "PathValidator.h"
#include "AwowLabBuildOptions.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>
#include <shobjidl.h>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#include <optional>
#include <vector>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

using namespace awow::updater;
namespace fs = std::filesystem;

static const char* SETUP_TITLE = "AwowLab Setup";

void showSetupError(const std::string& message) {
    std::wstring wideMessage(message.begin(), message.end());
    std::wstring wideTitle(SETUP_TITLE, SETUP_TITLE + strlen(SETUP_TITLE));
    MessageBoxW(nullptr, wideMessage.c_str(), wideTitle.c_str(), MB_OK | MB_ICONWARNING);
}

// Why a picked folder can't be used, or empty if it can.
std::string refusalMessage(InstallFolder::Status status) {
    switch (status) {
        case InstallFolder::Status::Usable:
        case InstallFolder::Status::ExistingInstall:
            return "";
        case InstallFolder::Status::Missing:
            return "That folder could not be found. Please choose another one.";
        case InstallFolder::Status::DriveRoot:
            return "AwowLab can't be installed at the top of a drive.\n\n"
                   "Please choose or create a folder for it, for example C:\\Games\\AwowLab.";
        case InstallFolder::Status::DownloadsFolder:
            return "AwowLab can't be installed in your Downloads folder.\n\n"
                   "Please choose another folder.";
        case InstallFolder::Status::NotWritable:
            return "AwowLab can't write to that folder.\n\n"
                   "Folders such as Program Files need administrator rights, which setup "
                   "does not ask for. Please choose a folder in your user profile.";
    }
    return "That folder can't be used. Please choose another one.";
}

// Asks where to install until the user picks a usable folder or cancels.
// Returns nullopt on cancel, with nothing left behind on disk.
std::optional<std::string> chooseInstallFolder() {
    const std::string defaultFolder = InstallFolder::defaultInstallFolder();
    const std::string downloadsFolder = InstallFolder::knownDownloadsFolder();

    // The dialog can only open in a folder that exists, so the default is
    // created up front and removed again if it is not used.
    bool createdDefault = false;
    if (!defaultFolder.empty()) {
        std::error_code ec;
        if (!fs::exists(defaultFolder, ec)) {
            createdDefault = fs::create_directories(defaultFolder, ec) && !ec;
        }
    }

    HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    std::optional<std::string> chosen;
    while (true) {
        IFileOpenDialog* dialog = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&dialog)))) {
            showSetupError("Could not open the folder picker.");
            break;
        }

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(L"Choose where to install AwowLab");
        dialog->SetOkButtonLabel(L"Install here");

        if (!defaultFolder.empty()) {
            IShellItem* startFolder = nullptr;
            std::wstring wideDefault = fs::path(defaultFolder).wstring();
            if (SUCCEEDED(SHCreateItemFromParsingName(wideDefault.c_str(), nullptr,
                                                      IID_PPV_ARGS(&startFolder)))) {
                dialog->SetFolder(startFolder);
                startFolder->Release();
            }
        }

        HRESULT shown = dialog->Show(nullptr);

        std::string picked;
        if (SUCCEEDED(shown)) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR widePath = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &widePath))) {
                    try {
                        picked = fs::path(widePath).string();
                    } catch (const std::exception&) {
                        // The rest of the updater works in the ANSI code page.
                        picked.clear();
                    }
                    CoTaskMemFree(widePath);
                }
                item->Release();
            }
        }
        dialog->Release();

        // Cancel, or the dialog failed to show.
        if (FAILED(shown)) {
            break;
        }

        if (picked.empty()) {
            showSetupError("That folder can't be used. Please choose another one.");
            continue;
        }

        std::string refusal = refusalMessage(InstallFolder::check(picked, downloadsFolder));
        if (refusal.empty()) {
            chosen = picked;
            break;
        }
        showSetupError(refusal);
    }

    if (SUCCEEDED(comInit)) {
        CoUninitialize();
    }

    if (createdDefault) {
        std::error_code ec;
        bool usingDefault = chosen && fs::equivalent(*chosen, defaultFolder, ec);
        if (!usingDefault) {
            // Only succeeds while the folder is still empty.
            fs::remove(defaultFolder, ec);
        }
    }

    return chosen;
}

// Shows the updater's progress in the window and keeps it responsive. A
// click on Cancel is read through isCancelled() once the update returns.
UpdateProgressCallback showProgressIn(ProgressWindow& progress) {
    return [&progress](const std::string& status, float fraction) {
        progress.setProgress(fraction, status);
        progress.processMessages();
    };
}

// The locale after --optional-pack, or empty when the flag is absent.
std::string parseOptionalPackArg(LPSTR lpCmdLine) {
    std::string cmdLine(lpCmdLine);

    size_t pos = cmdLine.find("--optional-pack");
    if (pos == std::string::npos) {
        return "";
    }

    pos += strlen("--optional-pack");
    while (pos < cmdLine.size() && (cmdLine[pos] == ' ' || cmdLine[pos] == '\t')) {
        pos++;
    }

    std::string locale;
    while (pos < cmdLine.size() && cmdLine[pos] != ' ' && cmdLine[pos] != '\t') {
        locale += cmdLine[pos++];
    }

    return locale;
}

// With no arguments the updater installs everything when AwowLab.exe is
// missing, and otherwise applies a delta update. Run on its own with no
// install beside it, it first asks where to install.
//
//   --optional-pack <locale>  download one language pack (ko_KR, zh_CN, zh_TW)
//   --allow-downgrade         permit installing a release older than the current one
int WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
                   LPSTR lpCmdLine, int /*nCmdShow*/) {

    // Load any DLL brought in from here on (the folder picker pulls in a
    // lot of shell code) from System32 only, never from the folder the
    // updater was downloaded to.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);

    std::string optionalPackLocale = parseOptionalPackArg(lpCmdLine);
    bool allowDowngrade = std::string(lpCmdLine).find("--allow-downgrade") != std::string::npos;

    Updater updater;
    updater.setRepository(AWOWLAB_UPDATE_REPO_OWNER, AWOWLAB_UPDATE_REPO_NAME);
    updater.setAllowDowngrade(allowDowngrade);

    bool freshInstall = !InstallFolder::isExistingInstall(updater.getAppDirectory());

    // The copy in an install's updater\ folder always works on that install.
    // A standalone copy with no install beside it is a new install, and must
    // not drop the app into wherever it was downloaded to.
    if (freshInstall && !updater.runsFromInstall()) {
        if (!optionalPackLocale.empty()) {
            showSetupError("AwowLab is not installed here.\n\n"
                           "Install AwowLab first, then add language packs from inside the app.");
            return 1;
        }

        auto folder = chooseInstallFolder();
        if (!folder) {
            return 1;
        }

        updater.setAppDirectory(*folder);
        freshInstall = !InstallFolder::isExistingInstall(*folder);
    }

    ProgressWindow progress;
    std::string windowTitle;

    if (!optionalPackLocale.empty()) {
        windowTitle = "AwowLab Language Pack";
    } else if (freshInstall) {
        windowTitle = SETUP_TITLE;
    } else {
        windowTitle = "AwowLab Updater";
    }

    if (!progress.create(windowTitle)) {
        MessageBoxA(nullptr, "Failed to create progress window", "Error", MB_ICONERROR);
        return 1;
    }

    if (!optionalPackLocale.empty()) {
        // Pack ids are letters, digits, '_' and '-' only.
        auto isValidPackId = [](const std::string& id) {
            return !id.empty() && std::all_of(id.begin(), id.end(), [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
            });
        };

        if (!isValidPackId(optionalPackLocale)) {
            progress.showMessage("Invalid language pack identifier: " + optionalPackLocale,
                                 "Error", true);
            progress.close();
            return 1;
        }

        progress.setProgress(0.0f, "Preparing to download language pack...");
        progress.processMessages();

        std::string packId = "langpack_" + optionalPackLocale;

        auto result = updater.downloadOptionalPack(packId, showProgressIn(progress));

        if (progress.isCancelled()) {
            progress.showMessage("Download cancelled.", "Cancelled", true);
            progress.close();
            updater.launchMainApp();
            return 1;
        }

        if (!result.success) {
            progress.showMessage("Download failed: " + result.errorMessage,
                                 "Download Failed", true);
            progress.close();
            updater.launchMainApp();
            return 1;
        }

        progress.setProgress(1.0f, "Language pack installed! Launching AwowLab...");
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));

        progress.close();
        updater.launchMainApp();
        return 0;
    }

    if (freshInstall) {
        progress.setProgress(0.0f, "Preparing to download AwowLab...");
        progress.processMessages();

        auto result = updater.performUpdate(showProgressIn(progress));

        if (progress.isCancelled()) {
            progress.showMessage("Download cancelled.", "Cancelled", true);
            progress.close();
            return 1;
        }

        if (!result.success) {
            progress.showMessage("Download failed: " + result.errorMessage,
                                 "Download Failed", true);
            progress.close();
            return 1;
        }

        progress.setProgress(1.0f, "Download complete! Launching AwowLab...");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    } else {
        progress.setProgress(0.0f, "Checking for updates...");
        progress.processMessages();

        auto newVersion = updater.checkForUpdate();

        if (!newVersion) {
            progress.setProgress(1.0f, "You're already up to date!");
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));

            progress.close();
            updater.launchMainApp();
            return 0;
        }

        auto result = updater.performUpdate(showProgressIn(progress));

        if (progress.isCancelled()) {
            progress.showMessage("Update cancelled. The application may be in an inconsistent state.",
                                 "Update Cancelled", true);
            progress.close();
            updater.launchMainApp();
            return 1;
        }

        if (result.needsRestart) {
            progress.showMessage("The updater has been updated.\n\n"
                                 "Please click 'Check for Updates' again to continue.",
                                 "Updater Updated", false);
        } else if (!result.success) {
            progress.showMessage("Update failed: " + result.errorMessage +
                                 "\n\nThe application will now launch with the previous version.",
                                 "Update Failed", true);
        } else {
            progress.setProgress(1.0f, "Update complete! " + std::to_string(result.filesUpdated) + " files updated.");
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }
    }

    progress.close();
    updater.launchMainApp();

    return 0;
}
