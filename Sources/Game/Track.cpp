#include "Game/Track.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "Audio/MidiFile.hpp"

namespace lightfoot {

Track::Track(uint32_t seed, int startLevel) : seed_(seed), state_(seed * 2654435761u + 12345u), startLevel_(std::max(1, startLevel)) {
    if (state_ == 0) state_ = 1;
    slots_.reserve(1024);
}

uint32_t Track::next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
}

double Track::tempoFor(int level) const {
    return std::min(kMaxTempo, kStartTempo + kTempoPerLevel * (level - 1));
}

Slot Track::at(int64_t index) {
    if (index < 0) index = 0;
    if (finite_) {
        if (index < static_cast<int64_t>(slots_.size())) return slots_[static_cast<size_t>(index)];
        Slot s;
        s.row = slots_.empty() ? 3 : slots_.back().row;
        return s;
    }
    if (index >= static_cast<int64_t>(slots_.size())) generate(index + 64);
    return slots_[static_cast<size_t>(index)];
}

void Track::generate(int64_t upTo) {
    while (static_cast<int64_t>(slots_.size()) <= upTo) {
        const int64_t i = static_cast<int64_t>(slots_.size());
        const int lvl = level(i);
        Slot s;
        const Slot* prev = slots_.empty() ? nullptr : &slots_.back();
        s.row = prev ? prev->row : 3;

        // A run-in: the first bar of the course and of every level is plain running.
        const bool breather = i < 6 || (i % kSlotsPerLevel) < 2;
        // Spacing between jumps shrinks with the level; density grows.
        const int minSpacing = std::max(1, 3 - lvl / 2);
        const double density = std::min(0.72, 0.22 + 0.07 * lvl);
        const bool previousWasGap = prev && prev->gap;
        if (!breather && !previousWasGap && i - lastCueAt_ >= minSpacing + 1 && unit() < density) {
            // A jump. Twins from level 3, decoys from level 5.
            int row;
            do row = static_cast<int>(unit() * 4) % 4;
            while (row == s.row);
            const bool twin = lvl >= 3 && unit() < std::min(0.3, 0.1 + 0.03 * lvl);
            s.cue = true;
            if (twin) {
                // The cue sits on a gap; the landing is the slot after it.
                s.gap = true;
                s.row = row;
                int second;
                do second = static_cast<int>(unit() * 4) % 4;
                while (second == row);
                s.row2 = second;
            } else {
                s.row = row;
            }
            if (lvl >= 5 && unit() < std::min(0.45, 0.15 + 0.04 * lvl)) {
                int d;
                do d = static_cast<int>(unit() * 4) % 4;
                while (d == s.row || d == s.row2);
                s.decoy = d;
            }
            lastCueAt_ = static_cast<int>(i);
            slots_.push_back(s);
            if (twin) {
                Slot landing;
                landing.row = row; // the first lit pad's row is where you land
                slots_.push_back(landing);
            }
            continue;
        }
        slots_.push_back(s);
    }
}

Track Track::fromSong(MidiSong& song, int startLevel) {
    Track t(static_cast<uint32_t>(song.notes.size() * 7919 + 17), startLevel);
    t.finite_ = true;
    t.beatsPerBar_ = song.beatsPerBar;
    t.songName_ = song.name;
    // The melody's pitches, ranked, give four rows: the top row is the top quarter.
    std::map<int, int> pitchRank;
    int offBeat = 0, melodyNotes = 0;
    for (const MidiNote& n : song.notes) {
        if (n.channel != song.melodyChannel) continue;
        pitchRank[n.note] = 0;
        ++melodyNotes;
        const double frac = n.beat - std::floor(n.beat);
        if (std::fabs(frac - 0.5) < 0.15) ++offBeat;
    }
    // Notes on the off-beat mean the course runs in half-beat steps, so the
    // rhythm is played as written rather than rounded to the beat.
    t.stepBeats_ = melodyNotes > 0 && offBeat * 10 >= melodyNotes ? 0.5 : 1.0;
    const double step = t.stepBeats_;
    int rank = 0;
    for (auto& kv : pitchRank) kv.second = rank++;
    const int distinct = std::max(1, rank);
    auto rowFor = [&](int note) {
        auto it = pitchRank.find(note);
        if (it == pitchRank.end()) return 3;
        return std::clamp(3 - it->second * 4 / distinct, 0, 3);
    };
    // One cue per step: the first (highest) note on it, quantised.
    const int64_t steps = static_cast<int64_t>(std::ceil(song.lengthBeats / step)) + 2;
    std::vector<MidiNote*> onStep(static_cast<size_t>(steps), nullptr);
    std::vector<MidiNote*> second(static_cast<size_t>(steps), nullptr);
    for (MidiNote& n : song.notes) {
        n.manual = false;
        if (n.channel != song.melodyChannel) continue;
        const int64_t b = static_cast<int64_t>(std::llround(n.beat / step));
        if (b < 0 || b >= steps) continue;
        if (std::fabs(n.beat - b * step) > 0.3 * step) continue; // between steps: not a jump
        MidiNote*& slotNote = onStep[static_cast<size_t>(b)];
        if (!slotNote) slotNote = &n;
        else if (std::fabs(n.beat - slotNote->beat) < 0.05 && rowFor(n.note) != rowFor(slotNote->note) && !second[static_cast<size_t>(b)]) second[static_cast<size_t>(b)] = &n;
    }
    t.slots_.reserve(static_cast<size_t>(steps) + 8);
    int row = 3;
    for (int64_t b = 0; b < steps; ++b) {
        Slot s;
        s.row = row;
        MidiNote* n = onStep[static_cast<size_t>(b)];
        const bool prevGap = b > 0 && t.slots_.back().gap;
        const int lvl = t.level(b);
        // The chart is a simplified melody, filling in as the levels rise: the
        // strong beats first, then every beat, then every note. Held notes are
        // landmarks and always stay.
        bool keep = n != nullptr;
        if (n) {
            const double beatPos = b * step;
            const bool onBeat = std::fabs(beatPos - std::round(beatPos)) < 1e-9;
            const bool strong = onBeat && (static_cast<int64_t>(std::llround(beatPos)) % 2 == 0);
            const bool held = n->length >= 1.0;
            if (lvl <= 2) keep = strong || held;
            else if (lvl <= 4) keep = onBeat || held;
        }
        if (n && keep && !prevGap && b * step >= 4) {
            const int target = rowFor(n->note);
            MidiNote* chord = second[static_cast<size_t>(b)];
            // The player sounds these notes; the engine leaves them alone.
            n->manual = true;
            s.note = n->note;
            s.velocity = n->velocity;
            s.noteBeats = n->length;
            const bool nextFree = b + 1 < steps && !onStep[static_cast<size_t>(b + 1)];
            // Decoys arrive with the levels, as on the endless course.
            int decoy = -1;
            if (lvl >= 5 && t.unit() < std::min(0.45, 0.15 + 0.04 * lvl)) {
                do decoy = static_cast<int>(t.unit() * 4) % 4;
                while (decoy == target || (chord && decoy == rowFor(chord->note)));
            }
            if (chord && nextFree && target != rowFor(chord->note)) {
                // A chord with a free step after it: a long jump.
                s.cue = true;
                s.gap = true;
                s.row = target;
                s.row2 = rowFor(chord->note);
                s.note2 = chord->note;
                chord->manual = true;
                s.decoy = decoy;
                t.slots_.push_back(s);
                Slot landing;
                landing.row = target;
                t.slots_.push_back(landing);
                row = target;
                ++b; // the landing used the next step
                continue;
            }
            s.cue = true;
            s.row = target;
            s.decoy = decoy;
            row = target;
        }
        t.slots_.push_back(s);
    }
    t.length_ = static_cast<int64_t>(t.slots_.size());
    return t;
}

} // namespace lightfoot
