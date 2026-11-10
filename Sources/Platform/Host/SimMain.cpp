// Mac simulator entry point.
//   lightfoot-sim              interactive window
//   lightfoot-sim --shots DIR  headless screenshots, then exit
#include <cstdio>
#include <cstring>
#include <string>

#include "Game/Game.hpp"
#include "Platform/Host/Shots.hpp"
#include "Platform/Host/SimWindow.hpp"

int main(int argc, char** argv) {
    int scale = 3;
    std::string shotsDir;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shots") && i + 1 < argc) shotsDir = argv[++i];
        else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) scale = std::atoi(argv[++i]);
        else {
            std::fprintf(stderr, "usage: %s [--scale N] [--shots DIR]\n", argv[0]);
            return 2;
        }
    }
    if (!shotsDir.empty()) return lightfoot::runShots(shotsDir, 2);
    lightfoot::Game game;
    lightfoot::SimWindow window(game, scale);
    return window.run();
}
