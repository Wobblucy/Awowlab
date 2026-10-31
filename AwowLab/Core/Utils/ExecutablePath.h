#pragma once

#include <filesystem>
#include <string>

namespace awow {

/**
 * @brief Get the directory containing the executable.
 *
 * Read through the wide API, so it is exact for any folder name. Prefer this
 * for new code. Empty on failure.
 */
std::filesystem::path getExecutableDirectoryPath();

/**
 * @brief Get the directory containing the executable as a narrow string.
 *
 * Returns the directory with a trailing slash/backslash, in the encoding the
 * narrow file APIs expect. If that encoding cannot represent the folder name,
 * returns ".\" instead, which works because startup sets the working
 * directory to the executable's folder. On failure, returns an empty string.
 *
 * @return std::string Path to executable directory with trailing separator
 */
std::string getExecutableDirectory();

} // namespace awow
