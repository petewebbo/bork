# BORK

A very incomplete tui drum machine.

## Build and run

Requires CMake, a C/C++20 toolchain, Git and internet access on the first build.
CMake fetches PortAudio 19.7.0, fmt, AudioFile and rigtorp/SPSCQueue v1.1
(pinned to commit `565a5149d54930463d58cb0f69b978d439555e66`). On Linux, install the development
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
`sequencer._pattern` before audio starts; subsequent edits go through an SPSC
command queue rather than accessing live sequencer state.

- Click a cell or press **x** to toggle it.
- **Space** toggles sequencer playback through the command queue.
- **Arrows** select a cell; **Home/End** jump to the first/last step.
- **C** clears the selected row.
- **q/Q/Escape** quits and stops audio.

Active cells display MIDI note names using `inc/midi_notes.hpp` (128-entry
lookup table with note numbers, sharp-spelled names and equal-tempered frequencies;
A4 = 440 Hz, MIDI 60 = C4). The current demo's `_note = 1` steps display `C#-1`.
The sequencer still uses note zero to mean inactive, so MIDI 0 cannot currently
be played; the lookup table nevertheless includes it. Invalid notes display `????`.

Toggling off preserves the note for re-enabling, as well as velocity and length. A newly enabled step defaults to
velocity 100 if no velocity was set. Clicks in cell gaps are ignored. Resizing
the terminal preserves the pattern.

## UI → audio commands

The TUI is the sole producer; the PortAudio callback is the sole consumer. A
1024-command `rigtorp::SPSCQueue` is allocated before audio starts. The UI owns a
pattern copy, and only updates it after non-blocking `try_push` succeeds. A full
queue rejects the edit and displays a retry message; it never silently drops an
edit already reflected in the UI. Clear-row is one command, not sixteen edits.

At the start of each audio callback, before rendering any tracks, the sequencer
reads the queue depth once and consumes that many commands using `front`/`pop`.
Commands arriving after that observation wait until the next callback. Work is
bounded by the 1024-command capacity, not a smaller per-block cap; a full backlog
can therefore take more callback time. Handlers perform bounded work
with no allocation, locking or logging. Commands are explicit `set_step`,
`clear_track`, and `set_tempo` values (tempo is supported by the protocol, not yet
exposed in the UI). Indices and tempo values are validated by the consumer.
The queue outlives the audio client, including exception-path shutdown.

After activation only the audio thread accesses live sequencer state. `inc/sequencer_status.hpp` publishes independent atomic runtime telemetry.
The header shows the sequencer's actual PLAYING/STOPPED state, published by the
audio callback after processing commands. The lifecycle owner clears it after
shutdown; the standalone preview stays stopped.
The UI refreshes status every 50 ms without reading live engine fields. There is
no pattern/playhead return channel yet; accepted commands may remain pending
until the next callback.
Quitting stops audio; remaining commands need not be applied. This is an
in-process thread boundary, not an IPC transport. Adding another producer or
consumer would violate the SPSC contract.

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
