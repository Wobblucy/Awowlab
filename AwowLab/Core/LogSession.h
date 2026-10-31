#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <memory>

class MMapFile;
struct EncounterSegment;

// An open combat log. The file is mapped only while it is being scanned or
// tokenized; the encounter list is cached so the selector works without the
// mapping. Lines handed out by getEncounterLines()/getAllLines() point into
// the mapping, so it stays open until releaseFileMapping().
class LogSession {
public:
    LogSession();
    ~LogSession();

    LogSession(const LogSession&) = delete;
    LogSession& operator=(const LogSession&) = delete;

    LogSession(LogSession&&) noexcept;
    LogSession& operator=(LogSession&&) noexcept;

    // Scans for encounters, then unmaps the file
    bool open(const std::string& filepath);

    void close();
    bool isOpen() const;

    const std::string& getFilePath() const { return filepath_; }

    // Rescans the whole file if it has grown. Returns true if the rescan
    // found more encounters than before.
    bool refresh();

    // Tokenizes one encounter's byte range. Empty if the index is invalid.
    // Call releaseFileMapping() once the lines are no longer needed.
    std::vector<std::vector<std::string_view>> getEncounterLines(size_t encounterIndex);

    // Tokenizes the whole file. Same lifetime rule as getEncounterLines().
    std::vector<std::vector<std::string_view>> getAllLines();

    const std::vector<EncounterSegment>& getEncounters() const { return encounters_; }

    size_t getTotalLines() const { return totalLineCount_; }
    size_t getTotalBytes() const { return totalFileSize_; }
    size_t getScannedBytes() const { return lastScannedOffset_; }
    size_t getEncounterCount() const { return encounters_.size(); }

    // True if the file on disk is larger than when it was last scanned
    bool hasNewContent() const;

    // Unmaps the file. The encounter list stays available.
    void releaseFileMapping();

private:
    std::string filepath_;

    std::vector<EncounterSegment> encounters_;

    size_t lastScannedOffset_ = 0;  // File size at the last scan
    size_t totalFileSize_ = 0;
    size_t totalLineCount_ = 0;     // For display only

    std::unique_ptr<MMapFile> mmapFile_;

    bool openMmap();
    void releaseMmap();
};
