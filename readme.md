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

On Linux/non-Apple Unix, `./build.sh USE_JACK` additionally compiles PortAudio's
JACK backend. Install `pkg-config` and JACK development files first (for example
`libjack-jackd2-dev` on Debian/Ubuntu). Direct CMake builds use `-DUSE_JACK=ON`.
Running `./build.sh` without the argument explicitly disables JACK again.
The pinned PortAudio CMake build does not support this option on macOS/Windows.

All six test suites run in one executable, `./.build/bork_tests`, also registered
as a single CTest test. It prints each suite name before running its assertions.

The audio engine opens PortAudio's default output device at its default sample
rate, using stereo when available (mono otherwise). It does not open an input
stream. The mono sequencer mix is sent to every output channel.

## Pattern UI

The main application displays a five-track, sixteen-step ncurses grid: Kick,
Snare, Clap, HH and Open HH. It needs a 90 × 15 terminal. The grid loads one
editing copy from the dedicated store; all eight stored patterns start empty.
Edits go through the store protocol rather than accessing live audio state.

- Click a cell to select/toggle it; **x** toggles every selected cell independently.
- **Space** toggles sequencer playback through store start/stop requests.
- **Arrows** move; **Shift+arrows** extend a rectangular selection. Normal
  movement/click collapses it. **Ctrl+Shift+arrows** extend selection straight to
  the grid edge in that direction, preserving the anchor. **Home/End** jump to
  the first/last step.
- **c** clears the selection, or the current row when only one cell is selected.
- **C**, then **p/s** copies the whole pattern/selection; **Escape** cancels.
- **V** pastes. A selection paste starts at the cursor and clips at the visible
  grid edges. A pattern paste replaces the entire displayed pattern, including
  hidden tracks and metadata, independent of cursor position.
- **P**, then **1–8** selects a pattern. The permanent top status bar prompts
  for the number; **Escape** cancels. Other keys do not edit or control playback
  while waiting for a number. The bar also shows errors/pending feedback and
  remains visible when empty.
- **q/Q/Escape** quits and stops audio.

An active cell under the cursor has a dark-grey background. Multi-cell selection
uses a light-grey background with an underlined cursor (with basic-colour or
monochrome fallbacks). Selection is anchor-to-cursor, inclusive, and persists
until normal movement, clicking or a pattern change.

Shift-arrows support standard xterm `CSI 1;2 A/B/C/D` sequences and native ncurses
shifted-arrow codes. Ctrl+Shift-arrows use xterm `CSI 1;6 A/B/C/D` sequences. A terminal must actually send shifted keys distinctly;
terminals configured to intercept these shortcuts may need configuration.
The standalone preview was exercised under a pseudo-terminal using xterm input.

The clipboard is internal to Bork and survives pattern changes. It owns snapshots
of all step fields, including empty cells; overlap and repeated pastes are safe.
Copy prompts use the top status bar and block normal controls until completed
or cancelled. Copy/selection movement never sends audio commands.

Active cells display MIDI note names using `inc/midi_notes.hpp` (128-entry
lookup table with note numbers, sharp-spelled names and equal-tempered frequencies;
A4 = 440 Hz, MIDI 60 = C4). Newly enabled empty cells default to C4.
The sequencer still uses note zero to mean inactive, so MIDI 0 cannot currently
be played; the lookup table nevertheless includes it. Invalid notes display `????`.

Toggling off preserves the note for re-enabling, as well as velocity and length. A newly enabled step defaults to
velocity 100 if no velocity was set. Clicks in cell gaps are ignored. Resizing
the terminal preserves the pattern.

## UI, store and audio

A dedicated pattern-store thread owns eight editable patterns and remembered-note
metadata. The UI holds one editing copy plus its deliberate clipboard. Edits are
optimistic only after successful request admission; queue-full leaves the display
unchanged. Selecting another pattern loads its editing copy independently of audio.
Editing/copying is disabled while that copy is loading or recovering from rejection.

UI requests and reliable results have capacity 64 with 64 outstanding-request
credits. Editing snapshots use a separate capacity-one channel. Rejected edits
recover through a FIFO authoritative-resync barrier, preserving later valid edits.
Local edits affect only their destination region; whole-pattern paste intentionally
includes hidden tracks and track metadata.

The store owns five shared playback snapshot buffers. Audio borrows only current
and optional pending references. A capacity-three snapshot queue carries immutable
references with selection flags; a separate capacity-64 event queue carries explicit
start/stop and tempo (tempo is not yet exposed in the UI). A capacity-five release
queue returns buffers to the store. All pattern copying occurs on the store thread.
Published buffers cannot be overwritten until explicitly returned.

If buffers are busy, edits accumulate in originals. The latest unsent selection
takes priority over content refreshes when publication can resume. Audio never
waits for UI/store progress, copies a whole pattern, locks or allocates.

Device callbacks are split into chunks of at most approximately 5 ms, bounded by
scratch capacity. Before each chunk, audio drains the observed event batch, then
the observed snapshot batch. These queues do not have a shared ordering. Updates
arriving after a queue check wait for a later chunk.

Audio publishes playing/current/pending state and packed pattern-ID/step telemetry
after each chunk. Store forwards it; UI polls/redraws at 25 ms intervals without
reading mutable engine fields. Soft-red per-track playheads preserve cursor and
selection styling and hide while stopped, loading, or showing another pattern.
Rows need not form a coherent snapshot. Display buffering adds latency; there is
no audible-output compensation. The non-rendering preview has no playhead.

## Pattern switching

While A plays, selecting B installs B's attached snapshot as pending; selecting C
replaces it, and selecting A cancels it while refreshing A's contents. Deferred
activation preserves the existing track-0 boundary rule. While stopped it waits
for playback. Immediate selection remains supported by the store protocol.

Updates replace matching current/pending references without creating another
selection. A newer edit arriving mid-chunk does not delay the musical switch:
audio activates its available pending snapshot and adopts the update at a later
queue check. Transport position and existing sample tails are preserved; stop
pauses sequencing while tails continue to render.

The header separates the requested editing pattern from actual audio state.
Store acceptance is not an audio switch acknowledgement. Shutdown joins callbacks
before reclaiming snapshot storage. See
`doc/specs/2026-10-19-new-architecture.md` for the protocol and ownership contract.

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
