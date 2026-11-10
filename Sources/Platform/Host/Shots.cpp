#include "Platform/Host/Shots.hpp"

#include <cstdio>
#include <cstdlib>

#include "Game/Game.hpp"
#include "Platform/Host/HostLoop.hpp"
#include "Platform/Host/PngWriter.hpp"

extern "C" {
#include "input.h"
#include "ui.h"
}

namespace lightfoot {

namespace {

struct Shooter {
    const std::string& dir;
    int scale;
    int failures = 0;
    int64_t now = 1000;

    void save(HostLoop& loop, const char* name) {
        const std::string path = dir + "/" + name + ".png";
        if (!writePng(path, loop.pixels(), UI_W, UI_H, scale)) {
            std::fprintf(stderr, "shots: could not write %s\n", path.c_str());
            ++failures;
        } else {
            std::printf("wrote %s\n", path.c_str());
        }
    }
    /// Steps once with whatever is queued, then advances the fake clock.
    void frame(HostLoop& loop, int advanceMs = 16) {
        loop.step(now);
        now += advanceMs;
    }
    void run(HostLoop& loop, int ms) {
        for (int t = 0; t < ms; t += 16) frame(loop);
    }
};

} // namespace

int runShots(const std::string& dir, int scale) {
    setenv("LIGHTFOOT_ROOT", (dir + "/card").c_str(), 1);
    Shooter s{dir, scale};
    Game game;
    HostLoop loop(game);
    // No audio output is attached, so the beat runs on the fake clock: deterministic.
    s.run(loop, 1200);
    s.save(loop, "00-title");

    loop.pushButtonTap(IN_B2);
    s.run(loop, 100);
    s.save(loop, "01-how-to");
    loop.pushButtonTap(IN_B1);
    s.run(loop, 100);

    // A seeded run from level 3: play the jumps perfectly, then let the picture settle.
    game.setStartLevel(3);
    game.startGame(s.now, 7);
    s.run(loop, 16);
    bool perfectShot = false;
    for (int step = 0; step < 6000; ++step) {
        s.frame(loop);
        if (game.state() == Game::State::GameOver) break;
        const bool tapping = step < 1500; // then stop playing and let the run end
        const int64_t cue = game.cueSlot();
        if (tapping && cue >= 0 && std::abs(game.beat() - static_cast<double>(cue)) < 0.02) {
            const Slot& slot = game.track().at(cue);
            loop.pushPadTap(Game::padIndex(slot.row, 0));
            if (slot.row2 >= 0) loop.pushPadTap(Game::padIndex(slot.row2, 0));
        }
        if (step == 700) s.save(loop, "02-playing");
        if (!perfectShot && game.combo() == 6) {
            perfectShot = true;
            s.frame(loop);
            s.save(loop, "03-perfect");
        }
        if (step == 1200) {
            s.save(loop, "04-run");
            loop.pushButtonTap(IN_PLAY);
            s.run(loop, 60);
            s.save(loop, "05-paused");
            loop.pushButtonTap(IN_PLAY);
            s.run(loop, 40);
        }
    }
    std::fprintf(stderr, "shots: run ended with score %d combo %d lives %d state %d\n", game.score(), game.combo(), game.lives(), static_cast<int>(game.state()));
    s.run(loop, 700);
    s.save(loop, "06-game-over");
    loop.pushButtonTap(IN_STOP);
    s.run(loop, 100);
    loop.pushButtonTap(IN_B3);
    s.run(loop, 100);
    s.save(loop, "07-scores");
    return s.failures ? 1 : 0;
}

} // namespace lightfoot
