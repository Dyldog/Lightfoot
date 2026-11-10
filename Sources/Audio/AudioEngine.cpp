#include "Audio/AudioEngine.hpp"

#include <algorithm>
#include <cmath>

namespace lightfoot {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2 * kPi;

double midiHz(int note) {
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

// A minor, four bars: Am F C G. Pad triads, bass roots an octave or two down.
constexpr int kChords[4][3] = {{57, 60, 64}, {53, 57, 60}, {60, 64, 67}, {55, 59, 62}};
constexpr int kBassRoot[4] = {33, 29, 36, 31};
// A minor pentatonic over two octaves for the arpeggio.
constexpr int kPentatonic[10] = {57, 60, 62, 64, 67, 69, 72, 74, 76, 79};
constexpr int kArpPattern[16] = {0, 2, 4, 5, 4, 2, 1, 3, 5, 7, 6, 4, 3, 5, 2, 1};
constexpr int kBassPattern[16] = {1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 2, 0, 0, 1, 3}; // 1 root, 2 octave, 3 fifth

float expEnv(double t, double rate) {
    return t < 0 ? 0.0f : static_cast<float>(std::exp(-t * rate));
}

} // namespace

AudioEngine::AudioEngine(int sampleRate) : sampleRate_(sampleRate) {}

double AudioEngine::beat(int64_t nowMs) {
    if (!attached_ && lastClockMs_ > 0 && !paused_ && nowMs > lastClockMs_) {
        const double elapsed = std::min<int64_t>(nowMs - lastClockMs_, 1000) / 1000.0;
        beat_ = beat_ + elapsed * tempo_ / 60.0;
    }
    lastClockMs_ = nowMs;
    return beat_;
}

void AudioEngine::resetBeat(double startBeat) {
    beat_ = startBeat;
    lastStep_ = static_cast<int>(std::floor(startBeat * 4)) - 1;
}

float AudioEngine::noise() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(static_cast<int32_t>(rng_)) / 2147483648.0f;
}

void AudioEngine::trigger(Sfx sfx) {
    const int head = queueHead_.load(std::memory_order_relaxed);
    const int next = (head + 1) % kQueue;
    if (next == queueTail_.load(std::memory_order_acquire)) return; // full: drop, a sound is not worth a stall
    queue_[static_cast<size_t>(head)] = static_cast<int>(sfx);
    queueHead_.store(next, std::memory_order_release);
}

AudioEngine::Voice* AudioEngine::allocate(Voice::Kind kind) {
    Voice* oldest = nullptr;
    for (Voice& v : voices_) {
        if (v.kind == Voice::Off) {
            v = Voice{};
            v.kind = kind;
            return &v;
        }
        if (!oldest || v.t > oldest->t) oldest = &v;
    }
    *oldest = Voice{};
    oldest->kind = kind;
    return oldest;
}

void AudioEngine::startSfx(Sfx sfx) {
    auto tone = [&](int shape, double f0, double f1, double sweep, double decay, float amp, double delay = 0) {
        Voice* v = allocate(Voice::Tone);
        v->shape = shape;
        v->freq = f0;
        v->freqEnd = f1 > 0 ? f1 : f0;
        v->sweep = sweep;
        v->decay = decay;
        v->amp = amp;
        v->t = -delay;
        v->music = false;
    };
    auto whoosh = [&](double f0, double f1, double seconds, float amp, double delay = 0) {
        Voice* v = allocate(Voice::Noise);
        v->freq = f0;
        v->freqEnd = f1;
        v->decay = seconds;
        v->amp = amp;
        v->t = -delay;
        v->music = false;
    };
    switch (sfx) {
    case Sfx::Jump:
        whoosh(300, 3200, 0.18, 0.22f);
        tone(0, 220, 660, 0.12, 0.07, 0.14f);
        break;
    case Sfx::TwinJump:
        whoosh(300, 4000, 0.26, 0.26f);
        tone(0, 220, 660, 0.12, 0.08, 0.14f);
        tone(0, 330, 990, 0.14, 0.09, 0.12f, 0.05);
        break;
    case Sfx::Land:
        tone(0, 130, 48, 0.07, 0.06, 0.45f);
        whoosh(2500, 400, 0.04, 0.12f);
        break;
    case Sfx::Perfect:
        tone(0, 659.25, 0, 0, 0.22, 0.2f);
        tone(0, 987.77, 0, 0, 0.26, 0.18f, 0.07);
        tone(0, 1318.5, 0, 0, 0.32, 0.14f, 0.14);
        break;
    case Sfx::Good:
        tone(0, 659.25, 0, 0, 0.2, 0.18f);
        break;
    case Sfx::Miss:
        tone(1, 240, 70, 0.35, 0.28, 0.22f);
        whoosh(2000, 150, 0.3, 0.14f);
        break;
    case Sfx::Slip:
        tone(1, 110, 0, 0, 0.07, 0.2f);
        tone(1, 116, 0, 0, 0.07, 0.2f);
        break;
    case Sfx::LevelUp:
        tone(0, 880, 0, 0, 0.4, 0.16f, 0.00);
        tone(0, 1108.7, 0, 0, 0.4, 0.16f, 0.08);
        tone(0, 1318.5, 0, 0, 0.4, 0.16f, 0.16);
        tone(0, 1760, 0, 0, 0.5, 0.16f, 0.24);
        break;
    case Sfx::Tick:
        tone(0, 1500, 0, 0, 0.025, 0.1f);
        break;
    case Sfx::Start:
        tone(0, 440, 0, 0, 0.6, 0.14f);
        tone(0, 554.37, 0, 0, 0.6, 0.12f);
        tone(0, 659.25, 0, 0, 0.6, 0.12f);
        tone(0, 880, 0, 0, 0.7, 0.1f, 0.1);
        break;
    case Sfx::GameOver:
        tone(1, 330, 165, 0.6, 0.5, 0.16f);
        tone(0, 220, 110, 0.6, 0.5, 0.18f);
        tone(1, 247, 124, 0.6, 0.5, 0.1f, 0.2);
        break;
    case Sfx::Select:
        tone(0, 880, 0, 0, 0.05, 0.1f);
        break;
    }
}

void AudioEngine::onStep(int step) {
    const Mode mode = this->mode();
    if (mode == Mode::Silent) return;
    const int inBar = ((step % 16) + 16) % 16;
    const int level = std::max(1, intensity_.load());
    const bool play = mode == Mode::Play;
    const double beatSeconds = 60.0 / std::max(30.0, tempo_.load());
    if (inBar == 0) bar_ = (step / 16) % 4;
    const int chord = ((bar_ % 4) + 4) % 4;

    // Pad: one voice per chord note, held for the bar, released into the next.
    if (inBar == 0) {
        for (Voice& v : voices_)
            if (v.kind == Voice::Pad && !v.released) v.released = true;
        for (int n = 0; n < 3; ++n) {
            Voice* v = allocate(Voice::Pad);
            v->freq = midiHz(kChords[chord][n]);
            v->attack = 0.08;
            v->hold = beatSeconds * 4;
            v->amp = play ? 0.055f : 0.07f;
        }
    }
    // Kick on every beat; a soft one in the menu so the title still breathes.
    if (inBar % 4 == 0) {
        Voice* v = allocate(Voice::Kick);
        v->amp = play ? 0.95f : 0.0f;
    }
    // Clap on 2 and 4 from level 2.
    if (play && level >= 2 && (inBar == 4 || inBar == 12)) {
        Voice* v = allocate(Voice::Clap);
        v->amp = 0.4f;
    }
    // Hats: eighths, sixteenths from level 5; the last one of the bar opens.
    const bool hatStep = level >= 5 ? true : inBar % 2 == 0;
    if (hatStep) {
        Voice* v = allocate(Voice::Hat);
        const bool open = inBar == 14;
        v->decay = open ? 9 : 55;
        v->amp = (play ? 0.2f : 0.1f) * (inBar % 4 == 2 ? 1.0f : 0.7f) * (open ? 1.3f : 1.0f);
    }
    // Bass follows the root with an octave kick-up, pumping under the kick.
    if (play && kBassPattern[inBar]) {
        Voice* v = allocate(Voice::Bass);
        const int root = kBassRoot[chord];
        const int note = kBassPattern[inBar] == 2 ? root + 12 : kBassPattern[inBar] == 3 ? root + 7 : root;
        v->freq = midiHz(note);
        v->hold = beatSeconds * 0.3;
        v->amp = 0.42f;
    }
    // Arpeggio from level 3 (every eighth in the menu, quietly).
    const bool arpStep = play ? level >= 3 : inBar % 2 == 0;
    if (arpStep) {
        Voice* v = allocate(Voice::Arp);
        const int idx = (kArpPattern[inBar] + chord) % 10;
        v->freq = midiHz(kPentatonic[idx]);
        v->decay = 9;
        v->amp = play ? (level >= 6 ? 0.11f : 0.08f) : 0.05f;
    }
}

float AudioEngine::renderVoice(Voice& v, double dt, float& right) {
    right = 0;
    if (v.t < 0) {
        v.t += dt;
        return 0;
    }
    const double t = v.t;
    v.t += dt;
    float out = 0;
    switch (v.kind) {
    case Voice::Kick: {
        const double f = 45 + 125 * std::exp(-t * 26);
        v.phase += f * dt;
        const float env = expEnv(t, 6.5);
        out = static_cast<float>(std::sin(kTwoPi * v.phase)) * env + noise() * expEnv(t, 180) * 0.4f;
        out *= v.amp;
        kickEnv_ = std::max(kickEnv_.load(), env);
        if (t > 0.6) v.kind = Voice::Off;
        break;
    }
    case Voice::Clap: {
        const float x = noise();
        v.lp += 0.25f * (x - v.lp);
        const float band = x - v.lp;
        const float env = expEnv(t, 16) * (1.0f + 0.6f * static_cast<float>(std::sin(t * 2 * kPi * 55)));
        out = band * env * v.amp;
        if (t > 0.35) v.kind = Voice::Off;
        break;
    }
    case Voice::Hat: {
        const float x = noise();
        const float d = x - v.noisePrev;
        v.noisePrev = x;
        out = d * expEnv(t, v.decay) * v.amp;
        if (t > (v.decay < 20 ? 0.6 : 0.12)) v.kind = Voice::Off;
        break;
    }
    case Voice::Bass: {
        v.phase += v.freq * dt;
        if (v.phase >= 1) v.phase -= 1;
        const float saw = static_cast<float>(2 * v.phase - 1);
        const float sq = v.phase < 0.5 ? 1.0f : -1.0f;
        const float mix = 0.6f * saw + 0.4f * sq;
        const double cutoff = 110 + 2400 * std::exp(-t * 11);
        const float a = static_cast<float>(1 - std::exp(-kTwoPi * cutoff * dt));
        v.lp += a * (mix - v.lp);
        v.lp2 += a * (v.lp - v.lp2);
        const float gate = t < v.hold ? 1.0f : expEnv(t - v.hold, 28);
        out = v.lp2 * gate * v.amp;
        if (t > v.hold + 0.25) v.kind = Voice::Off;
        break;
    }
    case Voice::Pad: {
        v.phase += v.freq * dt;
        v.phase2 += v.freq * 1.004 * dt;
        v.phase3 += v.freq * 0.9955 * dt;
        if (v.phase >= 1) v.phase -= 1;
        if (v.phase2 >= 1) v.phase2 -= 1;
        if (v.phase3 >= 1) v.phase3 -= 1;
        const float saws = static_cast<float>((2 * v.phase - 1) + (2 * v.phase2 - 1) + (2 * v.phase3 - 1)) / 3.0f;
        const double cutoff = 700 + 1800 * brightness_ + 90 * std::min(8, intensity_.load());
        const float a = static_cast<float>(1 - std::exp(-kTwoPi * cutoff * dt));
        v.lp += a * (saws - v.lp);
        v.lp2 += a * (v.lp - v.lp2);
        float env = static_cast<float>(std::min(1.0, t / v.attack));
        if (v.released || t > v.hold) {
            const double since = v.released && t <= v.hold ? 0 : t - v.hold;
            env *= expEnv(std::max(0.0, since), 3.5);
            if (v.released) {
                v.decay += dt; // reuse as release clock
                env *= expEnv(v.decay, 3.5);
            }
        }
        out = v.lp2 * env * v.amp;
        // Width: the detuned partner leans right.
        right = static_cast<float>(2 * v.phase2 - 1) * env * v.amp * 0.5f;
        if ((v.released && v.decay > 1.5) || t > v.hold + 2.0) v.kind = Voice::Off;
        break;
    }
    case Voice::Arp: {
        v.phase += v.freq * dt;
        if (v.phase >= 1) v.phase -= 1;
        const float tri = static_cast<float>(4 * std::fabs(v.phase - 0.5) - 1);
        const float a = static_cast<float>(1 - std::exp(-kTwoPi * 2600 * dt));
        v.lp += a * (tri - v.lp);
        out = v.lp * expEnv(t, v.decay) * v.amp;
        right = out * 0.6f;
        out *= 0.8f;
        if (t > 0.6) v.kind = Voice::Off;
        break;
    }
    case Voice::Tone: {
        double f = v.freq;
        if (v.sweep > 0 && v.freqEnd != v.freq) {
            const double k = std::min(1.0, t / v.sweep);
            f = v.freq * std::pow(v.freqEnd / v.freq, k);
        }
        v.phase += f * dt;
        if (v.phase >= 1) v.phase -= 1;
        float wave;
        switch (v.shape) {
        case 1: wave = v.phase < 0.5 ? 1.0f : -1.0f; break;
        case 2: wave = static_cast<float>(2 * v.phase - 1); break;
        case 3: wave = static_cast<float>(4 * std::fabs(v.phase - 0.5) - 1); break;
        default: wave = static_cast<float>(std::sin(kTwoPi * v.phase)); break;
        }
        const float env = static_cast<float>(std::min(1.0, t / 0.003)) * expEnv(t, 1.0 / std::max(0.005, v.decay));
        out = wave * env * v.amp;
        if (t > v.decay * 7) v.kind = Voice::Off;
        break;
    }
    case Voice::Noise: {
        const double k = std::min(1.0, t / std::max(0.001, v.decay));
        const double cutoff = v.freq * std::pow(v.freqEnd / v.freq, k);
        const float a = static_cast<float>(1 - std::exp(-kTwoPi * cutoff * dt));
        v.lp += a * (noise() - v.lp);
        const float env = static_cast<float>(std::sin(kPi * std::min(1.0, k)));
        out = v.lp * env * v.amp;
        if (t > v.decay) v.kind = Voice::Off;
        break;
    }
    default: v.kind = Voice::Off; break;
    }
    return out;
}

void AudioEngine::render(float* out, int frames) {
    // Pull the sounds the game asked for since the last block.
    while (queueTail_.load(std::memory_order_acquire) != queueHead_.load(std::memory_order_acquire)) {
        const int tail = queueTail_.load(std::memory_order_relaxed);
        startSfx(static_cast<Sfx>(queue_[static_cast<size_t>(tail)]));
        queueTail_.store((tail + 1) % kQueue, std::memory_order_release);
    }
    const double dt = 1.0 / sampleRate_;
    const double beatsPerSample = tempo_ / 60.0 * dt;
    const bool paused = paused_;
    const Mode mode = this->mode();
    const float menuTarget = mode == Mode::Silent || paused ? 0.0f : 1.0f;
    const float gain = masterGain_;
    double beat = beat_;
    float peakL = 0, peakR = 0;
    kickEnv_ = kickEnv_ * 0.6f;
    const double busCutoff = paused ? 500.0 : 1800.0 + 9000.0 * std::min(1.0f, 0.15f * intensity_ + brightness_);
    const float busA = static_cast<float>(1 - std::exp(-kTwoPi * busCutoff * dt));
    for (int i = 0; i < frames; ++i) {
        if (!paused && mode != Mode::Silent) {
            beat += beatsPerSample;
            const int step = static_cast<int>(std::floor(beat * 4));
            if (step != lastStep_) {
                lastStep_ = step;
                onStep(step);
            }
        }
        menuFade_ += (menuTarget - menuFade_) * 0.0005f;
        float musicL = 0, musicR = 0, fxL = 0, fxR = 0;
        float kick = 0;
        for (Voice& v : voices_) {
            if (v.kind == Voice::Off) continue;
            const Voice::Kind kind = v.kind;
            float r = 0;
            const float l = renderVoice(v, dt, r);
            if (kind == Voice::Kick) {
                kick += l;
                continue;
            }
            if (v.music) {
                musicL += l;
                musicR += l * 0.8f + r;
            } else {
                fxL += l;
                fxR += l + r;
            }
        }
        // Sidechain pump: everything melodic ducks under the kick.
        duck_ = std::max(duck_ * 0.9993f, std::min(1.0f, std::fabs(kick) * 1.4f));
        const float pump = 1.0f - 0.5f * duck_;
        busLp_ += busA * (musicL - busLp_);
        busLpR_ += busA * (musicR - busLpR_);
        float left = (busLp_ * pump + kick) * menuFade_ + fxL;
        float right = (busLpR_ * pump + kick) * menuFade_ + fxR;
        left = std::tanh(left * 1.3f * gain);
        right = std::tanh(right * 1.3f * gain);
        out[i * 2] = left;
        out[i * 2 + 1] = right;
        peakL = std::max(peakL, std::fabs(left));
        peakR = std::max(peakR, std::fabs(right));
    }
    beat_ = beat;
    peakLeft_ = peakL;
    peakRight_ = peakR;
}

} // namespace lightfoot
