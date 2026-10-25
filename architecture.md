# Bork architecture direction

The current implementation separates UI editing, a dedicated authoritative pattern
store, and audio playback. The detailed ownership and queue contract lives in
`doc/specs/2026-10-19-new-architecture.md`. Parameter and machine examples below
describe future direction, not additional implemented features.

## Source layout

| Directory | Responsibility |
|---|---|
| `src/app/` | Application and non-rendering preview entrypoints, wiring and shutdown |
| `src/model/` | Pattern values, edit operations, constants and MIDI note metadata |
| `src/store/` | Authoritative bank, worker and playback snapshot preparation |
| `src/audio/` | Sequencing, transport, machines and PortAudio wrapper |
| `src/messages/` | Cross-thread messages, queues, borrowed references and status |
| `src/ui/` | Editing view model, selection, terminal input and rendering |
| `tests/` | Flat regression suites and their single executable entrypoint |

Headers live beside their related implementation; there is no public `inc/` API.
Project includes are explicit paths from the `src/` include root, for example
`"model/pattern.hpp"`. Keep existing header-only code header-only. Top-level
CMake builds `bork`, `bork_preview` and, with testing enabled, `bork_tests`;
directories do not introduce separate libraries or nested build projects.

## Terminal UI

The FTXUI adapter has four concrete parts, shared by the main app and preview:

| File | Responsibility |
|---|---|
| `editor_state.hpp` | Terminal-independent editing copy, selection, clipboard, store requests/recovery and one input-mode enum |
| `pattern_grid.cpp` | Custom grid component, cell styling, navigation and reflected cell hitboxes |
| `editor_component.cpp` | Status/header/help composition, responsive fallback, modal-first routing and normal-mode quit |
| `run_tui.cpp` | FTXUI terminal lifecycle and main-thread 25 ms store-poll/redraw loop |

FTXUI types stay out of editor state, store and audio. Components reference the
existing editor state; they do not own another pattern copy. Rendering only reads
state. Input handlers invoke editor operations, and only the runner polls store
responses. One `input_mode` prevents copy/pattern/edit prompts from overlapping;
active modes consume keys/mouse events before normal controls or quit.

Grid cells are DOM elements, not separate button components. Reflected boxes are
the single source of truth for mouse targeting. Resize fallback suppresses grid
editing below 90×15. Colors adapt to FTXUI terminal capabilities; playheads change
foreground without replacing selection/cursor styling.

The runner uses FTXUI's non-blocking `Loop::RunOnce()` and posts a refresh event
at each poll. It does not introduce a refresh thread, mutate state from FTXUI's
input threads, or require store/audio to call UI code. Terminal cleanup is RAII.

## Expected scope

- 16 tracks.
- 20+ parameters per machine.
- Different machines per track: sampler, synth, etc., each with its own parameters.
- Parameters can have stored values or overrides per sequencer step.

For example, 16 tracks × 20 parameters × 16 steps is 5,120 values: approximately
20 KiB of floats before metadata. Storage is manageable. The main challenges
are ownership, coherent edits and bounded real-time work.

## Separate three kinds of state

### Editable state and the stored pattern bank

The store owns the eight editable originals, per-pattern remembered notes, and
five shared playback snapshot allocations. The UI owns only its current editing
copy, associated metadata and deliberate clipboard. It updates optimistically
after successful store-request admission and recovers rejected edits through a
FIFO resynchronisation barrier.

Audio borrows only current and optional pending immutable snapshots. Selection
includes the initial snapshot; content updates replace matching references.
The store applies all edits and copies originals into free snapshots off audio.
Only explicit audio release permits reuse. A new selection replaces pending;
selecting the actual playing pattern cancels pending and refreshes current.

### Audio runtime state

The audio thread exclusively owns mutable execution state:

- Sampler playback cursors.
- Synth voices and envelopes.
- Filter history.
- Effective parameter values during playback.
- Transport scheduling.

The UI must not read these ordinary mutable fields directly. A reference or
const reference does not make concurrent reads safe: unsynchronised reads and
writes are a C++ data race.

### Published telemetry

Expose a deliberately small runtime view:

- Play/stop state.
- Current step per track.
- Meters.
- Sample position, if needed.
- Possibly effective parameter values for the selected track.

Do not mirror the whole project through atomics or a status structure.

```text
UI editing copy <--> dedicated pattern store --> audio current/pending references
                           ^                         |
                           +---- snapshot releases --+
UI status <--------- store-forwarded atomic telemetry
```

## Command transport and ownership

All queues use the installed rigtorp/SPSCQueue dependency with one producer and
one consumer. Their storage outlives callbacks:

- UI/store: requests 64, reliable results 64, editing snapshots 1.
- Store/audio: events 64, snapshot references 3.
- Audio/store: buffer releases 5.

UI admission reserves one of 64 terminal-result credits. Audio needs no reliable
per-command result or snapshot revision. Latest-value telemetry reports actual
playing/pending state independently of store acceptance.

Audio checks events then snapshots before each approximately 5 ms processing chunk.
Each drain captures queue depth once. Ordering is FIFO within each queue, not
between event and snapshot queues. Callback handlers never copy full patterns,
allocate, block, lock, log or destroy heavyweight ownership.

Store retains edits in originals under publication backpressure and prioritises
the latest unsent selection. Snapshot references stay immutable until returned;
queue emptiness never grants permission to reuse them.

## Parameter IDs and machine descriptors

Use stable parameter IDs rather than a separate command type for each control.
Illustrative messages:

```cpp
struct set_parameter {
    track_id track;
    parameter_id parameter;
    float value;
};

struct set_step_parameter {
    track_id track;
    step_index step;
    parameter_id parameter;
    float value;
};
```

Each machine describes its parameters through immutable metadata:

```cpp
struct parameter_descriptor {
    parameter_id id;
    std::string_view name;
    float minimum;
    float maximum;
    float default_value;
    // Units, scaling, discrete choices, etc.
};
```

The UI uses descriptors to construct controls. The engine maps IDs to internal
parameter slots. Immutable descriptors may be shared safely without passing them
through the audio queue, provided their lifetime covers all readers.

Floats are suitable for ordinary continuous controls. Sample selection, enums,
routing and other non-continuous settings may require different representations.
The types above illustrate the interface direction, not a final schema.

## Base, step and effective values

Distinguish:

- **Base value:** the track's normal setting.
- **Step override:** an optional value associated with a sequencer step.
- **Effective value:** what the engine is currently using.

For example, a track may have an 8 kHz base cutoff and a 500 Hz override on step 2.
We must explicitly decide whether an override:

- Applies only to its step.
- Persists until another override.
- Is captured only when triggering a voice.

These semantics belong in the engine, not the UI.

A fixed parameter array plus a presence bitset per step is a reasonable initial
audio representation:

```cpp
struct step_parameters {
    std::bitset<MAX_MACHINE_PARAMETERS> present;
    std::array<float, MAX_MACHINE_PARAMETERS> values;
};
```

This avoids allocating while scheduling steps. A sparse project-file
representation can be introduced independently if useful.

## Telemetry transport

For independent indicators such as playheads and meters, use a small status
interface with lock-free atomics. Relaxed loads/stores are sufficient when each
field is independent telemetry, not a publication signal for other non-atomic
data. Verify that the chosen atomic types are lock-free on supported targets.

Status publishes playing state, current/pending pattern IDs and per-track positions
after each rendered chunk. Each position packs pattern ID and step into one
lock-free 32-bit atomic. Store forwards status; UI refreshes every 25 ms and hides
stopped/mismatched/loading positions without reading live transport.

Fields may reflect slightly different updates; intermediate values may be skipped.
Telemetry is not an ownership signal or a reliable result queue.

## Machine replacement and resource lifetime

Replacing a sampler with a synth may require allocation, sample loading, voice
construction and different parameter layouts. Do not perform that work inside
the audio callback.

Prepare the replacement on a non-audio thread, then hand the prepared object to
the engine for adoption at a block boundary. Retire old objects back to a
non-audio thread for destruction. Ownership and reclamation must be explicit;
a pointer handoff alone does not solve lifetime management.

Machine replacement also affects queued parameter edits. Use transactional
ordering or a machine-instance/version identifier so an edit intended for the
old machine cannot accidentally modify the new one.

## Bulk edits and coherent changes

Individual commands suit mouse clicks and ordinary parameter changes. Larger
operations should become coherent batches or prepared-state replacements:

- Loading a project.
- Pasting a pattern.
- Randomising many parameter locks.
- Replacing a machine and its configuration.

Avoid expressing these as thousands of unrelated edits spread unpredictably
across callbacks. Batch adoption must still have bounded callback-side work;
calling a large operation one command does not make it real-time safe.

### Pattern loading and bulk editing

Parsing/loading belongs off audio. Store validates and applies complete UI
operations to originals, then prepares immutable playback snapshots. Audio adopts
references rather than copying data. Local region operations must not overwrite
unrelated optimistic edits; whole-pattern replacement is intentional and explicit.

Future machine/parameter work should reuse this separation rather than introduce
another mutable bank or callback-side resource preparation.

The guiding principle is to keep editable project data, audio execution state
and telemetry separate—not to expose more of the sequencer or make every
stored value atomic.
