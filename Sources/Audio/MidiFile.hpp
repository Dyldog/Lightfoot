#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lightfoot {

struct MidiNote {
    double beat = 0;        // onset in quarter-note beats from the start of the song
    double length = 0.25;   // in beats
    int channel = 0;        // 0..15; 9 is drums
    int note = 60;
    int velocity = 100;
    bool manual = false;    // the player sounds this note by hitting its pad; the engine stays quiet
};

struct TempoPoint {
    double beat = 0;
    double bpm = 120;
};

/// A Standard MIDI File reduced to what the game needs: notes with lengths,
/// the tempo map, the time signature, and which channel carries the tune.
class MidiSong {
public:
    /// Reads a format 0 or 1 file. On failure returns null and sets `error`.
    static std::shared_ptr<MidiSong> load(const std::string& path, std::string* error = nullptr);
    static std::shared_ptr<MidiSong> parse(const std::vector<uint8_t>& bytes, std::string* error = nullptr);
    /// Writes a small built-in tune (format 0) so there is always a song to try.
    static bool writeDemo(const std::string& path);

    std::string name;
    std::vector<MidiNote> notes;     // sorted by beat
    std::vector<TempoPoint> tempos;  // sorted by beat, at least one
    int beatsPerBar = 4;
    int melodyChannel = -1;          // the channel the course follows
    bool hasDrums = false;
    double lengthBeats = 0;          // end of the last note

    double tempoAt(double beat) const;
    /// Shifts every event so the first note falls at `firstBeat`, dropping leading silence.
    void trimLeadIn(double firstBeat);

private:
    void analyse();
};

} // namespace lightfoot
