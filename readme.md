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
a UI-side display cache, initialised from the audio-owned eight-pattern bank
before audio starts. Pattern 1 contains the startup pattern; patterns 2–8 start
empty. Edits go through an SPSC command queue rather than accessing live audio state.

- Click a cell to select/toggle it; **x** toggles every selected cell independently.
- **Space** toggles sequencer playback through the command queue.
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

## UI → audio commands

The TUI is the sole producer; the PortAudio callback is the sole consumer. A
1024-command `rigtorp::SPSCQueue` is allocated before audio starts. The UI owns a
storage bank, and only updates it after non-blocking `try_push` succeeds. A full
queue rejects the edit and displays a retry message; it never silently drops an
edit already reflected in the UI. Clear-row is one command, not sixteen edits.

At the start of each audio callback, before rendering any tracks, the sequencer
reads the queue depth once and consumes up to that many commands using `front`/`pop`.
Commands arriving after that observation wait until the next callback. If the
switch-response queue is full, processing stops before the next switch command;
that command and everything after it remain queued until the UI drains responses. Work is
bounded by the 1024-command capacity, not a smaller per-block cap; a full backlog
can therefore take more callback time. Handlers perform bounded work
with no allocation, locking or logging. Commands are explicit `set_step`,
`clear_track`, and `set_tempo` values (tempo is supported by the protocol, not yet
exposed in the UI). Indices and tempo values are validated by the consumer.
The queue outlives the audio client, including exception-path shutdown.

Paste, multi-cell toggle and multi-cell clear use a separate four-entry pattern
payload SPSC queue. Each payload contains the
complete resulting pattern and its pattern ID. The sole producer
first checks both queues have space, publishes the payload, then publishes an
`update_pattern` marker in the regular command queue. Consumer activity can only
free space between those checks and pushes; submission must not be reentrant.

Audio consumes one payload only when it encounters its command marker, preserving
ordering with activation and single-step edits. It copies matching payloads into
the named stored pattern, whether playing, pending or neither. Invalid pattern
IDs are discarded, but every payload is popped. Queue pop
handles memory reuse: no edit-slot bookkeeping or edit-completion queue exists.
UI storage changes only after successful submission. If either queue is full,
the entire edit is rejected before publishing anything.

Both queues allocate their storage before audio starts; push/front/pop do not
allocate. The current fixed-size pattern is 1,568 bytes, and the payload queue
holds four entries. Audio still performs a full-pattern copy for these edits;
revisit its representation/budget as pattern size grows.

After activation only the audio thread accesses live sequencer state. `inc/sequencer_status.hpp` publishes independent atomic runtime telemetry.
The header shows the sequencer's actual PLAYING/STOPPED state, published by the
audio callback after processing commands. The lifecycle owner clears it after
shutdown; the standalone preview stays stopped.
The UI refreshes status and polls switch acknowledgements every 30 ms without
reading live engine fields. Each visible row shows its current step in soft red,
including empty steps. The cursor/selection backgrounds and cursor underline
are preserved. Stopped playback hides the playhead; resuming retains position.

Audio publishes the last step reached per track once after each complete audio
callback. Pattern ID and step share a lock-free 32-bit atomic, so the UI ignores
positions from a different displayed pattern during a switch. Rows may advance
independently; no coherent cross-track snapshot or status queue is required.
Colour-limited terminals fall back to standard red, monochrome to bold/underline.
The standalone preview has no playhead. Display timing is approximate: UI refresh
and device output buffering add latency; audible-output compensation is not yet
implemented.
Quitting stops audio; remaining commands need not be applied. This is an
in-process thread boundary, not an IPC transport. Adding another producer or
consumer would violate the SPSC contract.

## Pattern switching

Audio owns all eight stored patterns. Selecting a pattern sends only its ID;
the UI immediately displays and edits its cached copy after successful enqueue.
There are no shared writable pattern slots and no pattern transfers for selection.
Edits target the named stored pattern even when it is not playing.

While A plays, selecting B queues B; selecting C replaces the pending selection
with C; selecting A cancels the pending switch. Deferred activation follows the
existing track-0 cycle-boundary rule. While transport is stopped it waits for
playback to resume. Transport position and existing sample tails are preserved.
Immediate activation remains available through the command protocol.

The header separates displayed PATTERN from the last acknowledged Audio pattern
and marks the latest request pending until completion. Responses report the actual
playing pattern and switched/cancelled outcome. Each selection has a request ID,
so older responses update the playing indicator without clearing newer pending
requests, including repeated selections of the same pattern. Responses no longer
return slots. See `doc/specs/pattern-storage-and-switching.md`.

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
