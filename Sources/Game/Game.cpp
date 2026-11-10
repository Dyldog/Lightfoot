#include "Game/Game.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Shared/Drawing/Canvas.hpp"
#include "Shared/Input/Frame.hpp"
#include "Shared/Json/Json.hpp"
#include "Shared/Paths/Paths.hpp"

#include <unistd.h>

extern "C" {
#include "filebrowser.h"
}

namespace lightfoot {

namespace {

#ifndef LIGHTFOOT_VERSION
#define LIGHTFOOT_VERSION "dev"
#endif

constexpr double kGoodSeconds = 0.14;     // hit within this of the beat: GOOD
constexpr double kPerfectSeconds = 0.055; // within this: PERFECT
constexpr double kOutputLatency = 0.022;  // what the codec buffer adds before the beat is heard
constexpr double kCountIn = 4;            // beats of music before slot 0
constexpr double kMenuTempo = 96;

} // namespace

const char* Game::version() {
    return LIGHTFOOT_VERSION;
}

Game::~Game() = default;

std::string Game::songsDir() {
    return paths::join(paths::dataDir(), "Songs");
}

Game::Game() : palette_(Palette::make(0)) {
    scores_.load();
    loadSettings();
    ensureDemoSong();
    refreshSongList();
    palette_ = Palette::make(hue_);
    engine_.setTempo(kMenuTempo);
    engine_.setMode(AudioEngine::Mode::Menu);
    engine_.setIntensity(1);
}

// MARK: - Settings

void Game::loadSettings() {
    std::string text;
    if (!paths::readFile(paths::join(paths::dataDir(), "settings.json"), text)) return;
    const Json j = Json::parse(text);
    hue_ = static_cast<float>(j["hue"].asNumber(0));
    startLevel_ = std::clamp(j["startLevel"].asInt(1), 1, 8);
}

void Game::saveSettings() const {
    Json j = Json::object();
    j.set("hue", static_cast<double>(hue_));
    j.set("startLevel", startLevel_);
    paths::makeDirs(paths::dataDir());
    paths::writeFileAtomic(paths::join(paths::dataDir(), "settings.json"), j.dump(2) + "\n");
}

void Game::ensureDemoSong() {
    // A song to try before any are copied over: written once, never overwritten.
    paths::makeDirs(songsDir());
    if (paths::listFiles(songsDir(), ".mid").empty() && paths::listFiles(songsDir(), ".midi").empty())
        MidiSong::writeDemo(paths::join(songsDir(), "Lightfoot Demo.mid"));
}

void Game::refreshSongList() {
    songFiles_ = paths::listFiles(songsDir(), ".mid");
    for (const std::string& f : paths::listFiles(songsDir(), ".midi")) songFiles_.push_back(f);
    std::sort(songFiles_.begin(), songFiles_.end());
    songCursor_ = std::clamp(songCursor_, 0, static_cast<int>(songFiles_.size()) + 1);
}

bool Game::loadSong(const std::string& path) {
    std::string error;
    std::shared_ptr<MidiSong> song = MidiSong::load(path, &error);
    if (!song) {
        banner_ = "CAN'T READ THAT FILE";
        bannerUntil_ = lastFrameMs_ + 2000;
        std::fprintf(stderr, "song: %s: %s\n", path.c_str(), error.c_str());
        return false;
    }
    song->trimLeadIn(4);
    loadedSongs_.push_back(song);
    if (loadedSongs_.size() > 8) loadedSongs_.erase(loadedSongs_.begin());
    song_ = song;
    std::fprintf(stderr, "song: %s, %zu notes, %.0f BPM, %.0f beats, melody channel %d, drums %d\n", song->name.c_str(), song->notes.size(),
                 song->tempoAt(0), song->lengthBeats, song->melodyChannel + 1, song->hasDrums ? 1 : 0);
    return true;
}

void Game::openBrowser() {
    browser_ = std::make_unique<struct ui_filebrowser>();
    browserTitle_ = "LOAD A SONG";
    static const char* extensions[] = {".mid", ".midi", nullptr};
    struct ui_filebrowser_config cfg{};
    cfg.title = browserTitle_.c_str();
    cfg.action_label = "PLAY";
    cfg.options = UI_FB_OPT_HIDE_DOTFILES | UI_FB_OPT_SHOW_SIZE;
    cfg.extensions = extensions;
    auto absolute = [](const std::string& p) {
        if (!p.empty() && p[0] == '/') return p;
        char buf[4096];
        if (!::getcwd(buf, sizeof buf)) return p;
        return paths::join(buf, p);
    };
    if (!paths::isDirectory("/data") && !paths::isDirectory("/media")) {
        browserExternal_ = absolute(paths::cardRoot());
        const char* home = std::getenv("HOME");
        browserInternal_ = home ? home : "/";
        cfg.external_path = browserExternal_.c_str();
        cfg.internal_path = browserInternal_.c_str();
    }
    browserStart_ = absolute(songsDir());
    if (paths::isDirectory(browserStart_)) cfg.initial_path = browserStart_.c_str();
    ui_filebrowser_init(browser_.get(), &cfg);
}

void Game::setHue(float hue) {
    hue_ = hue - std::floor(hue);
    palette_ = Palette::make(hue_);
}

void Game::setStartLevel(int level) {
    startLevel_ = std::clamp(level, 1, 8);
}

// MARK: - Timing helpers

double Game::stepBeats() const {
    return track_.stepBeats();
}

double Game::songSpeed() const {
    return 1.0 + 0.05 * (level_ - 1);
}

// Windows are in steps. In seconds they shrink when steps are short, so two
// neighbouring cues never share a window.
double Game::windowGood() const {
    const double stepSeconds = stepBeats() * 60.0 / std::max(30.0, engine_.tempo());
    return std::min(kGoodSeconds, 0.42 * stepSeconds) / stepSeconds;
}

double Game::windowPerfect() const {
    const double stepSeconds = stepBeats() * 60.0 / std::max(30.0, engine_.tempo());
    return std::min(kPerfectSeconds, 0.2 * stepSeconds) / stepSeconds;
}

double Game::beatPhase() const {
    return beat_ - std::floor(beat_);
}

double Game::stepPhase() const {
    return pos_ - std::floor(pos_);
}

int64_t Game::columnBase() const {
    // The left pad column is the jump that is due next: the slot ahead, or the
    // slot just entered while its window is still open.
    return (cueSlot_ >= 0 && cueSlot_ == slot_) ? slot_ : slot_ + 1;
}

// MARK: - Flow

void Game::setState(State s, int64_t nowMs) {
    state_ = s;
    stateSince_ = nowMs;
    switch (s) {
    case State::Title:
    case State::HowTo:
    case State::Scores:
    case State::Songs:
    case State::Browse:
    case State::GameOver:
        engine_.setPaused(false);
        engine_.setMode(AudioEngine::Mode::Menu);
        engine_.setTempo(kMenuTempo);
        engine_.setIntensity(1);
        break;
    case State::Playing:
        engine_.setPaused(false);
        engine_.setMode(AudioEngine::Mode::Play);
        break;
    case State::Paused:
        engine_.setPaused(true);
        break;
    }
}

void Game::startGame(int64_t nowMs, uint32_t seed) {
    if (seed == 0) seed = static_cast<uint32_t>(nowMs * 2654435761u) | 1u;
    songComplete_ = false;
    if (song_) {
        track_ = Track::fromSong(*song_, startLevel_);
        engine_.setSong(song_.get());
    } else {
        track_ = Track(seed, startLevel_);
        engine_.setSong(nullptr);
    }
    score_ = combo_ = maxCombo_ = 0;
    perfects_ = goods_ = misses_ = slips_ = 0;
    lives_ = 3;
    level_ = startLevel_;
    slot_ = -static_cast<int64_t>(kCountIn / track_.stepBeats()) - 1;
    lastCount_ = 0;
    cueSlot_ = -1;
    resolvedUpTo_ = -1;
    twinHalf_ = false;
    runnerRow_ = 3;
    jumpFrom_ = jumpTo_ = 3;
    jumpStart_ = jumpEnd_ = -1;
    landed_ = true;
    fallStart_ = -1;
    graceUntil_ = -1;
    lastHit_ = Hit::None;
    finalRank_ = -1;
    particles_.clear();
    texts_.clear();
    shake_ = flash_ = 0;
    banner_.clear();
    gameStartMs_ = nowMs;
    engine_.resetBeat(-kCountIn);
    beat_ = -kCountIn;
    pos_ = beat_ / track_.stepBeats();
    setState(State::Playing, nowMs);
    applyLevel(level_, false, nowMs);
    engine_.setBrightness(0);
    engine_.setMelodyGate(true);
    engine_.trigger(Sfx::Start);
    saveSettings();
}

void Game::applyLevel(int level, bool announce, int64_t nowMs) {
    level_ = level;
    engine_.setTempo((song_ ? song_->tempoAt(std::max(0.0, beat_)) * songSpeed() : track_.tempoFor(level)) * assist_);
    engine_.setIntensity(level);
    if (announce) {
        char text[48];
        if (song_) std::snprintf(text, sizeof text, "LEVEL %d   x%.2f", level, songSpeed());
        else std::snprintf(text, sizeof text, "LEVEL %d   %.0f BPM", level, track_.tempoFor(level));
        banner_ = text;
        bannerUntil_ = nowMs + 1800;
        flash_ = 0.45f;
        flashColor_ = palette_.white;
        engine_.trigger(Sfx::LevelUp);
    }
}

void Game::endGame(int64_t nowMs) {
    finalRank_ = score_ > 0 ? scores_.add(score_, level_, maxCombo_, song_ ? song_->name : std::string()) : -1;
    setState(State::GameOver, nowMs);
    engine_.setSong(nullptr);
    engine_.trigger(songComplete_ ? Sfx::LevelUp : Sfx::GameOver);
    banner_.clear();
}

// MARK: - Frame

int Game::frame(ui* u, const input* rawIn) {
    Canvas canvas(u);
    Frame in(*rawIn);
    const int64_t now = canvas.nowMs();
    if (lastFrameMs_ == 0) lastFrameMs_ = now;
    const float dt = std::clamp((now - lastFrameMs_) / 1000.0f, 0.0f, 0.1f);
    lastFrameMs_ = now;

    if (in.mainMoved()) {
        const float norm = in.main() / 127.0f;
        engine_.setMasterGain(norm * norm * (1.5f - 0.5f * norm));
    }
    handleKnobs(in);
    update(canvas, in, now);
    updateEffects(dt);
    draw(canvas, in, now);
    drawPanelLights(canvas, now);
    canvas.wake(16);
    if (quit_) canvas.quit();
    return quit_ ? 1 : 0;
}

void Game::handleKnobs(const Frame& in) {
    if (in.knobMoved(0)) setHue(in.knob(0) / 128.0f);
    if (in.knobMoved(1)) brightness_ = in.knob(1) / 127.0f;
    if (in.knobMoved(2) && state_ != State::Playing && state_ != State::Paused) {
        const int level = 1 + in.knob(2) * 8 / 128;
        if (level != startLevel_) {
            setStartLevel(level);
            engine_.trigger(Sfx::Tick);
        }
    }
    if (in.faderMoved()) assist_ = 0.75f + 0.25f * (in.fader() / 127.0f);
    engine_.setBrightness(std::max(brightness_, std::min(1.0f, combo_ / 40.0f)));
}

void Game::update(Canvas& canvas, const Frame& in, int64_t nowMs) {
    // SHIFT + STOP twice leaves to stock from anywhere.
    if (in.shift() && in.pressed(IN_STOP)) {
        if (nowMs - quitArmedMs_ < 2500) quit_ = true;
        else {
            quitArmedMs_ = nowMs;
            banner_ = "SHIFT + STOP AGAIN TO QUIT";
            bannerUntil_ = nowMs + 2500;
        }
        return;
    }
    switch (state_) {
    case State::Title: updateTitle(canvas, in, nowMs); break;
    case State::Songs: updateSongs(canvas, in, nowMs); break;
    case State::Browse: updateBrowser(canvas, in, nowMs); break;
    case State::HowTo:
    case State::Scores:
        if (in.pressed(IN_PLAY)) startGame(nowMs);
        else if (in.pressed(IN_B1) || in.pressed(IN_B3) || in.pressed(IN_STOP) || in.encoderPressed()) {
            engine_.trigger(Sfx::Select);
            setState(State::Title, nowMs);
        }
        break;
    case State::Playing: updatePlaying(canvas, in, nowMs); break;
    case State::Paused:
        if (in.pressed(IN_PLAY) || in.pressed(IN_B1)) {
            setState(State::Playing, nowMs);
            engine_.trigger(Sfx::Select);
        } else if (in.pressed(IN_STOP) || in.pressed(IN_B3)) {
            setState(State::Title, nowMs);
        }
        break;
    case State::GameOver:
        if (nowMs - stateSince_ < 600) break; // let the moment land before a stray press restarts
        if (in.pressed(IN_PLAY) || in.pressed(IN_B1)) startGame(nowMs);
        else if (in.pressed(IN_STOP) || in.pressed(IN_B3) || in.encoderPressed()) {
            engine_.trigger(Sfx::Select);
            setState(State::Title, nowMs);
        }
        break;
    }
    if (state_ != State::Playing) engine_.beat(nowMs); // keep the menu clock moving for the lights
}

void Game::updateTitle(Canvas& canvas, const Frame& in, int64_t nowMs) {
    (void)canvas;
    constexpr int kItems = 5;
    if (const int d = in.encoder()) {
        menuCursor_ = (menuCursor_ + d % kItems + kItems) % kItems;
        engine_.trigger(Sfx::Tick);
    }
    if (in.pressed(IN_PLAY) || in.pressed(IN_B1)) {
        startGame(nowMs);
        return;
    }
    if (in.pressed(IN_B2)) {
        refreshSongList();
        setState(State::Songs, nowMs);
        engine_.trigger(Sfx::Select);
        return;
    }
    if (in.pressed(IN_B3)) {
        setState(State::Scores, nowMs);
        engine_.trigger(Sfx::Select);
        return;
    }
    if (in.encoderPressed()) {
        engine_.trigger(Sfx::Select);
        switch (menuCursor_) {
        case 0: startGame(nowMs); break;
        case 1:
            refreshSongList();
            setState(State::Songs, nowMs);
            break;
        case 2: setState(State::HowTo, nowMs); break;
        case 3: setState(State::Scores, nowMs); break;
        default: quit_ = true; break;
        }
        return;
    }
    // Pads play a little chime and ripple the mirror, to invite a tap.
    for (int p = 0; p < 16; ++p)
        if (in.padHit(p) >= 0) {
            engine_.trigger(Sfx::Select);
            padFlashMs_[static_cast<size_t>(p)] = nowMs;
            padFlashColor_[static_cast<size_t>(p)] = palette_.white;
            spawnBurst(static_cast<float>(160 + (padCol(p) - 2) * 30), static_cast<float>(205 - padRow(p) * 6), palette_.row[padRow(p)], 10, 60);
        }
}

void Game::updateSongs(Canvas& canvas, const Frame& in, int64_t nowMs) {
    (void)canvas;
    // Rows: endless course, each file, browse the card.
    const int rows = static_cast<int>(songFiles_.size()) + 2;
    if (const int d = in.encoder()) {
        songCursor_ = std::clamp(songCursor_ + d, 0, rows - 1);
        engine_.trigger(Sfx::Tick);
    }
    if (in.pressed(IN_B1) || in.pressed(IN_STOP)) {
        engine_.trigger(Sfx::Select);
        setState(State::Title, nowMs);
        return;
    }
    const bool choose = in.encoderPressed() || in.pressed(IN_B2) || in.pressed(IN_B3) || in.pressed(IN_PLAY);
    if (!choose) return;
    engine_.trigger(Sfx::Select);
    if (songCursor_ == 0) {
        song_.reset();
        if (in.pressed(IN_PLAY)) startGame(nowMs);
        else setState(State::Title, nowMs);
        return;
    }
    if (songCursor_ == rows - 1) {
        openBrowser();
        setState(State::Browse, nowMs);
        return;
    }
    const std::string path = paths::join(songsDir(), songFiles_[static_cast<size_t>(songCursor_ - 1)]);
    if (loadSong(path)) startGame(nowMs);
}

void Game::updateBrowser(Canvas& canvas, const Frame& in, int64_t nowMs) {
    if (!browser_) {
        setState(State::Songs, nowMs);
        return;
    }
    struct ui_filebrowser_style style = ui_filebrowser_style_default();
    style.bg = 0x0b0914;
    style.box = palette_.platform;
    style.text = palette_.text;
    style.text_dim = palette_.textDim;
    style.drive_color = palette_.accent;
    style.highlight = palette_.accent;
    style.highlight_fill = Palette::mix(palette_.platform, palette_.accent, 60);
    style.title = palette_.accent;
    style.border = palette_.textDim;
    const int rc = ui_filebrowser(canvas.raw(), 0, 0, Canvas::width, Canvas::height, browser_.get(), style, &in.raw());
    if (rc == UI_FB_CANCEL) {
        browser_.reset();
        setState(State::Songs, nowMs);
    } else if (rc == UI_FB_SELECT && !browser_->selected_is_dir) {
        const std::string path = browser_->selected_path;
        if (loadSong(path)) {
            browser_.reset();
            startGame(nowMs);
        }
    }
}

void Game::updatePlaying(Canvas& canvas, const Frame& in, int64_t nowMs) {
    (void)canvas;
    if (in.pressed(IN_PLAY) || in.pressed(IN_STOP)) {
        setState(State::Paused, nowMs);
        return;
    }
    engine_.setTempo((song_ ? song_->tempoAt(std::max(0.0, beat_)) * songSpeed() : track_.tempoFor(level_)) * assist_);
    advance(nowMs);
    if (state_ != State::Playing) return; // the run ended inside advance
    handlePads(in, nowMs);
}

void Game::advance(int64_t nowMs) {
    const double latencyBeats = kOutputLatency * engine_.tempo() / 60.0;
    beat_ = engine_.beat(nowMs) - latencyBeats;
    pos_ = beat_ / stepBeats();
    const int64_t newSlot = static_cast<int64_t>(std::floor(pos_));
    if (newSlot != slot_) {
        slot_ = newSlot;
        if (slot_ >= 0) {
            const int lvl = track_.level(slot_);
            if (lvl != level_) applyLevel(lvl, true, nowMs);
        }
        if (slot_ == 0) {
            banner_ = "GO";
            bannerUntil_ = nowMs + 600;
        }
    }
    if (beat_ < 0) {
        const int count = static_cast<int>(std::ceil(-beat_ - 1e-9));
        if (count >= 1 && count <= 3 && count != lastCount_) {
            lastCount_ = count;
            banner_ = std::to_string(count);
            bannerUntil_ = nowMs + 500;
        }
    }
    if (!landed_ && pos_ >= jumpEnd_) {
        landed_ = true;
        engine_.trigger(Sfx::Land);
        spawnBurst(96, static_cast<float>(rowY(runnerRow_)), palette_.row[runnerRow_], 8, 40);
    }
    if (track_.finite() && slot_ >= track_.length() + 2 && cueSlot_ < 0) {
        songComplete_ = true;
        endGame(nowMs);
        return;
    }
    if (fallStart_ >= 0 && pos_ >= fallStart_ + 0.8 / stepBeats()) fallStart_ = -1;
    findNextCue();
    while (cueSlot_ >= 0 && pos_ > cueSlot_ + windowGood()) {
        miss(nowMs);
        if (state_ != State::Playing) return;
        findNextCue();
    }
}

void Game::findNextCue() {
    if (cueSlot_ >= 0) return;
    int64_t k = std::max<int64_t>(std::max(resolvedUpTo_ + 1, slot_), 0);
    for (int i = 0; i < 96; ++i, ++k) {
        if (track_.at(k).cue) {
            cueSlot_ = k;
            return;
        }
    }
    cueSlot_ = -1;
}

void Game::handlePads(const Frame& in, int64_t nowMs) {
    for (int p = 0; p < 16; ++p)
        if (in.padHit(p) >= 0) judge(padRow(p), padCol(p), in, nowMs);
}

void Game::judge(int row, int col, const Frame& in, int64_t nowMs) {
    if (fallStart_ >= 0) return; // tumbling: nothing counts until the runner is back
    const int64_t base = columnBase();
    const int64_t k = base + col;
    const Slot& s = track_.at(k);
    if (col > 0) {
        // A lit pad in a later column is a jump that is not due yet.
        if (s.cue && (row == s.row || row == s.row2 || row == s.decoy)) slip("EARLY", nowMs);
        return;
    }
    if (!s.cue || k != cueSlot_) return; // nothing due in the left column: an idle tap
    if (row == s.decoy) {
        slip("DECOY", nowMs);
        return;
    }
    if (row != s.row && row != s.row2) return; // an unlit pad does nothing
    const double delta = pos_ - static_cast<double>(k);
    if (delta < -windowGood()) {
        slip("EARLY", nowMs);
        return;
    }
    padFlashMs_[static_cast<size_t>(padIndex(row, 0))] = nowMs;
    padFlashColor_[static_cast<size_t>(padIndex(row, 0))] = palette_.white;
    if (s.row2 >= 0) {
        // A twin needs both pads: together, or the second within the window.
        const int other = row == s.row ? s.row2 : s.row;
        const bool otherDown = in.padDown(padIndex(other, 0)) || (twinHalf_ && twinHalfRow_ == other);
        if (!otherDown) {
            twinHalf_ = true;
            twinHalfRow_ = row;
            twinHalfDelta_ = delta;
            return;
        }
        const double worst = std::max(std::fabs(delta), twinHalf_ ? std::fabs(twinHalfDelta_) : 0.0);
        twinHalf_ = false;
        resolve(worst <= windowPerfect() ? Hit::Perfect : Hit::Good, worst, nowMs);
        return;
    }
    resolve(std::fabs(delta) <= windowPerfect() ? Hit::Perfect : Hit::Good, delta, nowMs);
}

void Game::resolve(Hit hit, double deltaBeats, int64_t nowMs) {
    (void)deltaBeats;
    const Slot& s = track_.at(cueSlot_);
    const bool twin = s.row2 >= 0;
    const int multiplier = 1 + std::min(combo_, 70) / 10;
    int points = (hit == Hit::Perfect ? 100 : 60) * multiplier * (twin ? 2 : 1);
    if (assist_ < 0.999f) points /= 2;
    score_ += points;
    ++combo_;
    maxCombo_ = std::max(maxCombo_, combo_);
    if (hit == Hit::Perfect) ++perfects_;
    else ++goods_;
    lastHit_ = hit;
    lastHitMs_ = nowMs;

    jumpFrom_ = runnerRow_;
    jumpTo_ = s.row;
    jumpStart_ = pos_;
    jumpEnd_ = static_cast<double>(cueSlot_) + (twin ? 1.5 : 0.5);
    if (jumpEnd_ < jumpStart_ + 0.25) jumpEnd_ = jumpStart_ + 0.25;
    landed_ = false;
    runnerRow_ = jumpTo_;

    engine_.trigger(twin ? Sfx::TwinJump : Sfx::Jump);
    if (song_ && s.note >= 0) {
        // You are playing the tune: the hit is the note.
        const int velocity = hit == Hit::Perfect ? std::min(127, s.velocity + 10) : std::max(40, s.velocity - 15);
        engine_.noteOn(song_->melodyChannel, s.note, velocity, s.noteBeats);
        if (s.note2 >= 0) engine_.noteOn(song_->melodyChannel, s.note2, velocity, s.noteBeats);
        engine_.setMelodyGate(true);
        if (hit == Hit::Perfect) engine_.trigger(Sfx::Tick);
    } else {
        engine_.trigger(hit == Hit::Perfect ? Sfx::Perfect : Sfx::Good);
    }
    const uint32_t color = hit == Hit::Perfect ? palette_.white : palette_.row[s.row];
    spawnBurst(96, static_cast<float>(rowY(jumpFrom_)), palette_.row[jumpFrom_], hit == Hit::Perfect ? 18 : 10, 70);
    char label[32];
    std::snprintf(label, sizeof label, "%s +%d", hit == Hit::Perfect ? "PERFECT" : "GOOD", points);
    addText(label, 96, static_cast<float>(rowY(jumpFrom_) - 30), color, hit == Hit::Perfect ? 1 : 0);
    if (twin) addText("LONG JUMP", 96, static_cast<float>(rowY(jumpFrom_) - 44), palette_.accentHot, 0);
    if (combo_ > 0 && combo_ % 10 == 0) {
        std::snprintf(label, sizeof label, "%d COMBO", combo_);
        addText(label, 160, 70, palette_.accentHot, 1);
    }
    resolvedUpTo_ = cueSlot_;
    cueSlot_ = -1;
    findNextCue();
}

void Game::slip(const char* why, int64_t nowMs) {
    combo_ = 0;
    ++slips_;
    lastHit_ = Hit::Slip;
    lastHitMs_ = nowMs;
    shake_ = std::max(shake_, 0.35f);
    engine_.trigger(Sfx::Slip);
    addText(why, 96, static_cast<float>(rowY(runnerRow_) - 30), palette_.danger, 0);
}

void Game::miss(int64_t nowMs) {
    const Slot& s = track_.at(cueSlot_);
    ++misses_;
    combo_ = 0;
    twinHalf_ = false;
    lastHit_ = Hit::Miss;
    lastHitMs_ = nowMs;
    const bool graced = pos_ < graceUntil_;
    engine_.setMelodyGate(false); // the tune drops out until the next hit
    if (graced) {
        addText("MISS", 96, static_cast<float>(rowY(runnerRow_) - 30), palette_.textDim, 0);
    } else {
        --lives_;
        engine_.trigger(Sfx::Miss);
        shake_ = 1.0f;
        flash_ = 0.6f;
        flashColor_ = palette_.danger;
        fallStart_ = pos_;
        graceUntil_ = static_cast<double>(cueSlot_) + 3.0 / stepBeats();
        addText(lives_ > 0 ? "MISS" : "MISS  GAME OVER", 96, static_cast<float>(rowY(runnerRow_) - 30), palette_.danger, 1);
        for (size_t p = 0; p < 16; ++p) {
            padFlashMs_[p] = nowMs;
            padFlashColor_[p] = palette_.danger;
        }
    }
    // The runner picks up again on the platform after the one they missed.
    runnerRow_ = s.gap ? track_.at(cueSlot_ + 1).row : s.row;
    landed_ = true;
    jumpStart_ = jumpEnd_ = -1;
    resolvedUpTo_ = cueSlot_;
    cueSlot_ = -1;
    if (lives_ <= 0) endGame(nowMs);
}

// MARK: - Effects state

void Game::spawnBurst(float x, float y, uint32_t color, int count, float speed) {
    for (int i = 0; i < count; ++i) {
        const float a = static_cast<float>(i) / count * 6.2831853f + static_cast<float>(nowSeed_() % 100) * 0.01f;
        const float v = speed * (0.4f + 0.6f * static_cast<float>(nowSeed_() % 100) / 100.0f);
        Particle p{x, y, std::cos(a) * v, std::sin(a) * v - speed * 0.5f, 0.0f, 0.5f + static_cast<float>(nowSeed_() % 50) / 100.0f, color, 1 + static_cast<int>(nowSeed_() % 2)};
        particles_.push_back(p);
    }
    if (particles_.size() > 400) particles_.erase(particles_.begin(), particles_.begin() + static_cast<long>(particles_.size() - 400));
}

void Game::addText(const std::string& text, float x, float y, uint32_t color, int scale) {
    texts_.push_back({text, x, y, 0.0f, color, scale});
    if (texts_.size() > 12) texts_.erase(texts_.begin());
}

void Game::updateEffects(float dt) {
    for (Particle& p : particles_) {
        p.life += dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.vy += 180.0f * dt;
        p.vx *= 1.0f - 1.5f * dt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.life >= p.max; }), particles_.end());
    for (FloatText& t : texts_) {
        t.life += dt;
        t.y -= 22.0f * dt;
    }
    texts_.erase(std::remove_if(texts_.begin(), texts_.end(), [](const FloatText& t) { return t.life > 0.9f; }), texts_.end());
    shake_ = std::max(0.0f, shake_ - dt * 2.2f);
    flash_ = std::max(0.0f, flash_ - dt * 1.6f);
    const float speed = state_ == State::Playing && landed_ && fallStart_ < 0 ? static_cast<float>(engine_.tempo() / 60.0) * 2.2f : 1.2f;
    runPhase_ += dt * speed;
    titlePhase_ += dt;
}

} // namespace lightfoot
