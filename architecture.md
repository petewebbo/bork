# Bork architecture direction

This document records the intended direction, not a description of everything
currently implemented. The current prototype uses an SPSC command queue for
pattern edits and atomic playing/per-track playhead telemetry; richer parameters,
machine replacement and additional telemetry are future work.

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

The audio engine owns the complete eight-pattern bank, including patterns that
are not playing. Selection commands carry IDs only; a new ID replaces the pending
selection, and selecting the playing ID cancels it. All pattern edits target the
named stored pattern, not just the current one.

The application/UI thread keeps a separate display/edit cache and prepares edits:

- Patterns and steps.
- Track machine selection.
- Base parameter values.
- Per-step parameter values or locks.
- Sample selection and other configuration.

This thread can allocate, save/load files, support undo and prepare larger edits.
It updates its cache only after successful command submission. The UI is the sole
editor, so it does not need to read mutable audio patterns back to display values
it already knows. Future engine-generated pattern changes would need an explicit
cache-update path.

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
                     edit commands
UI/project model ---------------------> audio engine
       |                                     |
       | save/load/undo                      | render audio
       |                                     |
       <--------- published telemetry --------
```

## Command transport and ownership

The current transport is a fixed-capacity rigtorp/SPSCQueue:

- The TUI/application thread is the sole producer.
- The PortAudio callback is the sole consumer.
- The queue is allocated before audio activation and outlives audio shutdown.
- Switch responses carry a request ID, so older outcomes cannot clear newer UI
  pending state. They report playback changes, not pattern-slot ownership.
- The UI snapshots the initial pattern bank before audio starts.
- The UI uses non-blocking enqueue and updates its model only after success.
- A full queue rejects the edit and provides user feedback rather than silently
  dropping an edit already shown in the UI.
- Commands carry explicit desired values, not relative toggle operations.

At callback entry, the engine currently reads the queue depth once and processes
up to that many commands before rendering any tracks. Later arrivals wait for a
future callback. A full switch-response queue stops consumption before the next
activation command; it remains queued until the UI drains responses. Work is bounded by the fixed queue capacity, currently 1024 messages.
This is a concurrent depth observation, not a synchronised wall-clock snapshot.

As commands become more varied, revisit this processing policy: message count
alone is not an audio-time budget. Callback-side operations must remain small
and bounded. No allocation, blocking, file I/O, logging or heavyweight destruction
belongs in command handlers.

This is an in-process thread boundary, not an IPC transport. A future transport
can reuse the command concepts, but cannot simply share this queue between
processes.

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

The current status interface publishes playing state and per-track positions.
Each position packs pattern ID and step into one lock-free 32-bit atomic and is
published once at the end of an audio callback. The UI reads once per row per
redraw, hides stopped/mismatched-pattern positions, and never reads live transport.

The UI can hold a const reference to this status interface instead of to the
sequencer. Fields may reflect slightly different callback updates; this is
normally acceptable for visual indicators. Intermediate values can be skipped
without building a backlog.

If a coherent status frame is required, use a separate audio-to-UI SPSC queue
carrying small snapshots. Publish without blocking, define a queue-full policy,
and have the UI render the newest snapshot it has received. Publishing less
frequently than every callback may be appropriate.

The UI-to-audio command queue and audio-to-UI status path have different delivery
requirements: edits must not disappear silently, while intermediate telemetry
can often be skipped.

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

### Pattern-loading recommendation

- Parse and validate off the audio thread into a small preallocated transfer-slot
  pool. Only the UI/application thread submits commands, even if a worker loads
  the file.
- Enqueue one `load_pattern` command carrying a slot index. On enqueue success,
  the UI adopts its local copy and must not touch that slot until returned.
- At a block boundary, copy the prepared fixed-size pattern into the engine.
  Return the slot through a dedicated completion SPSC queue sized for every
  transfer slot, so ownership notifications cannot be lost.
- FIFO order defines edits before/after loading. Make transport behaviour
  (preserve position vs restart), voice handling and quantised adoption explicit.
- If patterns grow too large to copy cheaply, swap an active slot/index instead;
  return the old slot for non-audio reuse/destruction. Never retain references
  into a retired pattern.

## Incremental implementation plan

1. Keep the command queue and evolve the UI pattern copy into an application-owned
   project model.
2. Introduce stable machine and parameter IDs with immutable descriptors.
3. Add parameter and step-parameter commands.
4. Publish a small runtime status interface for playheads and meters.
5. Add prepared-state handoff and reclamation when machine replacement or bulk
   editing requires it.

The guiding principle is to keep editable project data, audio execution state
and telemetry separate—not to expose more of the sequencer or make every
stored value atomic.
