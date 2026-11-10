# Lightfoot

A rhythm platformer for the Akai MPC Sample, played on the pads.

The course scrolls at the music's tempo. The sixteen pads are the next four
beats: each column is one beat, the left column is now. One pad per column is
lit, at the height of the platform you have to reach. When a lit pad arrives
in the left column you hit it on the beat to jump. Only the lit pad counts.
Two lit pads together are a long jump over a gap; a dim red pad is a decoy.
Three lives, a new level every 32 beats, scores kept on the card.

Drop a Standard MIDI File into `Lightfoot/Songs/` on the card and it becomes
the music and the course: the tune picks the platforms, the file's tempo
drives the scroll, and the run ends with SONG CLEAR. A demo tune is written
there on first run.

Everything is synthesised on the device: the music, the effects, the night
skyline. The panel plays along: the pads are the game, TAP TEMPO is the
metronome, the level meters show combo heat and the kick, the track LEDs the
level, the right-hand buttons chase on a level-up, K1 repaints the night, K2
opens the filter, K3 picks the start level, the fader is an assist.

Runs from the SD card like any autorun app. Nothing on the device is
modified. Firmware 1.3.0.12.

- [MANUAL.md](MANUAL.md) is how to play; [Lightfoot-Manual.pdf](Lightfoot-Manual.pdf) is generated from it and ships in the package.
- [PLAN.md](PLAN.md) has the design and what is next.

## Build

Requirements: `zig` (cross-compiler), `python3`, SDL2 (`brew install sdl2`)
for the Mac simulator, `pandoc` and `typst` (`brew install pandoc typst`) for the PDF.

```sh
git submodule update --init --recursive
make sim          # Mac simulator: build/sim/lightfoot-sim (make run starts it)
make shots        # headless screenshots of every screen into build/shots/
make test         # unit tests + shots
make device       # static aarch64 binary: build/device/lightfoot
make manual       # MANUAL.md -> Lightfoot-Manual.pdf
make package      # SD autorun package: build/pkg/lightfoot.zip
make hooks        # git hook: rebuilds the PDF whenever MANUAL.md is committed
```

Simulator keys: Space PLAY, S STOP, Up/Down or the mouse wheel for the data
wheel, Return for the wheel press, F1–F3 for B1–B3, Left Shift for SHIFT, and
the pads on Z X C V / A S D F / Q W E R / 1 2 3 4 (bottom row to top row).

## Install on the device

1. Unzip `lightfoot.zip`. Copy the `autorun` folder to the root of an exFAT
   microSD card, or add the zip to mpc-boot-thingy with `setup.sh --add`.
2. Power the MPC off, insert the card, power on.
3. To return to stock: Quit to stock on the title screen, or SHIFT + STOP
   twice, or power on without the card.

## Layout

```
Sources/
  Game/       rules, judging, scoring, course generation, drawing, scores
  Audio/      the synth: music, effects, and the beat clock
  Shared/     canvas, input frame, JSON, paths
  Platform/   Host (SDL2 simulator, screenshots) and Device (DRM/panel via the toolkit, tinyalsa)
Tests/        unit tests (make test)
deps/         submodules: mpc-ui-toolkit, tinyalsa
tools/        package.sh, build-manual.py, check-manual.py, hooks/
```

Not affiliated with inMusic Brands or Akai Professional. No Akai code or
firmware is in this repository.
