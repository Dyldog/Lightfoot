#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Platform/Host/PanelMidi.hpp"

extern "C" {
#include "input.h"
}

namespace lightfoot {

class Game;

/// Runs the app's frame loop on the host. Input arrives as panel MIDI messages
/// through the same parser the device uses, so the simulator and device agree.
class HostLoop {
public:
    explicit HostLoop(Game& app);

    /// Queues a message for the next frame.
    void push(const panelmidi::Message& m);
    void pushButtonTap(int button);
    void pushPadTap(int pad, int velocity = 100);

    /// Draws one frame at `nowMs`. Returns true when the app asked to quit.
    bool step(int64_t nowMs);

    const uint32_t* pixels() const { return pixels_.data(); }
    const std::array<uint32_t, 16>& padColors() const { return padColors_; }
    const std::array<unsigned char, 32>& buttonLevels() const { return buttonLevels_; }
    int wakeMs() const { return wakeMs_; }
    bool hasPendingInput() const { return pending_; }

private:
    Game& app_;
    input_dev dev_{};
    input in_{};
    bool pending_ = false;
    std::vector<uint32_t> pixels_;
    std::array<uint32_t, 16> padColors_{};
    std::array<unsigned char, 32> buttonLevels_{};
    int wakeMs_ = 0;
    std::vector<int> pendingRelease_;
    std::vector<int> pendingPadRelease_;
};

} // namespace lightfoot
