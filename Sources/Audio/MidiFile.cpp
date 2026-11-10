#include "Audio/MidiFile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>

namespace lightfoot {

namespace {

struct Reader {
    const std::vector<uint8_t>& d;
    size_t pos = 0;
    bool ok = true;
    uint32_t u8() {
        if (pos >= d.size()) {
            ok = false;
            return 0;
        }
        return d[pos++];
    }
    uint32_t u16() { return (u8() << 8) | u8(); }
    uint32_t u32() { return (u16() << 16) | u16(); }
    uint32_t varlen() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const uint32_t b = u8();
            v = (v << 7) | (b & 0x7f);
            if (!(b & 0x80)) break;
        }
        return v;
    }
    bool tag(const char* t) {
        if (pos + 4 > d.size()) return false;
        const bool match = std::equal(t, t + 4, d.begin() + static_cast<long>(pos));
        pos += 4;
        return match;
    }
};

struct RawTempo {
    uint64_t tick;
    double bpm;
};

} // namespace

std::shared_ptr<MidiSong> MidiSong::load(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open";
        return nullptr;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto song = parse(bytes, error);
    if (song) {
        std::string base = path;
        const std::string::size_type slash = base.rfind('/');
        if (slash != std::string::npos) base = base.substr(slash + 1);
        const std::string::size_type dot = base.rfind('.');
        if (dot != std::string::npos && dot > 0) base = base.substr(0, dot);
        if (song->name.empty()) song->name = base;
    }
    return song;
}

std::shared_ptr<MidiSong> MidiSong::parse(const std::vector<uint8_t>& bytes, std::string* error) {
    Reader r{bytes};
    if (!r.tag("MThd")) {
        if (error) *error = "not a MIDI file";
        return nullptr;
    }
    const uint32_t headerLen = r.u32();
    const uint32_t format = r.u16();
    const uint32_t tracks = r.u16();
    const uint32_t division = r.u16();
    r.pos += headerLen > 6 ? headerLen - 6 : 0;
    if (!r.ok || format > 2) {
        if (error) *error = "unsupported MIDI format";
        return nullptr;
    }
    if (division & 0x8000) {
        if (error) *error = "SMPTE timing is not supported";
        return nullptr;
    }
    const double ticksPerBeat = division ? static_cast<double>(division) : 96.0;

    auto song = std::make_shared<MidiSong>();
    std::vector<RawTempo> rawTempos;
    std::string firstTrackName;
    for (auto& c : song->channels) c = ChannelInfo{};
    struct Pending {
        uint64_t tick;
        int velocity;
    };
    for (uint32_t t = 0; t < tracks && r.ok; ++t) {
        if (!r.tag("MTrk")) {
            if (error) *error = "bad track chunk";
            return nullptr;
        }
        const uint32_t len = r.u32();
        const size_t end = std::min(bytes.size(), r.pos + len);
        uint64_t tick = 0;
        uint8_t running = 0;
        int trackLyrics = 0;
        std::array<bool, 16> trackChannels{};
        std::map<int, std::vector<Pending>> open; // key: channel * 128 + note
        while (r.pos < end && r.ok) {
            tick += r.varlen();
            uint32_t status = r.u8();
            if (status < 0x80) {
                // Running status: the byte we read is data.
                if (!running) {
                    if (error) *error = "corrupt track";
                    return nullptr;
                }
                --r.pos;
                status = running;
            }
            if (status == 0xff) {
                const uint32_t type = r.u8();
                const uint32_t mlen = r.varlen();
                const size_t mstart = r.pos;
                if (type == 0x51 && mlen == 3) {
                    const uint32_t us = (r.u8() << 16) | (r.u8() << 8) | r.u8();
                    if (us > 0) rawTempos.push_back({tick, 60000000.0 / us});
                } else if (type == 0x58 && mlen >= 2) {
                    const uint32_t num = r.u8();
                    const uint32_t denPow = r.u8();
                    if (tick == 0 && num >= 2 && num <= 12) song->beatsPerBar = denPow == 3 ? static_cast<int>(num) / 2 : static_cast<int>(num);
                    if (song->beatsPerBar < 2) song->beatsPerBar = 4;
                } else if (type == 0x05 || type == 0x01) {
                    if (type == 0x05) ++trackLyrics;
                } else if (type == 0x03 && firstTrackName.empty() && mlen > 0) {
                    firstTrackName.assign(reinterpret_cast<const char*>(&bytes[r.pos]), std::min<size_t>(mlen, bytes.size() - r.pos));
                }
                r.pos = mstart + mlen;
                continue;
            }
            if (status == 0xf0 || status == 0xf7) {
                const uint32_t slen = r.varlen();
                r.pos += slen;
                continue;
            }
            running = static_cast<uint8_t>(status);
            const int kind = status & 0xf0;
            const int channel = status & 0x0f;
            const uint32_t d1 = r.u8();
            const uint32_t d2 = (kind == 0xc0 || kind == 0xd0) ? 0 : r.u8();
            if (kind == 0xc0) {
                if (song->channels[static_cast<size_t>(channel)].program < 0) song->channels[static_cast<size_t>(channel)].program = static_cast<int>(d1);
            }
            if (kind == 0x90 && d2 > 0) {
                trackChannels[static_cast<size_t>(channel)] = true;
                open[channel * 128 + static_cast<int>(d1)].push_back({tick, static_cast<int>(d2)});
            } else if (kind == 0x80 || (kind == 0x90 && d2 == 0)) {
                auto it = open.find(channel * 128 + static_cast<int>(d1));
                if (it != open.end() && !it->second.empty()) {
                    const Pending p = it->second.front();
                    it->second.erase(it->second.begin());
                    MidiNote n;
                    n.beat = p.tick / ticksPerBeat;
                    n.length = std::max(0.05, (tick - p.tick) / ticksPerBeat);
                    n.channel = channel;
                    n.note = static_cast<int>(d1);
                    n.velocity = p.velocity;
                    song->notes.push_back(n);
                }
            }
        }
        // Notes never released end a beat after they start.
        for (auto& kv : open)
            for (const Pending& p : kv.second) {
                MidiNote n;
                n.beat = p.tick / ticksPerBeat;
                n.length = 1.0;
                n.channel = kv.first / 128;
                n.note = kv.first % 128;
                n.velocity = p.velocity;
                song->notes.push_back(n);
            }
        for (int c = 0; c < 16; ++c)
            if (trackChannels[static_cast<size_t>(c)]) song->channels[static_cast<size_t>(c)].lyrics += trackLyrics;
        r.pos = end;
    }
    if (song->notes.empty()) {
        if (error) *error = "no notes in the file";
        return nullptr;
    }
    std::sort(song->notes.begin(), song->notes.end(), [](const MidiNote& a, const MidiNote& b) { return a.beat < b.beat || (a.beat == b.beat && a.note > b.note); });
    std::sort(rawTempos.begin(), rawTempos.end(), [](const RawTempo& a, const RawTempo& b) { return a.tick < b.tick; });
    if (rawTempos.empty() || rawTempos.front().tick > 0) song->tempos.push_back({0, 120});
    for (const RawTempo& t : rawTempos) {
        const TempoPoint p{t.tick / ticksPerBeat, std::clamp(t.bpm, 40.0, 240.0)};
        if (!song->tempos.empty() && song->tempos.back().beat == p.beat) song->tempos.back() = p;
        else song->tempos.push_back(p);
    }
    song->name = firstTrackName;
    song->analyse();
    return song;
}

namespace {
// How much a General MIDI program sounds like a lead line. Leads, pipes,
// reeds, voices and brass carry tunes; basses, pads and effects do not.
float programLeadBonus(int program) {
    if (program < 0) return 0;
    if (program <= 7) return 0.5f;      // pianos
    if (program <= 15) return 0.2f;     // chromatic percussion
    if (program <= 23) return 0.3f;     // organs
    if (program <= 25) return -0.3f;    // acoustic guitars
    if (program <= 31) return -0.8f;    // electric guitars: usually rhythm
    if (program <= 39) return -3.0f;    // basses
    if (program <= 47) return 0.5f;     // solo strings
    if (program <= 51) return -0.5f;    // string ensembles
    if (program <= 54) return 1.0f;     // voices
    if (program == 55) return -1.0f;    // orchestra hit
    if (program <= 63) return 0.8f;     // brass
    if (program <= 71) return 1.0f;     // reeds
    if (program <= 79) return 1.2f;     // pipes (the karaoke flute)
    if (program <= 87) return 1.2f;     // synth leads
    if (program <= 95) return -1.5f;    // pads
    if (program <= 103) return -1.5f;   // effects
    if (program <= 111) return 0.3f;    // ethnic
    if (program <= 119) return -1.5f;   // percussive
    return -3.0f;                       // sound effects
}
} // namespace

const char* MidiSong::programName(int program) {
    static const char* const names[128] = {
        "Grand Piano", "Bright Piano", "Electric Grand", "Honky-tonk", "E.Piano 1", "E.Piano 2", "Harpsichord", "Clavinet",
        "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
        "Drawbar Organ", "Perc Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
        "Nylon Guitar", "Steel Guitar", "Jazz Guitar", "Clean Guitar", "Muted Guitar", "Overdrive Gtr", "Distortion Gtr", "Gtr Harmonics",
        "Acoustic Bass", "Finger Bass", "Pick Bass", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
        "Violin", "Viola", "Cello", "Contrabass", "Tremolo Str", "Pizzicato", "Harp", "Timpani",
        "Strings 1", "Strings 2", "Synth Str 1", "Synth Str 2", "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
        "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass", "Synth Brass 1", "Synth Brass 2",
        "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
        "Piccolo", "Flute", "Recorder", "Pan Flute", "Bottle", "Shakuhachi", "Whistle", "Ocarina",
        "Square Lead", "Saw Lead", "Calliope", "Chiff", "Charang", "Voice Lead", "Fifths", "Bass+Lead",
        "New Age Pad", "Warm Pad", "Polysynth", "Choir Pad", "Bowed Pad", "Metallic Pad", "Halo Pad", "Sweep Pad",
        "Rain", "Soundtrack", "Crystal", "Atmosphere", "Brightness", "Goblins", "Echoes", "Sci-fi",
        "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
        "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
        "Fret Noise", "Breath", "Seashore", "Bird", "Telephone", "Helicopter", "Applause", "Gunshot",
    };
    return program >= 0 && program < 128 ? names[program] : "Unknown";
}

void MidiSong::analyse() {
    hasDrums = false;
    lengthBeats = 0;
    for (auto& c : channels) {
        c.notes = 0;
        c.meanPitch = 0;
        c.monophony = 0;
        c.coverage = 0;
        c.score = 0;
    }
    std::array<double, 16> pitchSum{};
    std::array<int, 16> onsets{}, soloOnsets{};
    std::array<std::vector<char>, 16> barsHit{};
    for (const MidiNote& n : notes) lengthBeats = std::max(lengthBeats, n.beat + n.length);
    const int bars = std::max(1, static_cast<int>(std::ceil(lengthBeats / std::max(1, beatsPerBar))));
    for (auto& b : barsHit) b.assign(static_cast<size_t>(bars), 0);
    for (size_t i = 0; i < notes.size(); ++i) {
        const MidiNote& n = notes[i];
        const size_t c = static_cast<size_t>(n.channel);
        if (n.channel == 9) {
            hasDrums = true;
            continue;
        }
        ++channels[c].notes;
        pitchSum[c] += n.note;
        const int bar = std::clamp(static_cast<int>(n.beat / beatsPerBar), 0, bars - 1);
        barsHit[c][static_cast<size_t>(bar)] = 1;
        // An onset is solo when no other note of the channel starts within a 32nd.
        const bool prevSame = i > 0 && notes[i - 1].channel == n.channel && n.beat - notes[i - 1].beat < 0.125;
        const bool nextSame = i + 1 < notes.size() && notes[i + 1].channel == n.channel && notes[i + 1].beat - n.beat < 0.125;
        if (!prevSame) {
            ++onsets[c];
            if (!nextSame) ++soloOnsets[c];
        }
    }
    candidates.clear();
    for (int c = 0; c < 16; ++c) {
        ChannelInfo& info = channels[static_cast<size_t>(c)];
        if (c == 9 || info.notes == 0) continue;
        info.meanPitch = pitchSum[static_cast<size_t>(c)] / info.notes;
        info.monophony = onsets[static_cast<size_t>(c)] ? static_cast<float>(soloOnsets[static_cast<size_t>(c)]) / onsets[static_cast<size_t>(c)] : 0.0f;
        int hit = 0;
        for (char b : barsHit[static_cast<size_t>(c)]) hit += b;
        info.coverage = static_cast<float>(hit) / bars;
        // Lead-likeness: one note at a time, in the singing register, on a
        // melody instrument, present through the song, with lyrics attached.
        const float registerScore = static_cast<float>(std::clamp((info.meanPitch - 48.0) / 24.0, 0.0, 1.0));
        float score = 3.0f * info.monophony + 2.0f * registerScore + programLeadBonus(info.program) + 1.5f * info.coverage;
        if (info.lyrics > 8) score += 2.0f;
        if (info.notes < 16) score -= 3.0f;
        info.score = score;
        candidates.push_back(c);
    }
    std::sort(candidates.begin(), candidates.end(), [&](int a, int b) { return channels[static_cast<size_t>(a)].score > channels[static_cast<size_t>(b)].score; });
    melodyChannel = candidates.empty() ? -1 : candidates.front();
}

double MidiSong::tempoAt(double beat) const {
    double bpm = tempos.empty() ? 120 : tempos.front().bpm;
    for (const TempoPoint& t : tempos) {
        if (t.beat > beat) break;
        bpm = t.bpm;
    }
    return bpm;
}

void MidiSong::trimLeadIn(double firstBeat) {
    if (notes.empty()) return;
    const double first = notes.front().beat;
    const double shift = std::floor(first / beatsPerBar) * beatsPerBar - firstBeat;
    if (shift <= 0) return;
    for (MidiNote& n : notes) n.beat -= shift;
    for (TempoPoint& t : tempos) t.beat = std::max(0.0, t.beat - shift);
    // Keep one tempo point at zero.
    std::sort(tempos.begin(), tempos.end(), [](const TempoPoint& a, const TempoPoint& b) { return a.beat < b.beat; });
    while (tempos.size() > 1 && tempos[1].beat <= 0) tempos.erase(tempos.begin());
    lengthBeats -= shift;
}

bool MidiSong::writeDemo(const std::string& path) {
    // Format 0, 96 ticks per beat, 112 BPM, 4/4: a bass line, a pentatonic
    // tune on channel 1 and drums on channel 10, 16 bars.
    std::vector<uint8_t> track;
    auto varlen = [&](uint32_t v) {
        uint8_t buf[4];
        int n = 0;
        do {
            buf[n++] = static_cast<uint8_t>(v & 0x7f);
            v >>= 7;
        } while (v && n < 4);
        for (int i = n - 1; i >= 0; --i) track.push_back(static_cast<uint8_t>(buf[i] | (i ? 0x80 : 0)));
    };
    struct Ev {
        uint32_t tick;
        uint8_t status, d1, d2;
    };
    std::vector<Ev> events;
    auto note = [&](uint32_t tick, int channel, int pitch, int vel, uint32_t len) {
        events.push_back({tick, static_cast<uint8_t>(0x90 | channel), static_cast<uint8_t>(pitch), static_cast<uint8_t>(vel)});
        events.push_back({tick + len, static_cast<uint8_t>(0x80 | channel), static_cast<uint8_t>(pitch), 0});
    };
    const int tune[32] = {69, 72, 74, 76, 74, 72, 69, 67, 69, 72, 76, 79, 76, 74, 72, 69, 67, 69, 72, 74, 72, 69, 67, 64, 67, 69, 72, 74, 76, 74, 72, 69};
    const int roots[4] = {45, 41, 48, 43};
    for (int bar = 0; bar < 16; ++bar) {
        const uint32_t b0 = static_cast<uint32_t>(bar) * 384;
        for (int beat = 0; beat < 4; ++beat) {
            const uint32_t t = b0 + static_cast<uint32_t>(beat) * 96;
            note(t, 9, 36, 110, 48);                     // kick
            if (beat == 1 || beat == 3) note(t, 9, 38, 100, 48); // snare
            note(t, 9, 42, 70, 24);                       // hats
            note(t + 48, 9, 42, 50, 24);
            note(t, 1, roots[bar % 4], 90, 80);           // bass on channel 2
            if (bar >= 2) {
                const int idx = (bar * 4 + beat) % 32;
                const bool rest = (bar % 4 == 3 && beat == 3);
                if (!rest) note(t, 0, tune[idx], 100, beat % 2 ? 60 : 90); // tune on channel 1
                // Off-beat pickups on beats 2 and 4 of every other bar: the rhythm is the game.
                if (bar % 2 == 1 && (beat == 1 || beat == 3)) note(t + 48, 0, tune[(idx + 1) % 32] - 2, 85, 40);
            }
        }
    }
    std::stable_sort(events.begin(), events.end(), [](const Ev& a, const Ev& b) { return a.tick < b.tick; });
    // Tempo and time signature first.
    const uint8_t tempoMeta[] = {0x00, 0xff, 0x51, 0x03, 0x08, 0x2d, 0x46}; // 535714 us = 112 BPM
    const uint8_t sigMeta[] = {0x00, 0xff, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08};
    const char* name = "Lightfoot Demo";
    track.insert(track.end(), tempoMeta, tempoMeta + sizeof tempoMeta);
    track.insert(track.end(), sigMeta, sigMeta + sizeof sigMeta);
    track.push_back(0x00);
    track.push_back(0xff);
    track.push_back(0x03);
    track.push_back(static_cast<uint8_t>(std::char_traits<char>::length(name)));
    track.insert(track.end(), name, name + std::char_traits<char>::length(name));
    uint32_t last = 0;
    for (const Ev& e : events) {
        varlen(e.tick - last);
        last = e.tick;
        track.push_back(e.status);
        track.push_back(e.d1);
        track.push_back(e.d2);
    }
    const uint8_t endMeta[] = {0x00, 0xff, 0x2f, 0x00};
    track.insert(track.end(), endMeta, endMeta + sizeof endMeta);

    std::vector<uint8_t> out = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k'};
    const uint32_t len = static_cast<uint32_t>(track.size());
    out.push_back(static_cast<uint8_t>(len >> 24));
    out.push_back(static_cast<uint8_t>(len >> 16));
    out.push_back(static_cast<uint8_t>(len >> 8));
    out.push_back(static_cast<uint8_t>(len));
    out.insert(out.end(), track.begin(), track.end());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(f);
}

} // namespace lightfoot
