#pragma once

#include <cstdint>
#include <string>

namespace lightfoot {

/// Writes 0xRRGGBB pixels as a PNG. Returns false on failure.
bool writePng(const std::string& path, const uint32_t* pixels, int width, int height, int scale = 1);

} // namespace lightfoot
