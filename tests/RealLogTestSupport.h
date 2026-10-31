#pragma once

// Helpers shared by the tests that run the analysis writers over the real
// combat log named by REAL_LOG_PATH, and read their CSV output back.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "Txttokenizer/Txttokenizer.h"
#include "Database/OutputWriter.h"

namespace test_support {

// How much of the real log is read: enough for several pulls, small enough
// to keep the tests quick.
inline constexpr size_t kRealLogPrefixBytes = 40u * 1024u * 1024u;

// The file's non-empty lines, without line endings.
inline std::vector<std::string> readLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

// Reads a row the way a consumer would, so a quoting or comma-count mistake in
// a writer shows up here rather than in someone's analysis.
inline std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> fields;
    std::string cur;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; }
                else inQuotes = false;
            } else cur += c;
        } else if (c == '"') inQuotes = true;
        else if (c == ',') { fields.push_back(cur); cur.clear(); }
        else cur += c;
    }
    fields.push_back(cur);
    return fields;
}

// Every event in the first kRealLogPrefixBytes of the real log, or nothing
// when there is no real log on this machine. The tokenizer hands out
// string_views into the buffer, so the caller has to keep bufferOut alive
// for as long as the returned data is used.
inline ExtractedData extractFromRealLog(std::string& bufferOut) {
    const std::string path = REAL_LOG_PATH;
    if (path.empty()) return {};

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};

    bufferOut.assign(kRealLogPrefixBytes, '\0');
    in.read(bufferOut.data(), static_cast<std::streamsize>(bufferOut.size()));
    bufferOut.resize(static_cast<size_t>(in.gcount()));

    size_t lastNewline = bufferOut.rfind('\n');
    if (lastNewline == std::string::npos) return {};
    bufferOut.resize(lastNewline + 1);

    tokenized_segment seg;
    auto lines = seg.tokenize(bufferOut.data(), bufferOut.size());
    return OutputWriter::extractAllEvents(lines);
}

inline std::string tempPath(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

} // namespace test_support
