#pragma once

#include <cstdint>

namespace lightfoot {

class AudioEngine;

/// Pulls the engine through SDL's default output device on the Mac.
class SdlAudio {
public:
    explicit SdlAudio(AudioEngine& engine);
    ~SdlAudio();

    bool start();
    void stop();
    bool running() const { return device_ != 0; }

private:
    static void callback(void* userdata, unsigned char* stream, int len);

    AudioEngine& engine_;
    uint32_t device_ = 0;
};

} // namespace lightfoot
