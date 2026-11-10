#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lightfoot {

/// One beat of the course. The platform under the runner for that beat, and
/// whether getting onto it needs a jump (a lit pad to hit on the beat).
struct Slot {
    int row = 3;        // platform height: 0 is the top row of pads, 3 the bottom
    bool cue = false;   // a jump is due on this slot's downbeat
    int row2 = -1;      // twin cue: a second lit pad; both are hit together for a long jump
    int decoy = -1;     // a dim red pad in this column that must not be hit
    bool gap = false;   // no platform: a twin jump flies over it
    int pad = -1;       // song mode: the pad that is this note (pads are the instrument), pad2 for a chord
    int pad2 = -1;
    int note = -1;      // song mode: the melody note this jump sounds (and note2 for a twin)
    int note2 = -1;
    int velocity = 100;
    double noteBeats = 0.5;
};

/// Deterministic course generator. The same seed gives the same course, so a
/// screenshot, a test and a replay agree. Levels change every 32 beats:
/// faster, more jumps, then twins and decoys.
class Track {
public:
    static constexpr int kSlotsPerLevel = 32;
    static constexpr double kStartTempo = 100;
    static constexpr double kTempoPerLevel = 5;
    static constexpr double kMaxTempo = 165;

    explicit Track(uint32_t seed = 1, int startLevel = 1);
    /// A fixed course: the melody of a song, one cue per beat at most, the row
    /// following the pitch. Beyond the end the course is plain running.
    /// Marks the notes the player will sound as manual, so the engine leaves them to the pads.
    static Track fromSong(class MidiSong& song, int startLevel = 1);

    bool finite() const { return finite_; }
    int64_t length() const { return length_; }
    int beatsPerBar() const { return beatsPerBar_; }
    const std::string& songName() const { return songName_; }
    /// Song mode: the melody pitch a pad plays, -1 for an unused pad.
    int padPitch(int pad) const { return pad >= 0 && pad < 16 ? padPitch_[static_cast<size_t>(pad)] : -1; }
    /// Pad index for a grid row (0 top) and column (0 left): pad 1 is bottom-left.
    static int padIndex(int row, int col) { return (3 - row) * 4 + col; }
    static int padRow(int pad) { return 3 - pad / 4; }

    /// By value: the course grows as it is read, so a reference would not survive.
    Slot at(int64_t index);
    /// Levels change every 32 beats whatever the step size.
    int level(int64_t index) const { return startLevel_ + static_cast<int>(index * stepBeats_ / kSlotsPerLevel); }
    /// Beats per slot: 1 on the endless course, 0.5 for a song with notes off the beat.
    double stepBeats() const { return stepBeats_; }
    double tempoFor(int level) const;
    int startLevel() const { return startLevel_; }
    uint32_t seed() const { return seed_; }

private:
    void generate(int64_t upTo);
    uint32_t next();
    double unit() { return next() / 4294967296.0; }

    uint32_t seed_;
    uint32_t state_;
    int startLevel_;
    std::vector<Slot> slots_;
    int lastCueAt_ = -100;
    bool finite_ = false;
    double stepBeats_ = 1.0;
    int64_t length_ = 0;
    int beatsPerBar_ = 4;
    std::string songName_;
    std::array<int, 16> padPitch_{};
};

} // namespace lightfoot
