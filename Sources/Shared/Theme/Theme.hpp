#pragma once

#include <cstdint>

// Colours the simulator's panel painting shares with the game.
namespace lightfoot::theme {

constexpr uint32_t accent = 0xffb000;

constexpr uint32_t mix(uint32_t a, uint32_t b, int t) {
    uint32_t r = (((a >> 16) & 0xff) * (255 - t) + ((b >> 16) & 0xff) * t) / 255;
    uint32_t g = (((a >> 8) & 0xff) * (255 - t) + ((b >> 8) & 0xff) * t) / 255;
    uint32_t bl = ((a & 0xff) * (255 - t) + (b & 0xff) * t) / 255;
    return (r << 16) | (g << 8) | bl;
}

} // namespace lightfoot::theme
