#include "Game/Game.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unistd.h>

#include "Shared/Drawing/Canvas.hpp"
#include "Shared/Input/Frame.hpp"
#include "Shared/Json/Json.hpp"
#include "Shared/Paths/Paths.hpp"

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
constexpr double kCountIn = 4;            // beats of music before step 0
constexpr double kMenuTempo = 96;

} // namespace

const char* Game::version() {
    return LIGHTFOOT_VERSION;
}

const char* Game::difficultyName(int difficulty) {
    static const char* const names[4] = {"EASY", "MEDIUM", "HARD", "EXPERT"};
    return names[std::clamp(difficulty, 0, 3)];
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

// MARK: - Settings and songs

void Game::loadSettings() {
    std::string text;
    if (!paths::readFile(paths::join(paths::dataDir(), "settings.json"), text)) return;
    const Json j = Json::parse(text);
    hue_ = static_cast<float>(j["hue"].asNumber(0));
    setDifficulty(j["difficulty"].asInt(Medium));
    const Json& leads = j["leads"];
    for (const std::string& key : leads.keys()) leadChoice_[key] = leads[key].asInt(-1);
}

void Game::saveSettings() const {
    Json j = Json::object();
    j.set("hue", static_cast<double>(hue_));
    j.set("difficulty", difficulty_);
    Json leads = Json::object();
    for (const auto& kv : leadChoice_) leads.set(kv.first, kv.second);
    j.set("leads", leads);
    paths::makeDirs(paths::dataDir());
    paths::writeFileAtomic(paths::join(paths::dataDir(), "settings.json"), j.dump(2) + "\n");
}

void Game::setDifficulty(int difficulty) {
    difficulty_ = std::clamp(difficulty, 0, 3);
    startLevel_ = startLevelFor(difficulty_);
}

void Game::ensureDemoSong() {
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
    const auto remembered = leadChoice_.find(song->name);
    if (remembered != leadChoice_.end() && remembered->second >= 0 && remembered->second < 16 && song->channels[static_cast<size_t>(remembered->second)].notes > 0)
        song->setMelodyChannel(remembered->second);
    leadCursor_ = 0;
    for (size_t i = 0; i < song->candidates.size(); ++i)
        if (song->candidates[i] == song->melodyChannel) leadCursor_ = static_cast<int>(i);
    loadedSongs_.push_back(song);
    if (loadedSongs_.size() > 8) loadedSongs_.erase(loadedSongs_.begin());
    song_ = song;
    std::fprintf(stderr, "song: %s, %zu notes, %.0f BPM, %.0f beats, lead channel %d, drums %d\n", song->name.c_str(), song->notes.size(),
                 song->tempoAt(0), song->lengthBeats, song->melodyChannel + 1, song->hasDrums ? 1 : 0);
    return true;
}

void Game::openBrowser() {
    browser_ = std::make_unique<struct ui_filebrowser>();
    browserTitle_ = "LOAD A SONG";
    static const char* extensions[] = {".mid", ".midi", nullptr};
    struct ui_filebrowser_config cfg{};
    cfg.title = browserTitle_.c_str();
    cfg.action_label = "OPEN";
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

// MARK: - Timing helpers

double Game::stepBeats() const {
    return track_.stepBeats();
}

double Game::songSpeed() const {
    return (1.0 + 0.05 * (level_ - 1)) * (difficulty_ == Expert ? 1.1 : 1.0);
}

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
    // The strike line's step: the gem due next, or the step just entered while its window is open.
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
        engine_.setPreview(-1);
        engine_.setMode(AudioEngine::Mode::Menu);
        engine_.setTempo(kMenuTempo);
        engine_.setIntensity(1);
        break;
    case State::SongInfo:
        // The page auditions the lead: the song plays with that channel alone.
        engine_.setPaused(false);
        engine_.setSong(song_.get());
        engine_.setMode(AudioEngine::Mode::Play);
        engine_.setIntensity(6); // the filter open, nothing procedural to drive
        previewLead(false);
        break;
    case State::Playing:
        engine_.setPaused(false);
        engine_.setPreview(-1);
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
        track_ = Track::fromSong(*song_, startLevel_, difficulty_);
        engine_.setSong(song_.get());
    } else {
        track_ = Track(seed, startLevel_);
        engine_.setSong(nullptr);
    }
    score_ = combo_ = maxCombo_ = 0;
    perfects_ = goods_ = misses_ = slips_ = 0;
    rock_ = 0.5f;
    level_ = startLevel_;
    slot_ = -static_cast<int64_t>(kCountIn / track_.stepBeats()) - 1;
    lastCount_ = 0;
    cueSlot_ = -1;
    resolvedUpTo_ = -1;
    lastHitSlot_ = -1;
    lastHitLanes_[0] = lastHitLanes_[1] = -1;
    twinHalf_ = false;
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
    finalRank_ = score_ > 0 ? scores_.add(score_, level_, maxCombo_, song_ ? song_->name : std::string(), difficulty_) : -1;
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
        const int d = in.knob(2) * 4 / 128;
        if (d != difficulty_) {
            setDifficulty(d);
            engine_.trigger(Sfx::Tick);
        }
    }
    if (in.faderMoved()) assist_ = 0.75f + 0.25f * (in.fader() / 127.0f);
    engine_.setBrightness(std::max(brightness_, std::min(1.0f, combo_ / 40.0f)));
}

void Game::update(Canvas& canvas, const Frame& in, int64_t nowMs) {
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
    case State::SongInfo: updateSongInfo(canvas, in, nowMs); break;
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
        if (nowMs - stateSince_ < 600) break;
        if (in.pressed(IN_PLAY) || in.pressed(IN_B1)) startGame(nowMs);
        else if (in.pressed(IN_STOP) || in.pressed(IN_B3) || in.encoderPressed()) {
            engine_.trigger(Sfx::Select);
            setState(State::Title, nowMs);
        }
        break;
    }
    if (state_ != State::Playing) engine_.beat(nowMs);
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
    for (int p = 0; p < 16; ++p)
        if (in.padHit(p) >= 0) {
            engine_.trigger(Sfx::Select);
            padFlashMs_[static_cast<size_t>(p)] = nowMs;
            padFlashColor_[static_cast<size_t>(p)] = palette_.white;
            spawnBurst(static_cast<float>(strikeX(padCol(p))), static_cast<float>(strikeY()), palette_.row[padCol(p)], 10, 60);
        }
}

void Game::updateSongs(Canvas& canvas, const Frame& in, int64_t nowMs) {
    (void)canvas;
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
    if (loadSong(path)) setState(State::SongInfo, nowMs);
}

void Game::updateSongInfo(Canvas& canvas, const Frame& in, int64_t nowMs) {
    (void)canvas;
    if (!song_) {
        setState(State::Songs, nowMs);
        return;
    }
    const int count = static_cast<int>(song_->candidates.size());
    if (const int d = in.encoder(); d && count > 0) {
        leadCursor_ = std::clamp(leadCursor_ + d, 0, count - 1);
        song_->setMelodyChannel(song_->candidates[static_cast<size_t>(leadCursor_)]);
        leadChoice_[song_->name] = song_->melodyChannel;
        previewLead(true);
    }
    // The pads scrub: pad 1 is the start of the song, pad 16 the last sixteenth.
    for (int pad = 0; pad < 16; ++pad) {
        if (in.padHit(pad) < 0) continue;
        const double bar = std::max(1, song_->beatsPerBar);
        const double segment = song_->lengthBeats / 16.0;
        previewSeek(std::min(std::round(pad * segment / bar) * bar, song_->lengthBeats));
        padFlashMs_[static_cast<size_t>(pad)] = nowMs;
        padFlashColor_[static_cast<size_t>(pad)] = palette_.accentHot;
    }
    // Follow the file's tempo, and go round again at the end.
    beat_ = engine_.positionBeats();
    engine_.setTempo(song_->tempoAt(std::max(0.0, beat_)));
    if (beat_ > song_->lengthBeats + 2) previewLead(false);
    if (in.pressed(IN_B1) || in.pressed(IN_STOP)) {
        engine_.trigger(Sfx::Select);
        setState(State::Songs, nowMs);
        return;
    }
    if (in.pressed(IN_PLAY) || in.pressed(IN_B3) || in.encoderPressed()) {
        saveSettings();
        startGame(nowMs);
    }
}

void Game::previewLead(bool keepPlace) {
    if (!song_) return;
    const int channel = song_->melodyChannel;
    engine_.setPreview(channel);
    // Start where the line next has something to play: from here if it is
    // about to, otherwise its next note, otherwise its first.
    const double here = keepPlace ? engine_.positionBeats() : -1;
    double first = -1, next = -1;
    for (const MidiNote& n : song_->notes) {
        if (n.channel != channel) continue;
        if (first < 0) first = n.beat;
        if (n.beat >= here - 0.5) {
            next = n.beat;
            break;
        }
    }
    if (next < 0) next = first;
    if (next < 0) next = 0;
    if (keepPlace && next <= here + 2) return;
    previewSeek(std::max(0.0, next - 0.25));
}

void Game::previewSeek(double beat) {
    engine_.seek(beat);
    beat_ = beat;
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
            setState(State::SongInfo, nowMs);
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
    if (state_ != State::Playing) return;
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
    if (track_.finite() && slot_ >= track_.length() + 2 && cueSlot_ < 0) {
        songComplete_ = true;
        endGame(nowMs);
        return;
    }
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
    // Any pad in a column is that lane's fret.
    for (int p = 0; p < 16; ++p)
        if (in.padHit(p) >= 0) judge(padCol(p), in, nowMs);
}

void Game::judge(int lane, const Frame& in, int64_t nowMs) {
    // Both pads of a chord arrive in one frame: the second is part of the hit, not a new one.
    if (nowMs - lastHitMs_ < 60 && (lane == lastHitLanes_[0] || lane == lastHitLanes_[1])) return;
    if (cueSlot_ < 0) return;
    const Slot s = track_.at(cueSlot_);
    const int laneA = laneOf(s.row), laneB = s.row2 >= 0 ? laneOf(s.row2) : -1;
    if (s.decoy >= 0 && lane == laneOf(s.decoy)) {
        slip("DECOY", nowMs);
        return;
    }
    if (lane != laneA && lane != laneB) {
        // A lane with a gem further up the highway: early. Anything else is idle.
        for (int64_t k = cueSlot_ + 1; k < cueSlot_ + 4; ++k) {
            const Slot u = track_.at(k);
            if (u.cue && (lane == laneOf(u.row) || (u.row2 >= 0 && lane == laneOf(u.row2)))) {
                slip("EARLY", nowMs);
                return;
            }
        }
        return;
    }
    const double delta = pos_ - static_cast<double>(cueSlot_);
    if (delta < -windowGood()) {
        slip("EARLY", nowMs);
        return;
    }
    fretFlashMs_[static_cast<size_t>(lane)] = nowMs;
    padFlashMs_[static_cast<size_t>(padIndex(3, lane))] = nowMs;
    padFlashColor_[static_cast<size_t>(padIndex(3, lane))] = palette_.white;
    if (laneB >= 0) {
        const int other = lane == laneA ? laneB : laneA;
        bool otherDown = twinHalf_ && twinHalfRow_ == other;
        for (int row = 0; row < 4; ++row) otherDown |= in.padDown(padIndex(row, other));
        if (!otherDown) {
            twinHalf_ = true;
            twinHalfRow_ = lane;
            twinHalfDelta_ = delta;
            return;
        }
        const double worst = std::max(std::fabs(delta), twinHalf_ ? std::fabs(twinHalfDelta_) : 0.0);
        twinHalf_ = false;
        fretFlashMs_[static_cast<size_t>(other)] = nowMs;
        resolve(worst <= windowPerfect() ? Hit::Perfect : Hit::Good, worst, nowMs);
        return;
    }
    resolve(std::fabs(delta) <= windowPerfect() ? Hit::Perfect : Hit::Good, delta, nowMs);
}

void Game::resolve(Hit hit, double deltaBeats, int64_t nowMs) {
    (void)deltaBeats;
    const Slot s = track_.at(cueSlot_);
    const bool chord = s.row2 >= 0;
    const int multiplier = 1 + std::min(combo_, 70) / 10;
    int points = (hit == Hit::Perfect ? 100 : 60) * multiplier * (chord ? 2 : 1);
    if (assist_ < 0.999f) points /= 2;
    score_ += points;
    ++combo_;
    maxCombo_ = std::max(maxCombo_, combo_);
    if (hit == Hit::Perfect) ++perfects_;
    else ++goods_;
    rock_ = std::min(1.0f, rock_ + (hit == Hit::Perfect ? 0.03f : 0.02f));
    lastHit_ = hit;
    lastHitMs_ = nowMs;
    lastHitSlot_ = cueSlot_;
    lastHitLanes_[0] = laneOf(s.row);
    lastHitLanes_[1] = s.row2 >= 0 ? laneOf(s.row2) : -1;

    if (song_ && s.note >= 0) {
        // You are playing the tune: the hit is the note.
        const int velocity = hit == Hit::Perfect ? std::min(127, s.velocity + 10) : std::max(40, s.velocity - 15);
        engine_.noteOn(song_->melodyChannel, s.note, velocity, s.noteBeats);
        if (s.note2 >= 0) engine_.noteOn(song_->melodyChannel, s.note2, velocity, s.noteBeats);
        engine_.setMelodyGate(true);
    } else {
        engine_.trigger(hit == Hit::Perfect ? Sfx::Perfect : Sfx::Good);
    }
    const int lane = laneOf(s.row);
    const uint32_t color = hit == Hit::Perfect ? palette_.white : palette_.row[lane];
    spawnBurst(static_cast<float>(strikeX(lane)), static_cast<float>(strikeY()), palette_.row[lane], hit == Hit::Perfect ? 22 : 12, 90);
    if (chord) spawnBurst(static_cast<float>(strikeX(laneOf(s.row2))), static_cast<float>(strikeY()), palette_.row[laneOf(s.row2)], 12, 90);
    char label[32];
    std::snprintf(label, sizeof label, "%s", hit == Hit::Perfect ? "PERFECT" : "GOOD");
    addText(label, 160, static_cast<float>(strikeY() - 34), color, hit == Hit::Perfect ? 1 : 0);
    std::snprintf(label, sizeof label, "+%d", points);
    addText(label, static_cast<float>(strikeX(lane)), static_cast<float>(strikeY() - 18), palette_.row[lane], 0);
    if (combo_ > 0 && combo_ % 10 == 0) {
        std::snprintf(label, sizeof label, "%d NOTE STREAK", combo_);
        addText(label, 160, 120, palette_.accentHot, 1);
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
    rock_ = std::max(0.0f, rock_ - 0.02f);
    shake_ = std::max(shake_, 0.3f);
    engine_.trigger(Sfx::Slip);
    addText(why, 160, static_cast<float>(strikeY() - 34), palette_.danger, 0);
}

void Game::miss(int64_t nowMs) {
    const Slot s = track_.at(cueSlot_);
    ++misses_;
    combo_ = 0;
    twinHalf_ = false;
    lastHit_ = Hit::Miss;
    lastHitMs_ = nowMs;
    engine_.setMelodyGate(false); // the tune drops out until the next hit
    engine_.trigger(Sfx::Miss);
    rock_ = std::max(0.0f, rock_ - (difficulty_ >= Hard ? 0.10f : 0.08f));
    shake_ = std::max(shake_, 0.6f);
    flash_ = 0.35f;
    flashColor_ = palette_.danger;
    addText("MISS", static_cast<float>(strikeX(laneOf(s.row))), static_cast<float>(strikeY() - 34), palette_.danger, 1);
    for (int row = 0; row < 4; ++row) {
        padFlashMs_[static_cast<size_t>(padIndex(row, laneOf(s.row)))] = nowMs;
        padFlashColor_[static_cast<size_t>(padIndex(row, laneOf(s.row)))] = palette_.danger;
    }
    resolvedUpTo_ = cueSlot_;
    cueSlot_ = -1;
    if (rock_ <= 0.0f) endGame(nowMs);
}

// MARK: - Effects state

void Game::spawnBurst(float x, float y, uint32_t color, int count, float speed) {
    for (int i = 0; i < count; ++i) {
        const float a = static_cast<float>(i) / count * 6.2831853f + static_cast<float>(nowSeed_() % 100) * 0.01f;
        const float v = speed * (0.4f + 0.6f * static_cast<float>(nowSeed_() % 100) / 100.0f);
        Particle p{x, y, std::cos(a) * v, std::sin(a) * v - speed * 0.7f, 0.0f, 0.45f + static_cast<float>(nowSeed_() % 50) / 100.0f, color, 1 + static_cast<int>(nowSeed_() % 2)};
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
        p.vy += 220.0f * dt;
        p.vx *= 1.0f - 1.5f * dt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.life >= p.max; }), particles_.end());
    for (FloatText& t : texts_) {
        t.life += dt;
        t.y -= 26.0f * dt;
    }
    texts_.erase(std::remove_if(texts_.begin(), texts_.end(), [](const FloatText& t) { return t.life > 0.9f; }), texts_.end());
    shake_ = std::max(0.0f, shake_ - dt * 2.2f);
    flash_ = std::max(0.0f, flash_ - dt * 1.6f);
    titlePhase_ += dt;
}

} // namespace lightfoot
