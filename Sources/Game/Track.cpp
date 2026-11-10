#include "Game/Track.hpp"

#include <algorithm>

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

} // namespace lightfoot
