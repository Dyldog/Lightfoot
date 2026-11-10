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
            if (kind == 0x90 && d2 > 0) {
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

void MidiSong::analyse() {
    hasDrums = false;
    lengthBeats = 0;
    std::array<int, 16> count{};
    std::array<double, 16> pitchSum{};
    for (const MidiNote& n : notes) {
        lengthBeats = std::max(lengthBeats, n.beat + n.length);
        if (n.channel == 9) {
            hasDrums = true;
            continue;
        }
        ++count[static_cast<size_t>(n.channel)];
        pitchSum[static_cast<size_t>(n.channel)] += n.note;
    }
    // The tune: the busiest channel that sits above the bass register, else the busiest.
    melodyChannel = -1;
    int best = 0;
    for (int c = 0; c < 16; ++c) {
        if (c == 9 || count[static_cast<size_t>(c)] < 8) continue;
        const double mean = pitchSum[static_cast<size_t>(c)] / count[static_cast<size_t>(c)];
        const int score = count[static_cast<size_t>(c)] * (mean >= 55 ? 2 : 1);
        if (score > best) {
            best = score;
            melodyChannel = c;
        }
    }
    if (melodyChannel < 0)
        for (int c = 0; c < 16; ++c)
            if (c != 9 && count[static_cast<size_t>(c)] > 0 && (melodyChannel < 0 || count[static_cast<size_t>(c)] > count[static_cast<size_t>(melodyChannel)])) melodyChannel = c;
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
