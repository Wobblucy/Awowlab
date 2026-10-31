#pragma once

// Adding a finished file to an archive, shared by the fight and baseline
// packages. Separate from CsvUtil.h because that one is included by writers
// that have nothing to do with zipping, and pulling miniz into them made every
// consumer need its include path.

#include <filesystem>
#include <fstream>
#include <string>

extern "C" {
#include "miniz.h"
}

namespace awow::analysis {

inline bool addFileToZip(mz_zip_archive& zip, const fs::path& file, const std::string& entryName) {
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open()) return false;
    std::string contents((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    return mz_zip_writer_add_mem(&zip, entryName.c_str(),
                                 contents.data(), contents.size(),
                                 MZ_BEST_COMPRESSION) != MZ_FALSE;
}



} // namespace awow::analysis
