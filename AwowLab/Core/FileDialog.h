#pragma once
#include <string>
#include "portable-file-dialogs.h"

namespace FileDialog {

// Open cross-platform file dialog to select a combat log file
inline std::string openCombatLogFile() {
    auto selection = pfd::open_file(
        "Select WoW Combat Log File",
        "",
        { "WoW Combat Logs (*.txt)", "*.txt", "All Files", "*" },
        pfd::opt::none
    ).result();

    if (!selection.empty()) {
        return selection[0];
    }
    return "";
}

// Open cross-platform file dialog to select a snapshot file
inline std::string openSnapshotFile() {
    auto selection = pfd::open_file(
        "Open Replay Snapshot",
        "",
        { "AwowLab Snapshots (*.awsnap)", "*.awsnap", "All Files", "*" },
        pfd::opt::none
    ).result();

    if (!selection.empty()) {
        return selection[0];
    }
    return "";
}

// Save cross-platform file dialog to save a snapshot file
inline std::string saveSnapshotFile(const std::string& defaultFilename = "snapshot.awsnap") {
    auto selection = pfd::save_file(
        "Save Replay Snapshot",
        defaultFilename,
        { "AwowLab Snapshots (*.awsnap)", "*.awsnap" },
        pfd::opt::none
    ).result();

    // Ensure .awsnap extension
    if (!selection.empty() && selection.find(".awsnap") == std::string::npos) {
        selection += ".awsnap";
    }
    return selection;
}

// Where an analysis package should be written. A folder rather than a filename,
// because the zip's own name encodes the package type, spec and date.
inline std::string selectPackageFolder() {
    return pfd::select_folder("Where should the analysis package go?").result();
}

} // namespace FileDialog
