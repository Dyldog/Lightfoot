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

std::vector<int> Track::contourRows(const std::vector<int>& pitches, int startRow) {
    const int n = static_cast<int>(pitches.size());
    std::vector<int> rows(static_cast<size_t>(n), std::clamp(startRow, 0, 3));
    if (n == 0) return rows;
    auto direction = [&](int i) { // movement into note i: +1 higher, -1 lower, 0 same
        if (i <= 0 || i >= n) return 0;
        return pitches[static_cast<size_t>(i)] > pitches[static_cast<size_t>(i - 1)] ? 1 : pitches[static_cast<size_t>(i)] < pitches[static_cast<size_t>(i - 1)] ? -1 : 0;
    };
    auto runFrom = [&](int i) { // consecutive moves in the direction of the move into note i
        const int d = direction(i);
        if (d == 0) return 0;
        int len = 0;
        while (i + len < n && direction(i + len) == d) ++len;
        return len;
    };
    // The first note sits where the opening run can be played.
    {
        const int d = direction(1), len = runFrom(1);
        if (len >= 2) rows[0] = d > 0 ? 3 : 0;
    }
    int planned = -1, plannedLen = 0, plannedStart = 0; // a long run being spread across the height
    for (int i = 1; i < n; ++i) {
        const int prev = rows[static_cast<size_t>(i - 1)];
        const int d = direction(i);
        if (d == 0) {
            rows[static_cast<size_t>(i)] = prev;
            continue;
        }
        if (i > planned) {
            // A new run begins here. Four or more moves: spread three rows over
            // it; the note before it has already been placed at the far end.
            const int len = runFrom(i);
            if (len >= 4) {
                planned = i + len - 1;
                plannedLen = len;
                plannedStart = i;
            } else {
                planned = -1;
            }
        }
        int row;
        if (planned >= 0) {
            const int k = i - plannedStart + 1; // 1..len
            const int origin = rows[static_cast<size_t>(plannedStart - 1)];
            const int travel = (k * 3) / plannedLen;
            row = d > 0 ? origin - travel : origin + travel;
        } else {
            row = prev - d; // up is a smaller row number
        }
        rows[static_cast<size_t>(i)] = std::clamp(row, 0, 3);
        // Look ahead: if a long run starts after this note, put this note at
        // the far end so the run has the whole height, even if that is a jump.
        if (i + 1 < n) {
            const int nextD = direction(i + 1), nextLen = runFrom(i + 1);
            if (nextLen >= 4 && nextD != 0) rows[static_cast<size_t>(i)] = nextD > 0 ? 3 : 0;
        }
    }
    return rows;
}

Track Track::fromSong(MidiSong& song, int startLevel, int difficulty) {
    difficulty = std::clamp(difficulty, 0, 3);
    Track t(static_cast<uint32_t>(song.notes.size() * 7919 + 17), startLevel);
    t.finite_ = true;
    t.beatsPerBar_ = song.beatsPerBar;
    t.songName_ = song.name;
    int offBeat = 0, melodyNotes = 0;
    for (const MidiNote& n : song.notes) {
        if (n.channel != song.melodyChannel) continue;
        ++melodyNotes;
        const double frac = n.beat - std::floor(n.beat);
        if (std::fabs(frac - 0.5) < 0.15) ++offBeat;
    }
    // Notes on the off-beat mean the course runs in half-beat steps, so the
    // rhythm is played as written rather than rounded to the beat.
    t.stepBeats_ = melodyNotes > 0 && offBeat * 10 >= melodyNotes ? 0.5 : 1.0;
    const double step = t.stepBeats_;
    // One candidate per step: the first (highest) note on it, quantised.
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
        else if (std::fabs(n.beat - slotNote->beat) < 0.05 && n.note != slotNote->note && !second[static_cast<size_t>(b)]) second[static_cast<size_t>(b)] = &n;
    }
    // The chart: a simplified melody that fills in as the levels rise. The
    // strong beats first, then every beat, then every note; held notes stay.
    struct ChartNote {
        int64_t step;
        MidiNote* note;
        MidiNote* chord;
    };
    std::vector<ChartNote> chart;
    for (int64_t b = 0; b < steps; ++b) {
        MidiNote* n = onStep[static_cast<size_t>(b)];
        if (!n || b * step < 4) continue;
        if (!chart.empty() && chart.back().step == b - 1 && chart.back().chord && b + 0 == chart.back().step + 1) continue; // a long jump lands here
        const int lvl = t.level(b);
        const double beatPos = b * step;
        const bool onBeat = std::fabs(beatPos - std::round(beatPos)) < 1e-9;
        const bool strong = onBeat && (static_cast<int64_t>(std::llround(beatPos)) % 2 == 0);
        const bool held = n->length >= 1.0;
        // The denser of the difficulty's floor and the level's tier.
        const int levelTier = lvl <= 2 ? 0 : lvl <= 4 ? 1 : 2;
        const int tier = std::max(std::min(difficulty, 2), levelTier);
        bool keep = true;
        if (tier == 0) keep = strong || held;
        else if (tier == 1) keep = onBeat || held;
        if (!keep) continue;
        MidiNote* chord = second[static_cast<size_t>(b)];
        const bool nextFree = b + 1 < steps && !onStep[static_cast<size_t>(b + 1)];
        const bool chordsAllowed = difficulty >= 1 && (difficulty >= 3 || lvl >= 3);
        chart.push_back({b, n, chord && nextFree && chordsAllowed ? chord : nullptr});
    }
    std::vector<int> pitches;
    pitches.reserve(chart.size());
    for (const ChartNote& c : chart) pitches.push_back(c.note->note);
    const std::vector<int> rows = contourRows(pitches, 2);

    t.slots_.reserve(static_cast<size_t>(steps) + 8);
    int row = rows.empty() ? 3 : rows.front();
    size_t next = 0;
    for (int64_t b = 0; b < steps; ++b) {
        Slot s;
        s.row = row;
        if (next < chart.size() && chart[next].step == b) {
            const ChartNote& c = chart[next];
            const int lvl = t.level(b);
            const int target = rows[next];
            // Repeated notes stay on the same row: still a hop, still the note.
            s.cue = true;
            s.row = target;
            s.note = c.note->note;
            s.velocity = c.note->velocity;
            s.noteBeats = c.note->length;
            c.note->manual = true;
            const bool decoysAllowed = difficulty >= 3 || (difficulty >= 2 && lvl >= 5);
            if (decoysAllowed && t.unit() < std::min(0.45, 0.15 + 0.04 * lvl)) {
                int d;
                do d = static_cast<int>(t.unit() * 4) % 4;
                while (d == target || d == target + (c.note->note > (c.chord ? c.chord->note : c.note->note) ? 1 : -1));
                s.decoy = d;
            }
            if (c.chord) {
                // A chord with a free step after it: a long jump on two pads, the
                // second a row towards the chord's other note.
                s.gap = true;
                s.row2 = std::clamp(target + (c.chord->note > c.note->note ? -1 : 1), 0, 3);
                if (s.row2 == target) s.row2 = target == 0 ? 1 : target - 1;
                s.note2 = c.chord->note;
                c.chord->manual = true;
                if (s.decoy == s.row2) s.decoy = -1;
                t.slots_.push_back(s);
                Slot landing;
                landing.row = target;
                t.slots_.push_back(landing);
                row = target;
                ++next;
                ++b; // the landing used the next step
                continue;
            }
            row = target;
            ++next;
        }
        t.slots_.push_back(s);
    }
    t.length_ = static_cast<int64_t>(t.slots_.size());
    return t;
}

} // namespace lightfoot
