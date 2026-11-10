#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace lightfoot {

class MidiSong;

/// One-shot sounds the game asks for. Triggered from the UI thread, rendered
/// on the audio thread.
enum class Sfx { Jump, TwinJump, Land, Perfect, Good, Miss, Slip, LevelUp, Tick, Start, GameOver, Select };

/// The game's music and effects, all synthesised: a four-on-the-floor kick,
/// hats, a clap, a pumping bass, a pad that follows a four-bar progression
/// and an arpeggio that joins at higher levels. The engine is also the clock:
/// the beat position advances with the samples rendered, so the scroll, the
/// pad lights and the music cannot drift apart. Without an output attached
/// (screenshots, tests) the beat advances by wall time instead.
class AudioEngine {
public:
    enum class Mode { Silent, Menu, Play };

    explicit AudioEngine(int sampleRate = 44100);

    int sampleRate() const { return sampleRate_; }
    /// Audio thread: `frames` stereo frames of interleaved float.
    void render(float* out, int frames);

    void setAttached(bool on) { attached_ = on; }
    bool attached() const { return attached_; }
    void setMasterGain(float gain) { masterGain_ = gain; }

    // Clock
    /// The current beat. Advances by wall time when no output is attached.
    double beat(int64_t nowMs);
    void setTempo(double bpm) { tempo_ = bpm; }
    double tempo() const { return tempo_; }
    /// Restart the beat count (a new game) at `startBeat`.
    void resetBeat(double startBeat = 0);
    void setPaused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }

    // Music
    void setMode(Mode mode) { mode_ = static_cast<int>(mode); }
    Mode mode() const { return static_cast<Mode>(mode_.load()); }
    /// 1 upwards: more layers, brighter filter.
    void setIntensity(int level) { intensity_ = level; }
    /// 0..1 extra brightness on top of the level's own.
    void setBrightness(float amount) { brightness_ = amount; }
    void trigger(Sfx sfx);
    /// Sound one of the song's notes now: the player hit its pad.
    void noteOn(int channel, int note, int velocity, double lengthBeats);
    /// While false the melody's automatic notes (the ones thinned out of the
    /// course) stay silent too: a miss leaves a hole until the next hit.
    void setMelodyGate(bool open) { melodyGate_ = open; }
    /// Play this song's notes from the beat clock instead of the procedural
    /// layers (the kick and hats stay when the song has no drums). Null
    /// returns to the procedural music. The song must outlive its use.
    void setSong(const MidiSong* song);
    const MidiSong* song() const { return song_.load(); }

    // Meters (last rendered block)
    float peakLeft() const { return peakLeft_; }
    float peakRight() const { return peakRight_; }
    /// 0..1, how far into the current kick's thump we are. For visuals that pump.
    float kickEnvelope() const { return kickEnv_; }

    // Compatibility with the device output thread's logging.
    bool playing() const { return mode_ != 0; }
    double positionBeats() const { return beat_; }

private:
    struct Voice {
        enum Kind { Off, Kick, Clap, Hat, Bass, Pad, Arp, Tone, Noise } kind = Off;
        double phase = 0, phase2 = 0, phase3 = 0;
        double freq = 0, freqEnd = 0; // tones sweep from freq to freqEnd over `sweep` seconds
        double sweep = 0;
        double t = 0;                 // seconds since trigger
        double attack = 0.001, decay = 0.2, hold = 0; // hold: seconds at full before decay (pad gate)
        float amp = 0;
        float lp = 0, lp2 = 0;        // one-pole state (bass/arp/pad)
        float noisePrev = 0;
        int shape = 0;                // tone: 0 sine, 1 square, 2 saw, 3 triangle
        bool music = true;            // goes through the music bus (filter, ducking)
        bool released = false;
    };

    void onStep(int step);                 // sixteenth boundary
    Voice* allocate(Voice::Kind kind);
    float renderVoice(Voice& v, double dt, float& right);
    void startSfx(Sfx sfx);
    float noise();

    static constexpr int kVoices = 72;
    static constexpr int kQueue = 32;

    int sampleRate_;
    std::atomic<bool> attached_{false};
    std::atomic<float> masterGain_{0.9f};
    std::atomic<double> tempo_{100.0};
    std::atomic<double> beat_{0.0};
    std::atomic<bool> paused_{false};
    std::atomic<int> mode_{0};
    std::atomic<int> intensity_{1};
    std::atomic<float> brightness_{0.0f};
    std::atomic<float> peakLeft_{0}, peakRight_{0};
    std::atomic<float> kickEnv_{0};
    int64_t lastClockMs_ = 0;

    std::array<Voice, kVoices> voices_{};
    struct Trigger {
        int sfx = -1;      // a Sfx, or -1 for a note
        int channel = 0, note = 60, velocity = 100;
        float lengthBeats = 0.5f;
    };
    std::array<Trigger, kQueue> queue_{};
    std::atomic<bool> melodyGate_{true};
    std::atomic<int> queueHead_{0}, queueTail_{0};
    int lastStep_ = -1;
    int bar_ = 0;
    uint32_t rng_ = 0x9e3779b9u;
    float busLp_ = 0, busLpR_ = 0;
    float duck_ = 0;
    float menuFade_ = 0;
    std::atomic<const MidiSong*> song_{nullptr};
    std::atomic<bool> songRewind_{true};
    size_t songIndex_ = 0;
    void playNote(int channel, int note, int velocity, double lengthBeats, double beatSeconds);
};

} // namespace lightfoot
