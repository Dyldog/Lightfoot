// Lightfoot unit tests: the course generator, the judge, scoring, the clock,
// the synth, the pad mapping and the score table. `make test` runs them after
// the screenshot suite.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <string>
#include <vector>

#include "Audio/AudioEngine.hpp"
#include "Audio/MidiFile.hpp"
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
    game.setDifficulty(lightfoot::Game::Easy);
    lightfoot::HostLoop loop(game);
    int64_t now = 1000;
    loop.step(now);
    game.startGame(now, 7);
    check(game.state() == lightfoot::Game::State::Playing && game.rock() == 0.5f && game.score() == 0, "a run starts fresh");
    // Count-in, then the first cue.
    playTo(game, loop, now, 0.5);
    check(game.cueSlot() > 0, "a cue is pending after the count-in");
    const int64_t cue = game.cueSlot();
    const lightfoot::Slot& s = game.track().at(cue);
    // Too early is a slip: no points, a nick off the rock meter.
    playTo(game, loop, now, cue - 0.6);
    loop.pushPadTap(lightfoot::Game::padIndex(3, lightfoot::Game::laneOf(s.row)));
    now += 8;
    loop.step(now);
    check(game.lastHit() == lightfoot::Game::Hit::Slip && game.score() == 0 && game.rock() > 0.45f, "an early tap slips");
    // On the beat: perfect, on any pad of the lane.
    playTo(game, loop, now, cue - 0.01);
    loop.pushPadTap(lightfoot::Game::padIndex(1, lightfoot::Game::laneOf(s.row)));
    now += 8;
    loop.step(now);
    check(game.lastHit() == lightfoot::Game::Hit::Perfect, "a tap on the beat is perfect");
    check(game.score() == 100 && game.combo() == 1, "a perfect scores 100 with combo 1");
    check(game.cueSlot() != cue, "the cue is resolved");
    // The next cue: slightly late is good.
    const int64_t cue2 = game.cueSlot();
    const lightfoot::Slot& s2 = game.track().at(cue2);
    playTo(game, loop, now, cue2 + 0.1);
    loop.pushPadTap(lightfoot::Game::padIndex(3, lightfoot::Game::laneOf(s2.row)));
    now += 8;
    loop.step(now);
    check(game.lastHit() == lightfoot::Game::Hit::Good && game.score() == 160, "a late tap within the window is good for 60");
    // Ignore the one after: a miss drains the rock meter and the streak.
    const int64_t cue3 = game.cueSlot();
    const float rockBefore = game.rock();
    playTo(game, loop, now, cue3 + 0.5);
    check(game.lastHit() == lightfoot::Game::Hit::Miss && game.rock() < rockBefore - 0.05f && game.combo() == 0, "a missed gem drains the rock meter and the streak");
    // Wrong (unlit) pad: nothing happens; the decoy would slip.
    const int64_t cue4 = game.cueSlot();
    const lightfoot::Slot& s4 = game.track().at(cue4);
    playTo(game, loop, now, cue4 - 0.02);
    int unlit = 0;
    while (unlit == lightfoot::Game::laneOf(s4.row) || (s4.row2 >= 0 && unlit == lightfoot::Game::laneOf(s4.row2)) || (s4.decoy >= 0 && unlit == lightfoot::Game::laneOf(s4.decoy))) ++unlit;
    const int scoreBefore = game.score();
    const auto hitBefore = game.lastHit();
    loop.pushPadTap(lightfoot::Game::padIndex(3, unlit));
    now += 8;
    loop.step(now);
    check(game.score() == scoreBefore && game.cueSlot() == cue4 && (game.lastHit() == hitBefore || game.lastHit() == lightfoot::Game::Hit::Slip), "an unlit lane is inert or early, never a hit");
    loop.pushPadTap(lightfoot::Game::padIndex(3, lightfoot::Game::laneOf(s4.row)));
    now += 8;
    loop.step(now);
    check(game.score() > scoreBefore, "the lit pad still works after an idle tap");
    // Miss everything else: game over, score saved.
    while (game.state() == lightfoot::Game::State::Playing && now < 400000) {
        now += 16;
        loop.step(now);
    }
    check(game.state() == lightfoot::Game::State::GameOver && game.rock() <= 0.0f, "an empty rock meter ends the run");
    check(game.scores().best() == game.score() && game.score() > 0, "the run is on the score table");
    lightfoot::Scores again;
    again.load();
    check(again.best() == game.score(), "the score table reloads from the card");
    unsetenv("LIGHTFOOT_ROOT");
}

void testTwinNeedsBothPads() {
    setenv("LIGHTFOOT_ROOT", "build/test-card", 1);
    lightfoot::Game game;
    game.setDifficulty(lightfoot::Game::Expert); // starts at level 7
    lightfoot::HostLoop loop(game);
    int64_t now = 1000;
    loop.step(now);
    game.startGame(now, 3);
    // Find the first twin and hit everything before it perfectly.
    int64_t k = 0;
    while (game.track().at(k).row2 < 0 && k < 2000) ++k;
    check(k < 2000, "an expert course has a chord");
    for (;;) {
        playTo(game, loop, now, 0.0);
        const int64_t cue = game.cueSlot();
        if (cue < 0 || cue > k) break;
        const lightfoot::Slot& s = game.track().at(cue);
        playTo(game, loop, now, cue - 0.005);
        if (cue == k) {
            loop.pushPadTap(lightfoot::Game::padIndex(3, lightfoot::Game::laneOf(s.row)));
            now += 8;
            loop.step(now);
            check(game.cueSlot() == k, "one lane of a chord leaves the gem pending");
            loop.push(lightfoot::panelmidi::padDown(lightfoot::Game::padIndex(3, lightfoot::Game::laneOf(s.row2))));
            now += 8;
            loop.step(now);
            check(game.cueSlot() != k && game.lastHit() != lightfoot::Game::Hit::Miss, "the second lane completes the chord");
            break;
        }
        loop.pushPadTap(lightfoot::Game::padIndex(3, lightfoot::Game::laneOf(s.row)));
        now += 8;
        loop.step(now);
        if (game.state() != lightfoot::Game::State::Playing) break;
    }
    check(game.rock() >= 0.5f, "a chord hit with both lanes loses nothing");
    // Both pads in the same frame: one hit, no slip from the second pad.
    {
        lightfoot::Game g2;
        g2.setDifficulty(lightfoot::Game::Expert);
        lightfoot::HostLoop l2(g2);
        int64_t t2 = 1000;
        l2.step(t2);
        g2.startGame(t2, 3);
        int64_t kk = 0;
        while (g2.track().at(kk).row2 < 0 && kk < 2000) ++kk;
        for (;;) {
            playTo(g2, l2, t2, 0.0);
            const int64_t cue = g2.cueSlot();
            if (cue < 0 || cue > kk || g2.state() != lightfoot::Game::State::Playing) break;
            const lightfoot::Slot s = g2.track().at(cue);
            playTo(g2, l2, t2, cue - 0.005);
            l2.pushPadTap(lightfoot::Game::padIndex(3, lightfoot::Game::laneOf(s.row)));
            if (s.row2 >= 0) l2.pushPadTap(lightfoot::Game::padIndex(3, lightfoot::Game::laneOf(s.row2)));
            t2 += 8;
            l2.step(t2);
            if (cue == kk) {
                check(g2.lastHit() == lightfoot::Game::Hit::Perfect || g2.lastHit() == lightfoot::Game::Hit::Good, "a chord hit with both pads in one frame counts once as a hit");
                break;
            }
        }
    }
    unsetenv("LIGHTFOOT_ROOT");
}

void testMidiSong() {
    setenv("LIGHTFOOT_ROOT", "build/test-card", 1);
    lightfoot::paths::makeDirs("build/test-card/Lightfoot/Songs");
    const std::string path = "build/test-card/Lightfoot/Songs/Demo.mid";
    check(lightfoot::MidiSong::writeDemo(path), "demo MIDI written");
    std::string error;
    auto song = lightfoot::MidiSong::load(path, &error);
    check(song != nullptr, "demo MIDI parses");
    if (!song) {
        std::printf("  error: %s\n", error.c_str());
        return;
    }
    check(song->name == "Lightfoot Demo", "track name read");
    check(std::fabs(song->tempoAt(0) - 112.0) < 0.1, "tempo meta read");
    check(song->beatsPerBar == 4, "time signature read");
    check(song->hasDrums, "drums on channel 10 found");
    check(song->melodyChannel == 0, "the tune is picked as the melody, not the bass");
    check(std::fabs(song->lengthBeats - 64.0) < 0.5, "16 bars long");
    int tuneNotes = 0;
    for (const auto& n : song->notes)
        if (n.channel == 0) ++tuneNotes;
    check(tuneNotes == 14 * 4 - 4 + 7 * 2, "every tune note with a length");
    // Running status, a note held past the end, and junk are all handled.
    std::vector<uint8_t> junk = {'M', 'T', 'h', 'x'};
    check(lightfoot::MidiSong::parse(junk, &error) == nullptr, "junk is refused");
    std::vector<uint8_t> running = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k', 0, 0, 0, 12,
                                    0x00, 0x90, 60, 100, 0x60, 60, 0, 0x60, 64, 100, 0x00, 0xff};
    auto rs = lightfoot::MidiSong::parse(running);
    check(rs && rs->notes.size() == 2 && std::fabs(rs->notes[0].length - 1.0) < 1e-9 && std::fabs(rs->notes[1].beat - 2.0) < 1e-9, "running status and an unreleased note");

    // The course follows the tune: cues on beats where the tune plays, rows from pitch.
    // At level 1 the chart is the strong beats only; from level 5 it is every note.
    lightfoot::Track easy = lightfoot::Track::fromSong(*song, 1, lightfoot::Game::Easy);
    check(easy.finite() && easy.stepBeats() == 0.5 && easy.length() >= 128, "a song with off-beat notes runs in half-beat steps");
    int easyCues = 0, easyOff = 0;
    for (int64_t i = 0; i < easy.length(); ++i) {
        if (!easy.at(i).cue) continue;
        ++easyCues;
        if (i % 2 == 1) ++easyOff;
    }
    check(easyCues >= 24 && easyCues <= 30 && easyOff == 0, "level 1 plays the strong beats only");
    // Rows follow the shape of the tune, not its absolute pitch.
    {
        using lightfoot::Track;
        const std::vector<int> up = {60, 62, 64, 65, 67, 69, 71, 72};      // seven rises
        const std::vector<int> rows = Track::contourRows(up, 2);
        check(rows.front() == 3 && rows.back() == 0, "a long rise starts at the bottom and reaches the top");
        for (size_t i = 1; i < rows.size(); ++i) check(rows[i] <= rows[i - 1], "a rise never goes down");
        const std::vector<int> wave = {60, 62, 60, 62, 60};
        const std::vector<int> w = Track::contourRows(wave, 2);
        check(w[0] == 2 && w[1] == 1 && w[2] == 2 && w[3] == 1 && w[4] == 2, "short moves are one row each way");
        const std::vector<int> same = {60, 60, 60};
        const std::vector<int> sm = Track::contourRows(same, 1);
        check(sm[0] == 1 && sm[1] == 1 && sm[2] == 1, "a repeated note stays on its row");
        const std::vector<int> turn = {60, 62, 72, 71, 69, 67, 65, 64};     // then six falls
        const std::vector<int> tr = Track::contourRows(turn, 2);
        check(tr[2] == 0 && tr.back() == 3, "before a long fall the course climbs to the top, then falls to the bottom");
    }
    int usedRows = 0;
    {
        std::vector<int> seen(4, 0);
        for (int64_t i = 0; i < easy.length(); ++i)
            if (easy.at(i).cue) seen[static_cast<size_t>(easy.at(i).row)] = 1;
        for (int v : seen) usedRows += v;
    }
    check(usedRows >= 3, "even the easy chart moves across the rows");
    lightfoot::Track course = lightfoot::Track::fromSong(*song, 5, lightfoot::Game::Hard);
    // Difficulty alone fills the chart in: Hard at level 1 plays every note.
    lightfoot::Track hardAtOne = lightfoot::Track::fromSong(*song, 1, lightfoot::Game::Hard);
    int hardCues = 0;
    for (int64_t i = 0; i < hardAtOne.length(); ++i) hardCues += hardAtOne.at(i).cue ? 1 : 0;
    check(hardCues >= 50, "Hard plays every note from level 1");
    int cues = 0, offBeatCues = 0;
    for (int64_t i = 0; i < course.length(); ++i) {
        if (!course.at(i).cue) continue;
        ++cues;
        if (i % 2 == 1) ++offBeatCues;
    }
    check(cues >= 50 && cues <= 70, "from level 5 every tune note is a jump");
    int manual = 0, manualWithPitch = 0;
    for (const auto& n : song->notes) manual += n.manual ? 1 : 0;
    for (int64_t i = 0; i < course.length(); ++i)
        if (course.at(i).cue && course.at(i).note >= 0) ++manualWithPitch;
    check(manual == cues && manualWithPitch == cues, "the jumps' notes are the player's to sound");
    check(offBeatCues == 14, "the off-beat pickups are jumps between the beats");
    check(!course.at(4).cue && course.at(course.length() + 50).cue == false, "no jumps before bar 2 or after the end");
    check(easy.level(0) == 1 && easy.level(64) == 2 && easy.level(128) == 3, "song levels still change every 32 beats");
    lightfoot::Track expert = lightfoot::Track::fromSong(*song, 7, lightfoot::Game::Expert);
    lightfoot::Track medium = lightfoot::Track::fromSong(*song, 3, lightfoot::Game::Medium);
    int expertDecoys = 0, mediumDecoys = 0;
    for (int64_t i = 0; i < expert.length(); ++i) expertDecoys += expert.at(i).decoy >= 0 ? 1 : 0;
    for (int64_t i = 0; i < medium.length(); ++i) mediumDecoys += medium.at(i).decoy >= 0 ? 1 : 0;
    check(expert.level(0) == 7 && expertDecoys > 0 && mediumDecoys == 0, "Expert has decoys, Medium never does");
    check(std::string(lightfoot::Game::difficultyName(3)) == "EXPERT" && lightfoot::Game::startLevelFor(2) == 5, "difficulty names and start levels");
    // Highest tune note is the top row, lowest the bottom row.
    int topRow = 9, bottomRow = -1;
    for (int64_t i = 0; i < course.length(); ++i)
        if (course.at(i).cue) {
            topRow = std::min(topRow, course.at(i).row);
            bottomRow = std::max(bottomRow, course.at(i).row);
        }
    check(topRow == 0 && bottomRow == 3, "the melody spans all four rows");

    // Playing a song: notes come out of the synth, and the run ends with SONG CLEAR.
    lightfoot::Game game;
    game.setDifficulty(lightfoot::Game::Easy);
    check(game.loadSong(path), "the game loads the song");
    lightfoot::HostLoop loop(game);
    int64_t now = 1000;
    loop.step(now);
    game.startGame(now, 1);
    check(game.engine().song() != nullptr && game.track().finite(), "a song run uses the song and its course");
    std::vector<float> out(256 * 2);
    game.engine().setAttached(true);
    game.engine().setMode(lightfoot::AudioEngine::Mode::Play);
    float peak = 0;
    for (int b = 0; b < 600; ++b) {
        game.engine().render(out.data(), 256);
        for (float v : out) peak = std::max(peak, std::fabs(v));
    }
    check(peak > 0.05f, "the song makes sound");
    // The melody is not in that sound: only the pads sound it.
    {
        lightfoot::AudioEngine solo(44100);
        solo.setAttached(true);
        solo.setMode(lightfoot::AudioEngine::Mode::Play);
        solo.setTempo(112);
        auto quiet = std::make_shared<lightfoot::MidiSong>(*song);
        quiet->notes.erase(std::remove_if(quiet->notes.begin(), quiet->notes.end(), [&](const lightfoot::MidiNote& n) { return n.channel != quiet->melodyChannel; }), quiet->notes.end());
        for (auto& n : quiet->notes) n.manual = true; // the whole tune is the player's
        quiet->hasDrums = true;                     // no procedural pulse either
        solo.setSong(quiet.get());
        solo.resetBeat(8);
        float melodyPeak = 0;
        for (int b = 0; b < 300; ++b) {
            solo.render(out.data(), 256);
            for (float v : out) melodyPeak = std::max(melodyPeak, std::fabs(v));
        }
        check(melodyPeak < 0.03f, "manual melody notes stay silent until hit");
        solo.noteOn(quiet->melodyChannel, 72, 100, 0.5);
        float hitPeak = 0;
        for (int b = 0; b < 40; ++b) {
            solo.render(out.data(), 256);
            for (float v : out) hitPeak = std::max(hitPeak, std::fabs(v));
        }
        check(hitPeak > 0.05f, "a hit sounds the note");
        // The song page's audition: the chosen channel alone, manual or not,
        // and a channel with nothing in it is silence (no pulse added).
        solo.setPreview(quiet->melodyChannel);
        solo.seek(8);
        float auditionPeak = 0;
        for (int b = 0; b < 300; ++b) {
            solo.render(out.data(), 256);
            for (float v : out) auditionPeak = std::max(auditionPeak, std::fabs(v));
        }
        check(auditionPeak > 0.05f, "the audition plays the chosen channel on its own");
        solo.setPreview(15);
        solo.seek(8);
        float emptyPeak = 0;
        for (int b = 0; b < 300; ++b) {
            solo.render(out.data(), 256);
            if (b < 20) continue; // the cut-off notes' tails
            for (float v : out) emptyPeak = std::max(emptyPeak, std::fabs(v));
        }
        check(emptyPeak < 0.01f, "auditioning an empty channel is silence");
        solo.setPreview(-1);
        solo.setSong(nullptr);
    }
    game.engine().setAttached(false);
    // Let the run go on unattended: misses end it before the song does, so the
    // completion path is tested with a tapper.
    bool spedUp = false;
    while (game.state() == lightfoot::Game::State::Playing && now < 600000) {
        now += 8;
        spedUp |= game.engine().tempo() > 112.5;
        const int64_t cue = game.cueSlot();
        if (cue >= 0 && std::fabs(game.pos() - static_cast<double>(cue)) < 0.03) {
            const lightfoot::Slot s = game.track().at(cue);
            loop.pushPadTap(lightfoot::Game::padIndex(3, lightfoot::Game::laneOf(s.row)));
            if (s.row2 >= 0) loop.push(lightfoot::panelmidi::padDown(lightfoot::Game::padIndex(3, lightfoot::Game::laneOf(s.row2))));
        }
        loop.step(now);
    }
    check(game.state() == lightfoot::Game::State::GameOver && game.songComplete(), "playing the whole song ends in SONG CLEAR");
    check(game.rock() > 0.9f, "a perfectly tapped song fills the rock meter");
    check(game.level() >= 3 && spedUp, "a song rises through the levels and speeds up with them");
    check(game.scores().entries().front().song == "Lightfoot Demo", "the score remembers the song");
    unsetenv("LIGHTFOOT_ROOT");
}

// A two-channel file: a busy overdriven guitar playing chords, and a flute
// playing a sparse single line. The flute is the lead.
std::vector<uint8_t> guitarAndFlute() {
    std::vector<uint8_t> track;
    auto varlen = [&](uint32_t v) {
        uint8_t buf[4];
        int n = 0;
        do {
            buf[n++] = static_cast<uint8_t>(v & 0x7f);
            v >>= 7;
        } while (v && n < 4);
        for (int i = n - 1; i >= 0; --i) track.push_back(static_cast<uint8_t>(buf[i] | (i ? 0x80 : 0)));
    };
    struct Ev {
        uint32_t tick;
        uint8_t a, b, c;
        bool two;
    };
    std::vector<Ev> ev;
    ev.push_back({0, 0xc0, 29, 0, true});  // ch 1: overdriven guitar
    ev.push_back({0, 0xc1, 73, 0, true});  // ch 2: flute
    for (int beat = 0; beat < 64; ++beat) {
        const uint32_t t = static_cast<uint32_t>(beat) * 96;
        for (int n : {52, 59, 64, 67}) { // a power chord every beat, and again on the off-beat
            ev.push_back({t, 0x90, static_cast<uint8_t>(n), 100, false});
            ev.push_back({t + 40, 0x80, static_cast<uint8_t>(n), 0, false});
            ev.push_back({t + 48, 0x90, static_cast<uint8_t>(n), 90, false});
            ev.push_back({t + 88, 0x80, static_cast<uint8_t>(n), 0, false});
        }
        if (beat % 2 == 0) { // the flute, one note every other beat, walking up and down
            const int note = 72 + (beat / 2) % 7;
            ev.push_back({t, 0x91, static_cast<uint8_t>(note), 100, false});
            ev.push_back({t + 150, 0x81, static_cast<uint8_t>(note), 0, false});
        }
    }
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& x, const Ev& y) { return x.tick < y.tick; });
    uint32_t last = 0;
    for (const Ev& e : ev) {
        varlen(e.tick - last);
        last = e.tick;
        track.push_back(e.a);
        track.push_back(e.b);
        if (!e.two) track.push_back(e.c);
    }
    const uint8_t endMeta[] = {0x00, 0xff, 0x2f, 0x00};
    track.insert(track.end(), endMeta, endMeta + 4);
    std::vector<uint8_t> out = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k'};
    const uint32_t len = static_cast<uint32_t>(track.size());
    out.push_back(static_cast<uint8_t>(len >> 24));
    out.push_back(static_cast<uint8_t>(len >> 16));
    out.push_back(static_cast<uint8_t>(len >> 8));
    out.push_back(static_cast<uint8_t>(len));
    out.insert(out.end(), track.begin(), track.end());
    return out;
}

void testLeadDetection() {
    std::string error;
    auto song = lightfoot::MidiSong::parse(guitarAndFlute(), &error);
    check(song != nullptr, "the guitar-and-flute file parses");
    if (!song) return;
    check(song->channels[0].program == 29 && song->channels[1].program == 73, "programs are read per channel");
    check(song->channels[0].notes == 512 && song->channels[1].notes == 32, "note counts per channel");
    check(song->channels[1].monophony > 0.99f && song->channels[0].monophony < 0.1f, "monophony tells a line from chords");
    check(song->melodyChannel == 1, "the sparse flute is the lead, not the busy guitar");
    check(song->candidates.size() == 2 && song->candidates[0] == 1, "candidates are ordered best first");
    check(std::string(lightfoot::MidiSong::programName(73)) == "Flute", "program names");
    // The song screen lets the wheel change the lead and remembers it.
    setenv("LIGHTFOOT_ROOT", "build/test-card", 1);
    lightfoot::paths::makeDirs("build/test-card/Lightfoot/Songs");
    {
        std::ofstream f("build/test-card/Lightfoot/Songs/Duet.mid", std::ios::binary);
        const std::vector<uint8_t> bytes = guitarAndFlute();
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    lightfoot::Game game;
    lightfoot::HostLoop loop(game);
    int64_t now = 1000;
    loop.step(now);
    check(game.loadSong("build/test-card/Lightfoot/Songs/Duet.mid"), "duet loads");
    game.setState(lightfoot::Game::State::SongInfo, now);
    check(game.engine().preview() == game.song()->melodyChannel && game.engine().song() == game.song(), "the song page auditions the guessed lead on its own");
    loop.push(lightfoot::panelmidi::encoder(1));
    loop.step(now += 16);
    check(game.song()->melodyChannel == 0, "the wheel moves the lead to the next candidate");
    check(game.engine().preview() == 0, "the audition follows the wheel");
    loop.push(lightfoot::panelmidi::encoder(5));
    loop.step(now += 16);
    check(game.song()->melodyChannel == 0, "the wheel stops at the last candidate");
    loop.pushPadTap(8); // pad 9: nine sixteenths in, to the bar
    loop.step(now += 16);
    check(std::fabs(game.engine().positionBeats() - 32.0) < 0.5, "a pad jumps the audition to that sixteenth of the song, on a bar");
    loop.pushButtonTap(IN_PLAY);
    loop.step(now += 16);
    loop.step(now += 16);
    check(game.state() == lightfoot::Game::State::Playing, "PLAY starts the song from the song screen");
    check(game.engine().preview() < 0, "the run plays the whole band again");
    lightfoot::Game again;
    check(again.loadSong("build/test-card/Lightfoot/Songs/Duet.mid") && again.song()->melodyChannel == 0, "the chosen lead is remembered for the song");
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
    check(game.state() == lightfoot::Game::State::Songs, "B2 opens the songs list");
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
    testMidiSong();
    testLeadDetection();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
