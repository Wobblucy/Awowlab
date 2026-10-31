#include "LogSession.h"
#include "ErrorLogger.h"
#include "MMapManager/src/MMapManager.cpp"  // MMapFile is defined entirely in this .cpp
#include "Txttokenizer/include/Txttokenizer/Txttokenizer.h"
#include "Parser/Include/Encounter/EncounterScanner.h"
#include "Parser/Include/Encounter/EncounterSegment.h"
#include <algorithm>
#include <filesystem>

namespace {

size_t countLines(const char* data, size_t size) {
    return static_cast<size_t>(std::count(data, data + size, '\n'));
}

} // namespace

LogSession::LogSession() = default;

LogSession::~LogSession() {
    close();
}

LogSession::LogSession(LogSession&& other) noexcept
    : filepath_(std::move(other.filepath_))
    , encounters_(std::move(other.encounters_))
    , lastScannedOffset_(other.lastScannedOffset_)
    , totalFileSize_(other.totalFileSize_)
    , totalLineCount_(other.totalLineCount_)
    , mmapFile_(std::move(other.mmapFile_))
{
    other.lastScannedOffset_ = 0;
    other.totalFileSize_ = 0;
    other.totalLineCount_ = 0;
}

LogSession& LogSession::operator=(LogSession&& other) noexcept {
    if (this != &other) {
        close();
        filepath_ = std::move(other.filepath_);
        encounters_ = std::move(other.encounters_);
        lastScannedOffset_ = other.lastScannedOffset_;
        totalFileSize_ = other.totalFileSize_;
        totalLineCount_ = other.totalLineCount_;
        mmapFile_ = std::move(other.mmapFile_);

        other.lastScannedOffset_ = 0;
        other.totalFileSize_ = 0;
        other.totalLineCount_ = 0;
    }
    return *this;
}

bool LogSession::openMmap() {
    if (mmapFile_ && mmapFile_->get_data()) {
        return true;  // Already open
    }

    if (filepath_.empty()) {
        return false;
    }

    mmapFile_ = std::make_unique<MMapFile>();
    try {
        mmapFile_->open(filepath_);
        return true;
    } catch (const std::runtime_error& e) {
        AWOW_LOG_ERROR("FILE", std::string("Failed to open combat log: ") + e.what());
        mmapFile_.reset();
        return false;
    }
}

void LogSession::releaseMmap() {
    mmapFile_.reset();
}

void LogSession::releaseFileMapping() {
    releaseMmap();
}

bool LogSession::open(const std::string& filepath) {
    close();

    filepath_ = filepath;

    if (!openMmap()) {
        filepath_.clear();
        return false;
    }

    totalFileSize_ = mmapFile_->get_size();

    encounters_ = EncounterScanner::scanForEncountersLightweight(
        mmapFile_->get_data(), totalFileSize_
    );
    lastScannedOffset_ = totalFileSize_;
    totalLineCount_ = countLines(mmapFile_->get_data(), totalFileSize_);

    // The encounter list is cached, so the mapping is not needed until an
    // encounter is tokenized.
    releaseMmap();
    return true;
}

void LogSession::close() {
    releaseMmap();
    filepath_.clear();
    encounters_.clear();
    lastScannedOffset_ = 0;
    totalFileSize_ = 0;
    totalLineCount_ = 0;
}

bool LogSession::isOpen() const {
    return !filepath_.empty();
}

bool LogSession::hasNewContent() const {
    if (filepath_.empty()) {
        return false;
    }

    try {
        auto currentSize = std::filesystem::file_size(filepath_);
        return currentSize > lastScannedOffset_;
    } catch (...) {
        return false;
    }
}

bool LogSession::refresh() {
    if (filepath_.empty()) {
        AWOW_LOG_ERROR("FILE", "Cannot refresh - no log file is open");
        return false;
    }

    if (!openMmap()) {
        return false;
    }

    size_t newFileSize = mmapFile_->get_size();

    if (newFileSize <= lastScannedOffset_) {
        totalFileSize_ = newFileSize;
        releaseMmap();
        return false;
    }

    size_t oldEncounterCount = encounters_.size();

    // Rescan from the start: an encounter can straddle the old end of file,
    // and the marker-only scan is cheap enough to repeat.
    encounters_ = EncounterScanner::scanForEncountersLightweight(
        mmapFile_->get_data(), newFileSize
    );

    lastScannedOffset_ = newFileSize;
    totalFileSize_ = newFileSize;
    totalLineCount_ = countLines(mmapFile_->get_data(), totalFileSize_);

    releaseMmap();
    return encounters_.size() > oldEncounterCount;
}

std::vector<std::vector<std::string_view>> LogSession::getEncounterLines(size_t encounterIndex) {
    std::vector<std::vector<std::string_view>> result;

    if (filepath_.empty() || encounterIndex >= encounters_.size()) {
        AWOW_LOG_ERROR("FILE", "Invalid encounter index or no log session open");
        return result;
    }

    // The mapping stays open: the returned string_views point into it.
    if (!openMmap()) {
        return result;
    }

    const auto& encounter = encounters_[encounterIndex];

    size_t byteStart = encounter.startByteOffset;
    size_t byteEnd = encounter.endByteOffset;

    if (byteEnd > mmapFile_->get_size()) {
        AWOW_LOG_ERROR("FILE", "Encounter data exceeds file size - log file may be corrupted");
        releaseMmap();
        return result;
    }

    if (byteStart >= byteEnd) {
        AWOW_LOG_ERROR("FILE", "Invalid encounter byte range - log file may be corrupted");
        releaseMmap();
        return result;
    }

    size_t byteLen = byteEnd - byteStart;
    const char* dataStart = mmapFile_->get_data() + byteStart;

    tokenized_segment tokenizer;
    result = tokenizer.tokenize(dataStart, byteLen);

    return result;
}

std::vector<std::vector<std::string_view>> LogSession::getAllLines() {
    std::vector<std::vector<std::string_view>> result;

    if (filepath_.empty()) {
        AWOW_LOG_ERROR("FILE", "Cannot get lines - no log session open");
        return result;
    }

    // The mapping stays open: the returned string_views point into it.
    if (!openMmap()) {
        return result;
    }

    tokenized_segment tokenizer;
    result = tokenizer.tokenize(mmapFile_->get_data(), mmapFile_->get_size());

    return result;
}
