#include "Platform/Host/HostLoop.hpp"

#include "Game/Game.hpp"

extern "C" {
#include "ui.h"
}

namespace lightfoot {

HostLoop::HostLoop(Game& app) : app_(app), pixels_(UI_W * UI_H, 0) {
    input_begin(&in_, &dev_);
}

void HostLoop::push(const panelmidi::Message& m) {
    input_push(&dev_, &in_, m.data());
    pending_ = true;
}

void HostLoop::pushButtonTap(int button) {
    push(panelmidi::buttonDown(button));
    // A press and release in the same drain cancel out in the parser, so the
    // release waits for the following frame.
    pendingRelease_.push_back(button);
}

void HostLoop::pushPadTap(int pad, int velocity) {
    push(panelmidi::padDown(pad, velocity));
    pendingPadRelease_.push_back(pad);
}

bool HostLoop::step(int64_t nowMs) {
    ui u{};
    u.px = pixels_.data();
    u.now_ms = nowMs;
    u.track_led = -1;
    const int quit = app_.frame(&u, &in_) || u.quit;

    for (int i = 0; i < u.nled; ++i) {
        const struct ui_led& l = u.led[i];
        padColors_[l.index] = (static_cast<uint32_t>(l.r) << 17) | (static_cast<uint32_t>(l.g) << 9) | (static_cast<uint32_t>(l.b) << 1);
    }
    for (int b = 0; b < 32; ++b)
        if (u.button_mask & (1u << b)) buttonLevels_[b] = u.button_level[b];
    wakeMs_ = u.wake_ms;

    // Honour ui_lift like the device loop does.
    dev_.held &= ~u.lift_buttons;
    if (u.lift_buttons & (1u << IN_SHIFT)) dev_.shift = 0;
    for (int p = 0; p < 16; ++p)
        if (u.lift_pads & (1u << p)) dev_.pad_down[p] = dev_.pad_vel[p] = dev_.pad_at[p] = 0;

    input_begin(&in_, &dev_);
    pending_ = false;
    for (int b : pendingRelease_) push(panelmidi::buttonUp(b));
    for (int p : pendingPadRelease_) push(panelmidi::padUp(p));
    pendingRelease_.clear();
    pendingPadRelease_.clear();
    return quit != 0;
}

} // namespace lightfoot
