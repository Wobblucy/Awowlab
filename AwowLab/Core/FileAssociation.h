#pragma once

#include <string>

namespace awow {
namespace FileAssociation {

// Check if .awsnap file association is already registered for this user
bool isRegistered();

// Register .awsnap file association in HKEY_CURRENT_USER (no admin required)
// Returns true on success
bool registerAssociation();

// Unregister .awsnap file association
// Returns true on success
bool unregisterAssociation();

// Get the current executable path
std::string getExecutablePath();

} // namespace FileAssociation
} // namespace awow
