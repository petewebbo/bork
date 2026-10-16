# BORK

A very incomplete tui drum machine.

## Build and run

Requires CMake, a C/C++20 toolchain, Git and internet access on the first build.
CMake fetches PortAudio 19.7.0, fmt and AudioFile. On Linux, install the development
packages for your native PortAudio backend (for example `libasound2-dev` for ALSA).
macOS uses CoreAudio; no JACK server or JACK development package is required.
The UI requires ncurses headers/libraries (macOS Command Line Tools, or
`libncurses-dev` on Debian/Ubuntu).

```sh
./build.sh
./.build/bork
ctest --test-dir .build --output-on-failure
```

The audio engine opens PortAudio's default output device at its default sample
rate, using stereo when available (mono otherwise). It does not open an input
stream. The mono sequencer mix is sent to every output channel.

## Pattern UI

The main application displays a five-track, sixteen-step ncurses grid: Kick,
Snare, Clap, HH and Open HH. It needs a 90 × 15 terminal. The initial grid reads
`sequencer._pattern`; edits write directly back to that same pattern.

- Click a cell or press **Space/x** to toggle it.
- **Arrows** select a cell; **Home/End** jump to the first/last step.
- **C** clears the selected row.
- **q/Q/Escape** quits and stops audio.

Toggling off preserves velocity and length. A newly enabled step defaults to
velocity 100 if no velocity was set. Clicks in cell gaps are ignored. Resizing
the terminal preserves the pattern.

**Prototype limitation:** UI writes and audio reads are deliberately
unsynchronised, as requested. This is a C++ data race, not a thread-safe editing
API; proper communication will replace it later. The UI only snapshots state
on initialisation and tracks its own subsequent edits; it does not poll external
pattern changes or transport/playhead state.

The five sample paths in `src/main.cpp` are absolute; change them to point to
your files. Samples are not resampled, so use samples matching the output sample
rate. `tui/` can also build a standalone preview with an empty pattern; the main
Bork target uses the live pattern. `grid_tui2.cpp` remains an older experiment.

`inc/ezpz_portaudio.hpp` replaces the JACK wrapper and retains the virtual
`process` interface with non-interleaved float buffers. Derived classes must stop
the stream before destroying state used by the callback. Variable callback sizes
are processed in bounded chunks, without allocating in the audio callback.

The wrapper test stubs PortAudio's device API, so it can test lifecycle, callback
buffers and error handling without opening an audio device. Live playback must
be checked separately.
