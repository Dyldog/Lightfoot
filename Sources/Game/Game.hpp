#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Audio/AudioEngine.hpp"
#include "Audio/MidiFile.hpp"
#include "Game/Palette.hpp"
#include "Game/Scores.hpp"
#include "Game/Track.hpp"

struct ui;
struct input;
struct ui_filebrowser;

namespace lightfoot {

class Canvas;
class Frame;

/// Lightfoot: an auto-running platformer played on the pads. The course scrolls
/// at the music's tempo. The four pad columns are the next four beats, the lit
/// pad in each column is the platform you have to reach, and when a lit pad
/// reaches the left column you hit it on the beat to jump. Only the lit pad
/// counts. Twins (two lit pads) are long jumps over a gap, dim red pads are
/// decoys. Three lives, levels every 32 beats, scores on the card.
class Game {
public:
    enum class State { Title, HowTo, Scores, Songs, Browse, Playing, Paused, GameOver };
    enum class Hit { None, Perfect, Good, Early, Slip, Miss };

    Game();
    ~Game();

    /// Toolkit frame callback: draws one frame and handles the panel.
    int frame(ui* u, const input* in);
    AudioEngine& engine() { return engine_; }
    static const char* version();

    // Observable for tests and screenshots.
    State state() const { return state_; }
    void startGame(int64_t nowMs, uint32_t seed = 0);
    void setState(State s, int64_t nowMs);
    int score() const { return score_; }
    int combo() const { return combo_; }
    int lives() const { return lives_; }
    int level() const { return level_; }
    double beat() const { return beat_; }
    /// Position in course steps (a step is a beat, or half a beat in a song with off-beat notes).
    double pos() const { return pos_; }
    double stepBeats() const;
    /// How fast a song is being played relative to the file: 5% more a level.
    double songSpeed() const;
    int64_t slot() const { return slot_; }
    int64_t cueSlot() const { return cueSlot_; }
    Hit lastHit() const { return lastHit_; }
    Track& track() { return track_; }
    Scores& scores() { return scores_; }
    const Palette& palette() const { return palette_; }
    float hue() const { return hue_; }
    void setHue(float hue);
    int startLevel() const { return startLevel_; }
    void setStartLevel(int level);
    void requestQuit() { quit_ = true; }
    /// Songs: a Standard MIDI File becomes the music and the course.
    bool loadSong(const std::string& path);
    void clearSong() { song_.reset(); }
    const MidiSong* song() const { return song_.get(); }
    bool songComplete() const { return songComplete_; }
    static std::string songsDir();
    /// Pad index (0 is pad 1, bottom-left) for a grid row (0 top) and column (0 left).
    static int padIndex(int row, int col) { return (3 - row) * 4 + col; }
    static int padRow(int pad) { return 3 - pad / 4; }
    static int padCol(int pad) { return pad % 4; }
    /// Beats of the judgement windows at the current tempo.
    double windowGood() const;
    double windowPerfect() const;

private:
    struct Particle {
        float x, y, vx, vy, life, max;
        uint32_t color;
        int size;
    };
    struct FloatText {
        std::string text;
        float x, y, life;
        uint32_t color;
        int scale;
    };

    // Flow
    void update(Canvas& canvas, const Frame& in, int64_t nowMs);
    void updateTitle(Canvas& canvas, const Frame& in, int64_t nowMs);
    void updateSongs(Canvas& canvas, const Frame& in, int64_t nowMs);
    void updateBrowser(Canvas& canvas, const Frame& in, int64_t nowMs);
    void refreshSongList();
    void ensureDemoSong();
    void openBrowser();
    void updatePlaying(Canvas& canvas, const Frame& in, int64_t nowMs);
    void handleKnobs(const Frame& in);
    void handlePads(const Frame& in, int64_t nowMs);
    void judge(int row, int col, const Frame& in, int64_t nowMs);
    void resolve(Hit hit, double deltaBeats, int64_t nowMs);
    void miss(int64_t nowMs);
    void slip(const char* why, int64_t nowMs);
    void advance(int64_t nowMs);
    void findNextCue();
    void applyLevel(int level, bool announce, int64_t nowMs);
    void endGame(int64_t nowMs);
    void loadSettings();
    void saveSettings() const;

    // Drawing (GameDraw.cpp)
    void draw(Canvas& canvas, const Frame& in, int64_t nowMs);
    void drawPlay(Canvas& canvas, int64_t nowMs);
    void drawTitle(Canvas& canvas, int64_t nowMs);
    void drawHowTo(Canvas& canvas, int64_t nowMs);
    void drawScores(Canvas& canvas, int64_t nowMs);
    void drawSongs(Canvas& canvas, int64_t nowMs);
    void drawGameOver(Canvas& canvas, int64_t nowMs);
    void drawPaused(Canvas& canvas, int64_t nowMs);
    void drawSky(Canvas& canvas, int64_t nowMs, double scroll);
    void drawWorld(Canvas& canvas, int64_t nowMs);
    void drawHud(Canvas& canvas, int64_t nowMs);
    void drawPadMirror(Canvas& canvas, int x, int y, int cell, int64_t nowMs);
    void drawRunner(Canvas& canvas, int x, int y, int64_t nowMs);
    void drawLogo(Canvas& canvas, int cx, int y, int scale, int64_t nowMs);
    void drawTexts(Canvas& canvas);
    void drawParticles(Canvas& canvas);
    void drawPanelLights(Canvas& canvas, int64_t nowMs);
    uint32_t padColor(int pad, int64_t nowMs) const;
    void spawnBurst(float x, float y, uint32_t color, int count, float speed);
    void addText(const std::string& text, float x, float y, uint32_t color, int scale = 1);
    void updateEffects(float dt);
    double runnerY(double b) const;
    int rowY(int row) const;
    int xForSlot(double slotIndex) const;
    int64_t columnBase() const;
    double beatPhase() const;
    double stepPhase() const;
    int slotW() const;

    AudioEngine engine_;
    mutable Track track_; // generated lazily, so drawing may extend it
    Scores scores_;
    Palette palette_;
    State state_ = State::Title;
    int64_t stateSince_ = 0;
    int menuCursor_ = 0;
    int startLevel_ = 1;
    float hue_ = 0;
    float brightness_ = 0;
    bool quit_ = false;
    int64_t quitArmedMs_ = 0;
    float assist_ = 1.0f;       // fader: 0.75 slow and half points .. 1.0 full speed
    bool knobsSeen_[3] = {false, false, false};

    // Songs
    std::shared_ptr<MidiSong> song_;                    // the song a run plays to, or null for endless
    std::vector<std::shared_ptr<MidiSong>> loadedSongs_; // kept alive: the audio thread may still read one
    bool songComplete_ = false;
    std::vector<std::string> songFiles_;
    int songCursor_ = 0;
    std::unique_ptr<ui_filebrowser> browser_;
    std::string browserTitle_, browserExternal_, browserInternal_, browserStart_;

    // The run
    double beat_ = 0;            // heard beat: the engine's clock less output latency
    double pos_ = 0;             // the same, in course steps
    int lastCount_ = 0;          // count-in number last shown
    int64_t slot_ = -1;          // beat the runner is on
    int64_t cueSlot_ = -1;       // the jump awaiting a hit, -1 when none
    int64_t resolvedUpTo_ = -1;  // last cue judged, hit or missed
    bool twinHalf_ = false;      // first pad of a twin is down
    int twinHalfRow_ = -1;
    double twinHalfDelta_ = 0;
    int score_ = 0, combo_ = 0, maxCombo_ = 0, lives_ = 3, level_ = 1;
    int perfects_ = 0, goods_ = 0, misses_ = 0, slips_ = 0;
    int runnerRow_ = 3;
    int jumpFrom_ = 3, jumpTo_ = 3;
    double jumpStart_ = -1, jumpEnd_ = -1;
    bool landed_ = true;
    double fallStart_ = -1;
    double graceUntil_ = -1;
    Hit lastHit_ = Hit::None;
    int64_t lastHitMs_ = 0;
    int finalRank_ = -1;
    int64_t gameStartMs_ = 0;
    double lastBeatForDt_ = 0;

    // Effects
    std::vector<Particle> particles_;
    std::vector<FloatText> texts_;
    float shake_ = 0;
    float flash_ = 0;
    uint32_t flashColor_ = 0xffffff;
    std::string banner_;
    int64_t bannerUntil_ = 0;
    std::array<int64_t, 16> padFlashMs_{};
    std::array<uint32_t, 16> padFlashColor_{};
    int64_t lastFrameMs_ = 0;
    float runPhase_ = 0;
    float titlePhase_ = 0;
    uint32_t rng_ = 0x2545F491u;
    uint32_t nowSeed_();
};

} // namespace lightfoot
