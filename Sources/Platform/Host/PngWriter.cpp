#include "Platform/Host/PngWriter.hpp"

#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include "stb_image_write.h"
#pragma clang diagnostic pop

namespace lightfoot {

bool writePng(const std::string& path, const uint32_t* pixels, int width, int height, int scale) {
    if (scale < 1) scale = 1;
    const int w = width * scale, h = height * scale;
    std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint32_t p = pixels[(y / scale) * width + (x / scale)];
            unsigned char* out = &rgb[(static_cast<size_t>(y) * w + x) * 3];
            out[0] = static_cast<unsigned char>((p >> 16) & 0xff);
            out[1] = static_cast<unsigned char>((p >> 8) & 0xff);
            out[2] = static_cast<unsigned char>(p & 0xff);
        }
    return stbi_write_png(path.c_str(), w, h, 3, rgb.data(), w * 3) != 0;
}

} // namespace lightfoot
