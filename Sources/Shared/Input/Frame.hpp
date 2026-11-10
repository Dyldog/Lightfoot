#pragma once

extern "C" {
#include "input.h"
}

namespace lightfoot {

/// One frame of panel input, wrapping the toolkit's C struct with readable accessors.
/// Button identifiers are the toolkit's IN_* values.
class Frame {
public:
    explicit Frame(const input& in) : in_(in) {}

    const input& raw() const { return in_; }

    bool pressed(int button) const { return input_down(&in_, button) != 0; }
    bool released(int button) const { return (in_.up >> button) & 1u; }
    bool held(int button) const { return (in_.held >> button) & 1u; }
    bool shift() const { return in_.shift != 0; }

    int encoder() const { return in_.encoder; }
    bool encoderPressed() const { return in_.encoder_press != 0; }

    /// Velocity 1..127 when the pad was struck this frame, otherwise -1.
    int padHit(int pad) const { return in_.pad_hit[pad]; }
    bool padReleased(int pad) const { return in_.pad_release[pad] != 0; }
    bool padDown(int pad) const { return in_.pad_down[pad] != 0; }
    int padPressure(int pad) const { return in_.pad_pressure[pad]; }

    bool knobMoved(int k) const { return in_.knob_moved[k] != 0; }
    /// Absolute 0..127 when moved this frame.
    int knob(int k) const { return in_.knob[k]; }
    int knobPosition(int k) const { return in_.knob_pos[k]; }

    /// The fader, absolute 0..127, when it moved this frame.
    bool faderMoved() const { return in_.fader_moved != 0; }
    int fader() const { return in_.fader; }

    /// MAIN VOLUME knob, absolute 0..127, when it moved this frame.
    bool mainMoved() const { return in_.main_moved != 0; }
    int main() const { return in_.main; }

    /// True when any pad, button, knob or encoder changed this frame.
    bool any() const {
        if (in_.encoder || in_.encoder_press || in_.down || in_.up) return true;
        if (in_.main_moved || in_.fader_moved) return true;
        for (int i = 0; i < 3; ++i)
            if (in_.knob_moved[i]) return true;
        for (int i = 0; i < 16; ++i)
            if (in_.pad_hit[i] >= 0 || in_.pad_release[i]) return true;
        return false;
    }

private:
    const input& in_;
};

} // namespace lightfoot
