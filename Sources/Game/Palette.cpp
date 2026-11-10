#include "Game/Palette.hpp"

#include <algorithm>
#include <cmath>

namespace lightfoot {

uint32_t Palette::hsv(float h, float s, float v) {
    h = h - std::floor(h);
    const float c = v * s;
    const float x = c * (1 - std::fabs(std::fmod(h * 6, 2.0f) - 1));
    const float m = v - c;
    float r = 0, g = 0, b = 0;
    const int sector = static_cast<int>(h * 6) % 6;
    switch (sector) {
    case 0: r = c, g = x; break;
    case 1: r = x, g = c; break;
    case 2: g = c, b = x; break;
    case 3: g = x, b = c; break;
    case 4: r = x, b = c; break;
    default: r = c, b = x; break;
    }
    auto q = [&](float f) { return static_cast<uint32_t>(std::clamp((f + m) * 255.0f + 0.5f, 0.0f, 255.0f)); };
    return (q(r) << 16) | (q(g) << 8) | q(b);
}

uint32_t Palette::mix(uint32_t a, uint32_t b, int t) {
    t = std::clamp(t, 0, 255);
    uint32_t r = (((a >> 16) & 0xff) * (255 - t) + ((b >> 16) & 0xff) * t) / 255;
    uint32_t g = (((a >> 8) & 0xff) * (255 - t) + ((b >> 8) & 0xff) * t) / 255;
    uint32_t bl = ((a & 0xff) * (255 - t) + (b & 0xff) * t) / 255;
    return (r << 16) | (g << 8) | bl;
}

uint32_t Palette::scale(uint32_t c, float k) {
    auto q = [&](uint32_t v) { return static_cast<uint32_t>(std::clamp(v * k, 0.0f, 255.0f)); };
    return (q((c >> 16) & 0xff) << 16) | (q((c >> 8) & 0xff) << 8) | q(c & 0xff);
}

Palette Palette::make(float hue) {
    Palette p;
    const float h = hue;
    p.skyTop = hsv(h + 0.68f, 0.75f, 0.10f);
    p.skyMid = hsv(h + 0.74f, 0.70f, 0.30f);
    p.skyHorizon = hsv(h + 0.88f, 0.65f, 0.55f);
    p.sun = hsv(h + 0.93f, 0.85f, 1.0f);
    p.sunCore = hsv(h + 0.10f, 0.55f, 1.0f);
    p.mountain = hsv(h + 0.72f, 0.55f, 0.16f);
    p.cityFar = hsv(h + 0.70f, 0.50f, 0.12f);
    p.cityNear = hsv(h + 0.69f, 0.45f, 0.07f);
    p.window = hsv(h + 0.12f, 0.35f, 0.85f);
    p.ground = hsv(h + 0.70f, 0.60f, 0.06f);
    p.gridLine = hsv(h + 0.92f, 0.80f, 0.40f);
    p.gridLineBright = hsv(h + 0.92f, 0.70f, 0.85f);
    p.platform = hsv(h + 0.70f, 0.35f, 0.20f);
    p.platformEdge = hsv(h + 0.50f, 0.60f, 0.95f);
    p.platformGlow = hsv(h + 0.50f, 0.80f, 0.60f);
    p.text = 0xf2f3f7;
    p.textDim = hsv(h + 0.70f, 0.25f, 0.62f);
    p.accent = hsv(h + 0.11f, 0.90f, 1.0f);
    p.accentHot = hsv(h + 0.14f, 0.55f, 1.0f);
    p.danger = 0xff3b30;
    p.white = 0xffffff;
    p.row[0] = hsv(h + 0.52f, 0.80f, 1.0f); // top: cyan
    p.row[1] = hsv(h + 0.88f, 0.75f, 1.0f); // pink
    p.row[2] = hsv(h + 0.11f, 0.95f, 1.0f); // amber
    p.row[3] = hsv(h + 0.36f, 0.80f, 0.95f); // bottom: green
    p.runner = 0xfafafa;
    p.runnerTrail = hsv(h + 0.52f, 0.60f, 1.0f);
    return p;
}

} // namespace lightfoot
