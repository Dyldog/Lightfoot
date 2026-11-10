#pragma once

#include <string>
#include <vector>

namespace lightfoot::paths {

/// Root of the storage the game writes to. On the device this is the SD card
/// root, found from MPC_APP_DIR (the autorun folder the launcher sets). On
/// the host it is LIGHTFOOT_ROOT, or build/sdcard under the working directory.
std::string cardRoot();
/// <cardRoot>/Lightfoot, where scores and settings live.
std::string dataDir();

std::string join(const std::string& a, const std::string& b);
bool exists(const std::string& path);
bool makeDirs(const std::string& path);
bool isDirectory(const std::string& path);
/// Sorted file names in `dir` ending with `suffix` (case-insensitive), without the directory.
std::vector<std::string> listFiles(const std::string& dir, const std::string& suffix);
bool readFile(const std::string& path, std::string& out);
/// Writes to a temp file and renames over the target, so a cut power never leaves a half-written file.
bool writeFileAtomic(const std::string& path, const std::string& data);

} // namespace lightfoot::paths
