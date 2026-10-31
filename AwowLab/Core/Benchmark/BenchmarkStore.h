#pragma once

#include <string>

// Where benchmark data lives on disk.
//
// Spell classifications and reference profiles hold nothing private, and are
// useful to share with someone playing the same spec. They are kept as plain
// JSON, apart from the encrypted settings file that holds credentials, so they
// can be handed around, edited or deleted. Nothing in them is trusted beyond
// display and comparison: a tampered file gives a wrong report, nothing more.
namespace benchmark::store {

// Redirect every path below to a different root. Used by tests so saving
// never overwrites a real install's rulings.
void setRootDirectoryForTesting(const std::string& root);

// "User Settings/benchmark/classifications.json"
std::string classificationsPath();

// "User Settings/benchmark/mentors.json"
std::string mentorsPath();

// Reads the whole file, or an empty string when it does not exist. A missing
// file is the normal first-run state, not an error.
std::string readFile(const std::string& path);

// Writes atomically via a temporary file, so an interrupted write cannot
// leave a half-written file that fails to parse on next launch. Returns
// false if the write could not be completed.
bool writeFile(const std::string& path, const std::string& contents);

} // namespace benchmark::store
