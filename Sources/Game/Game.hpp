#pragma once

#include <array>
#include <cstdint>
#include <map>
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

/// Lightfoot: a rhythm game played on the pads. Gems come down a four-lane
/// highway at the music's tempo; the pads are the same highway, each row a
/// step ahead with the bottom row as the strike line, and the lit pad in a
/// row is the lane. When a gem reaches the line you hit a pad in its lane on
/// the beat. Chords are two lanes at once, crossed dark gems are decoys.
/// Misses drain the rock meter, hits fill it; empty is a fail. Levels rise
/// every 32 beats, four difficulties set how much of a song you play.
class Game {
public:
    enum class State { Title, HowTo, Scores, Songs, Browse, SongInfo, Playing, Paused, GameOver };
    enum class Hit { None, Perfect, Good, Early, Slip, Miss };
    enum Difficulty { Easy = 0, Medium, Hard, Expert };

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
    float rock() const { return rock_; }
    int level() const { return level_; }
    double beat() const { return beat_; }
    /// Position in course steps (a step is a beat, or half a beat in a song with off-beat notes).
    double pos() const { return pos_; }
    double stepBeats() const;
    /// How fast a song is being played relative to the file: 5% more a level, 10% more on Expert.
    double songSpeed() const;
    int64_t slot() const { return slot_; }
    int64_t cueSlot() const { return cueSlot_; }
    Hit lastHit() const { return lastHit_; }
    Track& track() { return track_; }
    Scores& scores() { return scores_; }
    const Palette& palette() const { return palette_; }
    float hue() const { return hue_; }
    void setHue(float hue);
    int difficulty() const { return difficulty_; }
    void setDifficulty(int difficulty);
    static const char* difficultyName(int difficulty);
    /// The level a run starts at for a difficulty: 1, 3, 5 or 7.
    static int startLevelFor(int difficulty) { return 1 + 2 * std::clamp(difficulty, 0, 3); }
    void requestQuit() { quit_ = true; }
    /// Songs: a Standard MIDI File becomes the music and the chart.
    bool loadSong(const std::string& path);
    void clearSong() { song_.reset(); }
    const MidiSong* song() const { return song_.get(); }
    bool songComplete() const { return songComplete_; }
    static std::string songsDir();
    /// Pad index (0 is pad 1, bottom-left) for a grid row (0 top) and column (0 left).
    static int padIndex(int row, int col) { return (3 - row) * 4 + col; }
    static int padRow(int pad) { return 3 - pad / 4; }
    static int padCol(int pad) { return pad % 4; }
    /// A slot's row (0 is the top row of the old grid, the highest note) as a highway lane, 0 left.
    static int laneOf(int row) { return 3 - std::clamp(row, 0, 3); }
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
    void updateSongInfo(Canvas& canvas, const Frame& in, int64_t nowMs);
    /// Audition the chosen lead on the song page: solo, from where it next has notes.
    void previewLead(bool keepPlace);
    /// Jump the song page's audition to `beat`.
    void previewSeek(double beat);
    void updatePlaying(Canvas& canvas, const Frame& in, int64_t nowMs);
    void refreshSongList();
    void ensureDemoSong();
    void openBrowser();
    void handleKnobs(const Frame& in);
    void handlePads(const Frame& in, int64_t nowMs);
    void judge(int lane, const Frame& in, int64_t nowMs);
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
    void drawPlay(Canvas& canvas, const Frame& in, int64_t nowMs);
    void drawTitle(Canvas& canvas, int64_t nowMs);
    void drawHowTo(Canvas& canvas, int64_t nowMs);
    void drawScores(Canvas& canvas, int64_t nowMs);
    void drawSongs(Canvas& canvas, int64_t nowMs);
    void drawSongInfo(Canvas& canvas, int64_t nowMs);
    void drawGameOver(Canvas& canvas, int64_t nowMs);
    void drawPaused(Canvas& canvas, int64_t nowMs);
    void drawSky(Canvas& canvas, int64_t nowMs, double scroll);
    void drawHighway(Canvas& canvas, const Frame& in, int64_t nowMs);
    void drawHud(Canvas& canvas, int64_t nowMs);
    void drawLogo(Canvas& canvas, int cx, int y, int scale, int64_t nowMs);
    void drawTexts(Canvas& canvas);
    void drawParticles(Canvas& canvas);
    void drawPanelLights(Canvas& canvas, int64_t nowMs);
    uint32_t padColor(int pad, int64_t nowMs) const;
    void spawnBurst(float x, float y, uint32_t color, int count, float speed);
    void addText(const std::string& text, float x, float y, uint32_t color, int scale = 1);
    void updateEffects(float dt);
    double yForDepth(double z) const;
    double halfWidthAtY(double y) const;
    double laneX(int lane, double y) const;
    int strikeX(int lane) const;
    int strikeY() const;
    int64_t columnBase() const;
    double beatPhase() const;
    double stepPhase() const;
    uint32_t nowSeed_();

    AudioEngine engine_;
    mutable Track track_; // generated lazily, so drawing may extend it
    Scores scores_;
    Palette palette_;
    State state_ = State::Title;
    int64_t stateSince_ = 0;
    int menuCursor_ = 0;
    int difficulty_ = Medium;
    int startLevel_ = 3;
    float hue_ = 0;
    float brightness_ = 0;
    bool quit_ = false;
    int64_t quitArmedMs_ = 0;
    float assist_ = 1.0f; // fader: 0.75 slow and half points .. 1.0 full speed

    // Songs
    std::shared_ptr<MidiSong> song_;
    std::vector<std::shared_ptr<MidiSong>> loadedSongs_; // kept alive: the audio thread may still read one
    bool songComplete_ = false;
    std::vector<std::string> songFiles_;
    int songCursor_ = 0;
    int leadCursor_ = 0;
    std::map<std::string, int> leadChoice_;
    std::unique_ptr<ui_filebrowser> browser_;
    std::string browserTitle_, browserExternal_, browserInternal_, browserStart_;

    // The run
    double beat_ = 0;            // heard beat: the engine's clock less output latency
    double pos_ = 0;             // the same, in course steps
    int lastCount_ = 0;
    int64_t slot_ = -1;          // the step the strike line is on
    int64_t cueSlot_ = -1;       // the gem awaiting a hit, -1 when none
    int64_t resolvedUpTo_ = -1;  // last gem judged, hit or missed
    int64_t lastHitSlot_ = -1;
    int lastHitLanes_[2] = {-1, -1}; // the lanes of the gem just resolved, so a chord's second pad is not judged twice
    bool twinHalf_ = false;      // first lane of a chord is down
    int twinHalfRow_ = -1;       // a lane index
    double twinHalfDelta_ = 0;
    int score_ = 0, combo_ = 0, maxCombo_ = 0, level_ = 1;
    int perfects_ = 0, goods_ = 0, misses_ = 0, slips_ = 0;
    float rock_ = 0.5f;
    Hit lastHit_ = Hit::None;
    int64_t lastHitMs_ = 0;
    int finalRank_ = -1;
    int64_t gameStartMs_ = 0;

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
    std::array<int64_t, 4> fretFlashMs_{};
    int64_t lastFrameMs_ = 0;
    float titlePhase_ = 0;
    uint32_t rng_ = 0x2545F491u;
};

} // namespace lightfoot
