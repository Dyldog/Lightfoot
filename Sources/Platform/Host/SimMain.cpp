// Mac simulator entry point.
//   lightfoot-sim              interactive window
//   lightfoot-sim --shots DIR  headless screenshots, then exit
#include <cstdio>
#include <cstring>
#include <string>

#include "Audio/MidiFile.hpp"
#include "Game/Game.hpp"
#include "Game/Track.hpp"
#include "Platform/Host/Shots.hpp"
#include "Platform/Host/SimWindow.hpp"

int main(int argc, char** argv) {
    int scale = 3;
    std::string shotsDir;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shots") && i + 1 < argc) shotsDir = argv[++i];
        else if (!std::strcmp(argv[i], "--song") && i + 1 < argc) {
            // Report what the game would make of a MIDI file.
            std::string error;
            auto song = lightfoot::MidiSong::load(argv[++i], &error);
            if (!song) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
            std::printf("%s: %.0f BPM, %d/4, %.0f beats, %zu notes, drums %s\n", song->name.c_str(), song->tempoAt(0), song->beatsPerBar, song->lengthBeats, song->notes.size(), song->hasDrums ? "yes" : "no");
            for (int c : song->candidates) {
                const auto& ch = song->channels[static_cast<size_t>(c)];
                std::printf("  ch %2d %-16s notes %5d mean %5.1f mono %.2f cover %.2f lyrics %3d  score %5.2f%s\n", c + 1, lightfoot::MidiSong::programName(ch.program), ch.notes, ch.meanPitch, ch.monophony, ch.coverage, ch.lyrics, ch.score, c == song->melodyChannel ? "  <- lead" : "");
            }
            song->trimLeadIn(4);
            for (int level : {1, 3, 5}) {
                lightfoot::Track t = lightfoot::Track::fromSong(*song, level);
                int cues = 0;
                std::string rows;
                for (int64_t k = 0; k < t.length(); ++k)
                    if (t.at(k).cue) {
                        ++cues;
                        if (rows.size() < 64) rows += static_cast<char>('0' + t.at(k).row);
                    }
                std::printf("  level %d: %d jumps over %lld steps of %.1f beat; first rows %s\n", level, cues, static_cast<long long>(t.length()), t.stepBeats(), rows.c_str());
            }
            return 0;
        }
        else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) scale = std::atoi(argv[++i]);
        else {
            std::fprintf(stderr, "usage: %s [--scale N] [--shots DIR] [--song FILE.mid]\n", argv[0]);
            return 2;
        }
    }
    if (!shotsDir.empty()) return lightfoot::runShots(shotsDir, 2);
    lightfoot::Game game;
    lightfoot::SimWindow window(game, scale);
    return window.run();
}
