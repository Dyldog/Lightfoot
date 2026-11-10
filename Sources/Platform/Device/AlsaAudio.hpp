#pragma once

#include <atomic>
#include <string>
#include <thread>

namespace lightfoot {

class AudioEngine;

/// Device output through tinyalsa: finds the codec card, opens it at 44.1 kHz
/// stereo, and feeds the engine from a realtime thread.
class AlsaAudio {
public:
    explicit AlsaAudio(AudioEngine& engine);
    ~AlsaAudio();

    bool start();
    void stop();
    const std::string& status() const { return status_; }

    /// Card number whose name or id contains `needle`, or -1. Reads /proc/asound/cards.
    static int findCard(const char* needle);

private:
    void run(int card);

    AudioEngine& engine_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::string status_;
};

} // namespace lightfoot
