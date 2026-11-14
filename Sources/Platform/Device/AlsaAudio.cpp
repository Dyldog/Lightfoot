#include "Platform/Device/AlsaAudio.hpp"

#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Audio/AudioEngine.hpp"

extern "C" {
#include <tinyalsa/mixer.h>
#include <tinyalsa/pcm.h>
}

namespace lightfoot {

namespace {
constexpr unsigned kPeriod = 256;
constexpr unsigned kPeriods = 4;

bool endsWith(const char* s, const char* suffix) {
    const size_t n = std::strlen(s), m = std::strlen(suffix);
    return n >= m && std::strcmp(s + n - m, suffix) == 0;
}

// The codec keeps whatever the last program left in its mixer. The stock app
// routes the built-in speaker by the jacks, and a sample-base app (Melodee)
// killed while it held the speaker off for a click-free power-up leaves it off
// until something turns it back on or the MPC restarts. So, once our stream
// has the codec powered and the board's mute open, log every control and open
// the output path: playback switches on, a fully attenuated playback volume
// raised, and the speaker on unless headphones or line out are plugged in.
void prepareCodec(int card) {
    mixer* m = mixer_open(static_cast<unsigned>(card));
    if (!m) {
        std::fprintf(stderr, "mixer: card %d not opened: %s\n", card, std::strerror(errno));
        return;
    }
    const unsigned count = mixer_get_num_ctls(m);
    int headphones = -1, lineOut = -1;
    mixer_ctl* speaker = nullptr;
    for (unsigned i = 0; i < count; ++i) {
        mixer_ctl* c = mixer_get_ctl(m, i);
        if (!c) continue;
        const char* name = mixer_ctl_get_name(c);
        const mixer_ctl_type type = mixer_ctl_get_type(c);
        const unsigned values = mixer_ctl_get_num_values(c);
        std::string shown;
        for (unsigned v = 0; v < values && v < 8; ++v) {
            const int value = mixer_ctl_get_value(c, v);
            if (type == MIXER_CTL_TYPE_ENUM) {
                const char* label = value >= 0 ? mixer_ctl_get_enum_string(c, static_cast<unsigned>(value)) : nullptr;
                shown += std::string(" ") + (label ? label : "?");
            } else {
                shown += " " + std::to_string(value);
            }
        }
        if (type == MIXER_CTL_TYPE_INT)
            shown += " (" + std::to_string(mixer_ctl_get_range_min(c)) + ".." + std::to_string(mixer_ctl_get_range_max(c)) + ")";
        std::fprintf(stderr, "mixer: %u %s [%s]%s\n", i, name, mixer_ctl_get_type_string(c), shown.c_str());

        if (std::strcmp(name, "Headphones Jack") == 0 && values) {
            headphones = mixer_ctl_get_value(c, 0);
        } else if (std::strcmp(name, "Line Out Jack") == 0 && values) {
            lineOut = mixer_ctl_get_value(c, 0);
        } else if (std::strcmp(name, "Builtin Speaker Switch") == 0) {
            speaker = c;
        } else if (type == MIXER_CTL_TYPE_BOOL && endsWith(name, "Playback Switch")) {
            for (unsigned v = 0; v < values; ++v)
                if (mixer_ctl_get_value(c, v) == 0) {
                    const int rc = mixer_ctl_set_value(c, v, 1);
                    std::fprintf(stderr, "mixer: %s was off, switched on%s\n", name, rc < 0 ? " (failed)" : "");
                }
        } else if (type == MIXER_CTL_TYPE_INT && endsWith(name, "Playback Volume")) {
            const int min = mixer_ctl_get_range_min(c), max = mixer_ctl_get_range_max(c);
            for (unsigned v = 0; v < values; ++v)
                if (mixer_ctl_get_value(c, v) <= min && max > min) {
                    const int rc = mixer_ctl_set_value(c, v, max);
                    std::fprintf(stderr, "mixer: %s was at its minimum, raised to %d%s\n", name, max, rc < 0 ? " (failed)" : "");
                }
        }
    }
    if (speaker) {
        const int want = (headphones == 1 || lineOut == 1) ? 0 : 1;
        const int have = mixer_ctl_get_value(speaker, 0);
        if (have != want) {
            const int rc = mixer_ctl_set_value(speaker, 0, want);
            std::fprintf(stderr, "mixer: Builtin Speaker Switch %d -> %d (headphones %d, line out %d)%s\n", have, want, headphones, lineOut,
                         rc < 0 ? " failed" : "");
        } else {
            std::fprintf(stderr, "mixer: speaker %s, headphones %d, line out %d\n", have ? "on" : "off", headphones, lineOut);
        }
    } else {
        std::fprintf(stderr, "mixer: no Builtin Speaker Switch on card %d\n", card);
    }
    mixer_close(m);
}
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
    int64_t blocks = 0;
    float blockPeak = 0;
    while (!stop_) {
        engine_.render(mix.data(), kPeriod);
        // Every five seconds, say what is actually leaving the engine: a silent
        // card with a healthy peak is a codec or cable problem, a zero peak is ours.
        for (float v : mix) blockPeak = std::max(blockPeak, std::fabs(v));
        // One second in, the codec is powered and the board's mute is open:
        // now set the mixer, so the speaker does not amplify the power-up click.
        if (++blocks == 44100 / kPeriod) prepareCodec(card);
        if (blocks % (44100 * 5 / kPeriod) == 0) {
            std::fprintf(stderr, "output: 5s peak %.3f mode %d beat %.1f gain %.2f underruns %d\n", static_cast<double>(blockPeak),
                         static_cast<int>(engine_.mode()), engine_.positionBeats(), static_cast<double>(engine_.masterGain()), underruns);
            blockPeak = 0;
        }
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
