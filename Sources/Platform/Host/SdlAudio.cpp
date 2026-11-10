#include "Platform/Host/SdlAudio.hpp"

#include <SDL.h>

#include <cstdio>

#include "Audio/AudioEngine.hpp"

namespace lightfoot {

SdlAudio::SdlAudio(AudioEngine& engine) : engine_(engine) {}

SdlAudio::~SdlAudio() {
    stop();
}

void SdlAudio::callback(void* userdata, unsigned char* stream, int len) {
    auto* self = static_cast<SdlAudio*>(userdata);
    self->engine_.render(reinterpret_cast<float*>(stream), len / static_cast<int>(sizeof(float) * 2));
}

bool SdlAudio::start() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL audio: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want{}, have{};
    want.freq = engine_.sampleRate();
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 256;
    want.callback = callback;
    want.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!device_) {
        std::fprintf(stderr, "SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return false;
    }
    engine_.setAttached(true);
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void SdlAudio::stop() {
    if (!device_) return;
    engine_.setAttached(false);
    SDL_CloseAudioDevice(device_);
    device_ = 0;
}

} // namespace lightfoot
