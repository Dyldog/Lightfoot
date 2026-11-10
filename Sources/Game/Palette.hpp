#pragma once

#include <cstdint>

namespace lightfoot {

/// The game's colours, derived from one hue so K1 can repaint the whole night.
/// Hue 0 is the default: indigo sky, magenta sun, cyan and pink rows.
struct Palette {
    uint32_t skyTop, skyMid, skyHorizon;
    uint32_t sun, sunCore;
    uint32_t mountain, cityFar, cityNear, window;
    uint32_t ground, gridLine, gridLineBright;
    uint32_t platform, platformEdge, platformGlow;
    uint32_t text, textDim, accent, accentHot, danger, white;
    uint32_t row[4];
    uint32_t runner, runnerTrail;

    static Palette make(float hue);
    static uint32_t hsv(float h, float s, float v);
    static uint32_t mix(uint32_t a, uint32_t b, int t); // t 0..255 towards b
    static uint32_t scale(uint32_t c, float k);
};

} // namespace lightfoot
