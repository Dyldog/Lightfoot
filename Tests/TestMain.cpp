// Lightfoot unit tests: the course generator, the judge, scoring, the clock,
// the synth, the pad mapping and the score table. `make test` runs them after
// the screenshot suite.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "Audio/AudioEngine.hpp"
#include "Game/Game.hpp"
#include "Game/Scores.hpp"
#include "Game/Track.hpp"
#include "Platform/Host/HostLoop.hpp"
#include "Shared/Paths/Paths.hpp"

extern "C" {
#include "input.h"
}

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void testPadMapping() {
    check(lightfoot::Game::padIndex(3, 0) == 0, "bottom-left is pad 1");
    check(lightfoot::Game::padIndex(0, 0) == 12, "top-left is pad 13");
    check(lightfoot::Game::padIndex(0, 3) == 15, "top-right is pad 16");
    for (int p = 0; p < 16; ++p)
        check(lightfoot::Game::padIndex(lightfoot::Game::padRow(p), lightfoot::Game::padCol(p)) == p, "pad index round trips");
}

void testTrack() {
    lightfoot::Track a(42), b(42), c(43);
    bool same = true, differ = false;
    for (int i = 0; i < 200; ++i) {
        same &= a.at(i).row == b.at(i).row && a.at(i).cue == b.at(i).cue;
        differ |= a.at(i).row != c.at(i).row;
    }
    check(same, "the same seed gives the same course");
    check(differ, "a different seed gives a different course");
    int cues = 0, twins = 0, decoys = 0;
    for (int i = 0; i < 6; ++i) check(!a.at(i).cue, "the course opens with a plain run");
    for (int i = 0; i < 400; ++i) {
        const lightfoot::Slot& s = a.at(i);
        if (s.cue) {
            ++cues;
            if (i > 0) check(s.row != a.at(i - 1).row || s.gap, "a jump changes row");
            if (s.row2 >= 0) {
                ++twins;
                check(s.gap, "a twin sits on a gap");
                check(a.at(i + 1).row == s.row && !a.at(i + 1).cue, "a twin lands on the next slot at the first pad's row");
            }
            if (s.decoy >= 0) {
                ++decoys;
                check(s.decoy != s.row && s.decoy != s.row2, "a decoy is never a lit pad");
            }
        } else if (i > 0 && !a.at(i - 1).gap) {
            check(s.row == a.at(i - 1).row, "no jump means the same row");
        }
    }
    check(cues > 60, "a 400-beat course has plenty of jumps");
    check(twins > 0, "twins appear by level 12");
    check(decoys > 0, "decoys appear by level 12");
    check(a.level(0) == 1 && a.level(32) == 2 && a.level(63) == 2, "levels change every 32 beats");
    check(a.tempoFor(1) == 100 && a.tempoFor(5) == 120 && a.tempoFor(40) == 165, "tempo ramps 5 BPM a level and caps");
    lightfoot::Track late(5, 4);
    check(late.level(0) == 4, "a start level offsets the whole course");
}

void testEngineClockAndRender() {
    lightfoot::AudioEngine e(44100);
    e.setTempo(120);
    e.resetBeat(0);
    e.beat(1000);
    check(std::fabs(e.beat(1500) - 1.0) < 1e-9, "unattached, the beat follows wall time");
    e.setPaused(true);
    check(std::fabs(e.beat(2500) - 1.0) < 1e-9, "paused, the beat stands still");
    e.setPaused(false);
    e.beat(2500);
    e.setAttached(true);
    e.setMode(lightfoot::AudioEngine::Mode::Play);
    e.setIntensity(6);
    for (int i = 0; i < 24; ++i) e.trigger(static_cast<lightfoot::Sfx>(i % 12));
    std::vector<float> out(256 * 2);
    float peak = 0;
    bool finite = true;
    for (int block = 0; block < 400; ++block) { // about 2.3 s
        e.render(out.data(), 256);
        for (float v : out) {
            finite &= std::isfinite(v);
            peak = std::max(peak, std::fabs(v));
        }
    }
    check(finite, "the synth never produces NaN or infinity");
    check(peak > 0.05f && peak <= 1.0f, "the synth makes sound within range");
    check(std::fabs(e.positionBeats() - (1.0 + 400 * 256 / 44100.0 * 2.0)) < 1e-6, "attached, the beat follows the samples");
}

lightfoot::Game& playTo(lightfoot::Game& game, lightfoot::HostLoop& loop, int64_t& now, double beat) {
    while (game.beat() < beat && game.state() == lightfoot::Game::State::Playing) {
        now += 8;
        loop.step(now);
    }
    return game;
}

void testJudgeAndScore() {
    setenv("LIGHTFOOT_ROOT", "build/test-card", 1);
    lightfoot::Game game;
    game.setStartLevel(1);
    lightfoot::HostLoop loop(game);
    int64_t now = 1000;
    loop.step(now);
    game.startGame(now, 7);
    check(game.state() == lightfoot::Game::State::Playing && game.lives() == 3 && game.score() == 0, "a run starts fresh");
    // Count-in, then the first cue.
    playTo(game, loop, now, 0.5);
    check(game.cueSlot() > 0, "a cue is pending after the count-in");
    const int64_t cue = game.cueSlot();
    const lightfoot::Slot& s = game.track().at(cue);
    // Too early is a slip: no points, no life lost.
    playTo(game, loop, now, cue - 0.6);
    loop.pushPadTap(lightfoot::Game::padIndex(s.row, 0));
    now += 8;
    loop.step(now);
    check(game.lastHit() == lightfoot::Game::Hit::Slip && game.score() == 0 && game.lives() == 3, "an early tap slips");
    // On the beat: perfect.
    playTo(game, loop, now, cue - 0.01);
    loop.pushPadTap(lightfoot::Game::padIndex(s.row, 0));
    now += 8;
    loop.step(now);
    check(game.lastHit() == lightfoot::Game::Hit::Perfect, "a tap on the beat is perfect");
    check(game.score() == 100 && game.combo() == 1, "a perfect scores 100 with combo 1");
    check(game.cueSlot() != cue, "the cue is resolved");
    // The next cue: slightly late is good.
    const int64_t cue2 = game.cueSlot();
    const lightfoot::Slot& s2 = game.track().at(cue2);
    playTo(game, loop, now, cue2 + 0.1);
    loop.pushPadTap(lightfoot::Game::padIndex(s2.row, 0));
    now += 8;
    loop.step(now);
    check(game.lastHit() == lightfoot::Game::Hit::Good && game.score() == 160, "a late tap within the window is good for 60");
    // Ignore the one after: a miss costs a life.
    const int64_t cue3 = game.cueSlot();
    playTo(game, loop, now, cue3 + 0.5);
    check(game.lastHit() == lightfoot::Game::Hit::Miss && game.lives() == 2 && game.combo() == 0, "a missed cue costs a life and the combo");
    // Wrong (unlit) pad: nothing happens; the decoy would slip.
    const int64_t cue4 = game.cueSlot();
    const lightfoot::Slot& s4 = game.track().at(cue4);
    playTo(game, loop, now, cue4 - 0.02);
    int unlit = 0;
    while (unlit == s4.row || unlit == s4.row2 || unlit == s4.decoy) ++unlit;
    const int scoreBefore = game.score();
    loop.pushPadTap(lightfoot::Game::padIndex(unlit, 0));
    now += 8;
    loop.step(now);
    check(game.score() == scoreBefore && game.cueSlot() == cue4, "an unlit pad is inert");
    loop.pushPadTap(lightfoot::Game::padIndex(s4.row, 0));
    now += 8;
    loop.step(now);
    check(game.score() > scoreBefore, "the lit pad still works after an idle tap");
    // Miss everything else: game over, score saved.
    while (game.state() == lightfoot::Game::State::Playing && now < 400000) {
        now += 16;
        loop.step(now);
    }
    check(game.state() == lightfoot::Game::State::GameOver, "three misses end the run");
    check(game.scores().best() == game.score() && game.score() > 0, "the run is on the score table");
    lightfoot::Scores again;
    again.load();
    check(again.best() == game.score(), "the score table reloads from the card");
    unsetenv("LIGHTFOOT_ROOT");
}

void testTwinNeedsBothPads() {
    setenv("LIGHTFOOT_ROOT", "build/test-card", 1);
    lightfoot::Game game;
    game.setStartLevel(6);
    lightfoot::HostLoop loop(game);
    int64_t now = 1000;
    loop.step(now);
    game.startGame(now, 3);
    // Find the first twin and hit everything before it perfectly.
    int64_t k = 0;
    while (game.track().at(k).row2 < 0 && k < 2000) ++k;
    check(k < 2000, "a level-6 course has a twin");
    for (;;) {
        playTo(game, loop, now, 0.0);
        const int64_t cue = game.cueSlot();
        if (cue < 0 || cue > k) break;
        const lightfoot::Slot& s = game.track().at(cue);
        playTo(game, loop, now, cue - 0.005);
        if (cue == k) {
            loop.pushPadTap(lightfoot::Game::padIndex(s.row, 0));
            now += 8;
            loop.step(now);
            check(game.cueSlot() == k, "one pad of a twin leaves the cue pending");
            loop.push(lightfoot::panelmidi::padDown(lightfoot::Game::padIndex(s.row2, 0)));
            now += 8;
            loop.step(now);
            check(game.cueSlot() != k && game.lastHit() != lightfoot::Game::Hit::Miss, "the second pad completes the twin");
            break;
        }
        loop.pushPadTap(lightfoot::Game::padIndex(s.row, 0));
        now += 8;
        loop.step(now);
        if (game.state() != lightfoot::Game::State::Playing) break;
    }
    check(game.lives() == 3, "a twin hit with both pads loses nothing");
    unsetenv("LIGHTFOOT_ROOT");
}

void testMenuFlow() {
    setenv("LIGHTFOOT_ROOT", "build/test-card", 1);
    lightfoot::Game game;
    lightfoot::HostLoop loop(game);
    int64_t now = 1000;
    loop.step(now);
    check(game.state() == lightfoot::Game::State::Title, "starts on the title");
    loop.pushButtonTap(IN_B2);
    loop.step(now += 16);
    loop.step(now += 16);
    check(game.state() == lightfoot::Game::State::HowTo, "B2 opens how to play");
    loop.pushButtonTap(IN_B1);
    loop.step(now += 16);
    loop.step(now += 16);
    check(game.state() == lightfoot::Game::State::Title, "B1 goes back");
    loop.pushButtonTap(IN_PLAY);
    loop.step(now += 16);
    loop.step(now += 16);
    check(game.state() == lightfoot::Game::State::Playing, "PLAY starts a run");
    loop.pushButtonTap(IN_PLAY);
    loop.step(now += 16);
    loop.step(now += 16);
    check(game.state() == lightfoot::Game::State::Paused, "PLAY pauses");
    const double at = game.beat();
    loop.step(now += 500);
    check(game.beat() == at, "paused, the course stands still");
    loop.pushButtonTap(IN_STOP);
    loop.step(now += 16);
    loop.step(now += 16);
    check(game.state() == lightfoot::Game::State::Title, "STOP from pause returns to the title");
    unsetenv("LIGHTFOOT_ROOT");
}

} // namespace

int main() {
    std::system("rm -rf build/test-card"); // settings and scores from an earlier run must not leak in
    testPadMapping();
    testTrack();
    testEngineClockAndRender();
    testJudgeAndScore();
    testTwinNeedsBothPads();
    testMenuFlow();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
