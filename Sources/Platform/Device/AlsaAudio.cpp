#include "Platform/Device/AlsaAudio.hpp"

#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Audio/AudioEngine.hpp"

extern "C" {
#include <tinyalsa/pcm.h>
}

namespace lightfoot {

namespace {
constexpr unsigned kPeriod = 256;
constexpr unsigned kPeriods = 4;
} // namespace

AlsaAudio::AlsaAudio(AudioEngine& engine) : engine_(engine) {}

AlsaAudio::~AlsaAudio() {
    stop();
}

int AlsaAudio::findCard(const char* needle) {
    std::FILE* f = std::fopen("/proc/asound/cards", "r");
    if (!f) return -1;
    char line[256];
    int found = -1;
    while (std::fgets(line, sizeof line, f)) {
        int idx;
        if (std::sscanf(line, " %d [", &idx) == 1 && std::strstr(line, needle)) {
            found = idx;
            break;
        }
    }
    std::fclose(f);
    return found;
}

bool AlsaAudio::start() {
    int card = findCard("I2S");
    if (card < 0) card = findCard("platform");
    if (card < 0) card = findCard("rockchip");
    if (card < 0) card = 0;
    stop_ = false;
    thread_ = std::thread([this, card] { run(card); });
    return true;
}

void AlsaAudio::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    engine_.setAttached(false);
}

void AlsaAudio::run(int card) {
    pcm_config config{};
    config.channels = 2;
    config.rate = static_cast<unsigned>(engine_.sampleRate());
    config.period_size = kPeriod;
    config.period_count = kPeriods;
    config.format = PCM_FORMAT_S16_LE;
    config.start_threshold = kPeriod * 2;
    config.stop_threshold = kPeriod * kPeriods;
    config.silence_threshold = 0;

    pcm* pcm = pcm_open(static_cast<unsigned>(card), 0, PCM_OUT | PCM_NORESTART, &config);
    bool is32 = false;
    if (!pcm || !pcm_is_ready(pcm)) {
        if (pcm) pcm_close(pcm);
        config.format = PCM_FORMAT_S32_LE;
        pcm = pcm_open(static_cast<unsigned>(card), 0, PCM_OUT | PCM_NORESTART, &config);
        is32 = true;
    }
    if (!pcm || !pcm_is_ready(pcm)) {
        status_ = std::string("pcm_open failed: ") + (pcm ? pcm_get_error(pcm) : "null");
        std::fprintf(stderr, "alsa: %s (card %d)\n", status_.c_str(), card);
        if (pcm) pcm_close(pcm);
        return;
    }
    status_ = "card " + std::to_string(card) + (is32 ? " S32" : " S16");
    std::fprintf(stderr, "output: opened %s, period %u x %u\n", status_.c_str(), kPeriod, kPeriods);

    sched_param sp{};
    sp.sched_priority = 40; // below PREEMPT_RT's threaded IRQ handlers at 50
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);

    std::vector<float> mix(kPeriod * 2);
    std::vector<int16_t> s16(kPeriod * 2);
    std::vector<int32_t> s32(kPeriod * 2);
    engine_.setAttached(true);
    int underruns = 0;
    while (!stop_) {
        engine_.render(mix.data(), kPeriod);
        int rc;
        if (is32) {
            for (size_t i = 0; i < mix.size(); ++i) s32[i] = static_cast<int32_t>(std::lround(std::clamp(mix[i], -1.0f, 1.0f) * 2147483392.0f));
            rc = pcm_writei(pcm, s32.data(), kPeriod);
        } else {
            for (size_t i = 0; i < mix.size(); ++i) s16[i] = static_cast<int16_t>(std::lround(std::clamp(mix[i], -1.0f, 1.0f) * 32767.0f));
            rc = pcm_writei(pcm, s16.data(), kPeriod);
        }
        if (rc < 0) {
            const int err = errno;
            if (++underruns <= 5 || underruns % 100 == 0) std::fprintf(stderr, "output: underrun %d: %s\n", underruns, std::strerror(err));
            pcm_prepare(pcm);
        }
    }
    std::fprintf(stderr, "output: closed, %d underruns\n", underruns);
    pcm_close(pcm);
}

} // namespace lightfoot
