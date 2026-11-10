#pragma once

#include <string>

namespace lightfoot {

/// Writes a PNG of every screen state into `dir`. Returns a process exit code.
int runShots(const std::string& dir, int scale);

} // namespace lightfoot
