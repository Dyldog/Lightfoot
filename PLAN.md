# Lightfoot: plan and status

## The idea

An auto-scrolling platformer where the pads are the controller and the
display at once. The LEDs scroll right to left, one lit pad per column in a
random row, and you can only jump by hitting the one that is lit. Expanded:

- The four columns are the next four beats; the row is the platform height.
  That turns the strip into a readable course, not a reaction test.
- Timing is judged against the music, so the game is a rhythm game: PERFECT
  and GOOD windows, combo and multiplier, levels that raise the tempo.
- Twins (two pads, long jump over a gap) and decoys (dim red, do not hit)
  arrive at higher levels, so the strip keeps getting richer.
- The whole panel joins in: TAP TEMPO is the metronome, the meters show
  combo heat and the kick, the track LEDs the level, the right-hand buttons
  chase on a level-up. K1 recolours the world, K2 opens the filter, K3 sets
  the start level, the fader is an assist.
- Music and effects are synthesised on the device and drive the clock, so
  the scroll, the lights and the sound cannot drift apart.

## Decisions

- Standalone autorun app on the same toolkit and build as MashPC; no stock
  code touched.
- Deterministic courses from a seed; screenshots and tests replay them.
- One recording of truth for timing: the audio engine's beat, less output
  latency. Without an output (tests, screenshots) the clock is wall time.
- Scores and settings on the card under `Lightfoot/`.

## Status

Built and tested on the Mac simulator; packaged for the device. Not yet
played on hardware: the first device session should check the output
latency constant (`kOutputLatency` in Game.cpp) by feel and the CPU load at
level 8.

## Next

- A results screen per level (accuracy, best streak) and a daily seed.
- Two-player alternation on pad banks.
- Haptic-free "ghost" of the best run as a second runner.
- Song import: run to a stem recorded in MashPC, tempo-mapped.
