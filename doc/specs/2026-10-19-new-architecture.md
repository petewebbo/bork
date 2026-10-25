# UI, pattern store and audio

Status: implemented 2026-10-21. Rewritten 2026-10-21.
This document replaces the earlier design in this file.

## Purpose

Move authoritative pattern editing out of audio and separate it from the UI.
Keep playback handoff small, bounded and easy to understand.

Expected operation: UI work is serviced at intervals of at most 25 ms; audio
checks its queues roughly every 5 ms. These are operating targets, not ownership
or memory-safety guarantees. Queue-full handling still exists.

## Ownership

| Component | Owns |
|---|---|
| UI/main thread | One editing copy and its metadata, cursor, selection, clipboard |
| Dedicated store thread | Eight editable patterns, remembered-note metadata, five playback snapshot buffers |
| Audio | Transport, machines, one current snapshot reference, optional pending snapshot reference |

Store originals and playback snapshots are separate. Audio never reads an editable
original. UI never reads mutable store/audio fields directly. Audio borrows
immutable snapshots; the store owns all backing allocations.

Use whole-pattern copies on the store thread. No chunking, copy-on-write trees,
networking, persistence or source-layout reorganisation.

## Store → audio: two queues

Use the existing SPSC queue dependency. Store is sole producer; audio sole consumer.

| Queue | Capacity | Contents |
|---|---:|---|
| Events | 64 | Start, stop, tempo |
| Snapshots | 3 | Snapshot reference and selection flags |

A snapshot reference contains buffer identity, pattern ID and a `const pattern*`.
It is not an owning smart pointer or inline pattern value.

Conceptually:

```cpp
struct snapshot_message {
    snapshot_ref snapshot;
    bool select;
    bool deferred;
};
```

- `select == true`: select this pattern using the attached contents.
- `select == false`: update contents of a matching current/pending pattern.
- `deferred` matters only for selection.

Selection and initial contents are one message. No separate switch message,
snapshot revisions, minimum-version gates or audio control-result credits.
Snapshot messages retain FIFO order. Events are FIFO within their own queue;
there is deliberately no global ordering between events and snapshots.

Audio processes events first, then snapshots, before each audio processing chunk.
Capture each queue's depth once per drain, keeping work bounded. Process all
captured messages, not just one message per chunk.

Start/stop are explicit desired states rather than toggle events. UI can still
present a toggle control. Validate tempo using the existing finite/range checks.
Stop pauses sequencing; it does not discard pending selection or cut sample tails.

## Snapshot pool and release

Allocate exactly five stable-address playback buffers before starting audio:

- Up to one current reference.
- Up to one pending reference.
- Up to three snapshot references queued for audio.

These are maximum roles, not permanent assignments. A returned buffer may still
be awaiting store reclamation, so queue space alone does not imply a free buffer.

Publication:

1. Apply an accepted edit to the authoritative original.
2. Obtain a free playback buffer and copy the desired original into it.
3. Enqueue its reference and flags.
4. Once enqueue succeeds, do not modify that buffer until explicitly returned.

Failed enqueue transfers nothing. Store may refresh/reuse that unpublished buffer.

Use a separate audio → store SPSC release queue of capacity **5**. Audio returns
exact buffer identities after ceasing all use of them. Store consumes a release
before reusing that allocation. At most one release per allocation can be
outstanding, so five entries cover the entire pool without dropping releases.

Audio never allocates, locks, waits, logs, copies a whole pattern or destroys
pattern ownership. Machines must not retain references after release; sampler
triggers already copy the step values they need.

## Audio snapshot handling

| Incoming message | Action |
|---|---|
| Deferred select of another pattern | Release old pending if present; install incoming as pending |
| Immediate select of another pattern | Release old pending and current; install incoming as current |
| Select actual current pattern | Release/clear pending; replace current with incoming contents and return old current |
| Update matching current | Replace current; return old current |
| Update matching pending | Replace pending; return old pending |
| Update matching neither | Return incoming buffer without installing it |

An update does not create a selection or change its timing. Audio holds no bank
or references for the other stored patterns.

At the existing deferred-switch boundary, release old current, move pending to
current and clear pending. Preserve the existing track-zero boundary condition,
transport positions, track scheduling and sample tails. Deferred switches wait
while sequencing is stopped. Do not combine this change with a scheduling fix.

### Example

A is playing. Store edits A and queues its snapshot, then receives selection B
and queues B with `select = true`.

At the next queue check audio:

1. Adopts edited A and returns old A.
2. Installs B as pending.
3. Processes audio.
4. If the boundary occurs, makes B current and returns edited A.

If a B edit arrives after a chunk's queue check, audio may switch using older B
contents. Adopt the update at the next queue check if available. This short stale
interval is accepted; do not postpone the switch by a pattern cycle. Delivery by
exactly the next chunk cannot be guaranteed if store work or buffers are delayed.

## Audio chunking and telemetry

Process device callbacks in chunks no larger than approximately 5 ms at the active
sample rate, and check queues before every chunk. Use at least one frame per chunk.
The chunk size must also fit existing scratch buffers. A larger device callback
therefore does not postpone all command processing until its end.

Use existing track rendering/summing within each chunk. Do not add queue checks
inside individual track processing or otherwise rewrite event scheduling.

Audio publishes non-blocking latest-value status after each completed chunk:
playing state, actual current ID, optional pending ID and per-track playheads.
This is lossy display telemetry, not a reliable outcome or ownership channel.
Keep pattern ID and step packed together as today; independent tracks need not
form a coherent snapshot. Never use telemetry to decide a buffer is reusable.

Store polls/forwards telemetry while playback or pending work requires refresh;
it must not sleep indefinitely during steady playback. UI redraw/polling continues
at its normal cadence. No audio acknowledgements are required for every event or
snapshot; display actual playing/pending state from latest status.

## Store work and backpressure

Store applies UI requests in FIFO order. For each pass, drain releases, process the
currently queued UI batch, then attempt outstanding audio output. Several edits
already waiting may become one latest snapshot; no batching-delay timer.

Pool/queue exhaustion must not block edits to originals or serving editing copies.
Keep unpublished contents in the originals, not an unbounded snapshot backlog.
Never allocate a sixth playback buffer. A successfully queued message cannot be
retracted or overwritten.

Keep only the latest unsent playback selection; a newer selection supersedes an
older unsent one. Preserve edits to both originals. Pending publication is work
to retry, not a reason to spin. Do not drop an accepted start/stop/tempo request
because the event queue is temporarily full; finite UI request capacity supplies
backpressure rather than an unbounded staging queue.

Publish dirty updates for patterns with outstanding references that may still be
current, pending or in flight. Store tracks each occupied buffer's pattern ID
until its release is consumed; latest telemetry is not a lifetime signal.
This conservative set can include a superseded target until audio returns it.
Audio safely returns unmatched updates. Do not publish all eight originals on
every edit; future selections always copy the latest original.

### Publication priority

When a buffer and snapshot queue slot become available, publish the latest unsent
selection before dirty content updates. The next musical boundary may be close;
getting the selection to audio takes priority over refreshing existing contents.
Prepare its snapshot from the latest authoritative original, including edits
already processed by the store. This does not reorder messages already queued.

After submitting the selection, use remaining capacity for relevant dirty updates.
An unmatched update returned by audio requires no immediate resend: its original
remains authoritative, and a future selection copies its latest contents.

## Store wake-up

UI enqueue updates a mutex-protected wake predicate and notifies the store's
condition variable. Avoid lost wake-ups. Audio only pushes releases and publishes
atomics; it never takes that mutex or notifies the condition variable.

Use timed waits initially 1 ms while waiting for audio progress or output capacity.
Include telemetry refresh during playback. Wait indefinitely only when idle with
no refresh/retry needed. No separate timer thread and no busy-spinning.

## UI ↔ store

This boundary retains the agreed editing behaviour. It is independent of playback
buffer availability and musical boundaries.

| Channel | Capacity |
|---|---:|
| UI requests | 64 |
| Reliable store results | 64 |
| Editing snapshots | 1 |

Allow at most 64 UI requests awaiting a terminal result. Successful admission
reserves a result credit; failed admission changes neither UI data nor credits.
Return the credit when UI consumes the terminal result, including ignored stale
results. Each request gets exactly one result for store acceptance/rejection or
supersession; selection completion here means store handling, not audio activation.
Audio playback state is shown separately through telemetry.

Editing snapshots are separate non-real-time copies, not playback-pool buffers.
While their queue is full, retain only the latest unsent snapshot requirement.
Never overwrite a queued response. Tag responses with editing-selection and,
where applicable, resync request identities. Retry publication even if no further
UI request arrives. A terminal result alone does not finish loading a snapshot.

### Editing selection

Request another pattern's editing copy immediately, without waiting for audio.
Track requested selection separately from the loaded copy's identity. While
loading, disable edits and copy actions on the old displayed contents; further
pattern selections remain available. Install only the latest matching editing
snapshot. Keep the deliberate clipboard across selection changes.

### Optimistic edits and validation

After successful request enqueue, apply the desired edit to the UI copy. Queue
full or exhausted result credits leaves it unchanged and displays retry feedback.

Store validates pattern/track/step/region bounds and applies each operation
atomically, including metadata. Local operations carry explicit desired values
and affect only their destination cells; do not send an entire optimistic pattern
for an unrelated rectangle edit. Whole-pattern paste intentionally replaces the
whole pattern, including hidden tracks and track lengths. Preserve existing
selection clipping and clipboard behaviour.

Remembered notes stay in the store and accompany editing/recovery snapshots:
clear/toggle-off preserve remembered pitches; toggle-on restores them. Paste
updates metadata only in its destination, using default note 60 for pasted empty
cells. Clipboard copies step/pattern contents, not hidden remembered-note metadata.
UI holds metadata only for its current editing copy.

### Rejected edit recovery

Use the agreed FIFO resync barrier, not an edit-replay journal:

1. Report rejection with editing-session and request identity.
2. UI pauses edits to the affected current editing session.
3. Queue one resync barrier after already-submitted edits; retry if capacity or
   credits are unavailable.
4. Store processes preceding valid edits and returns authoritative pattern plus
   metadata at the barrier.
5. UI installs only a matching current-session recovery response, then resumes.

Further selections remain available. Old-session responses cannot replace a new
copy, including a later visit to the same pattern. Delayed rejections already
covered by the barrier must not start repeated recovery. All edit entrypoints,
including mouse actions, obey loading/recovery state. Recovery does not wait for
an audio callback.

## Startup, shutdown and migration

Startup: initialise originals/metadata, allocate five snapshots, copy the initial
pattern into current, leave pending empty and four buffers free. Establish channel
lifetimes before starting store/audio; obtain the initial UI copy from store.

Shutdown: stop admitting UI work, wake store, stop audio and join all callbacks,
then stop/join store and destroy channels/storage. Once audio is stopped, reclaim
its current/pending and queued references without requiring another callback.
Do not let shutdown retries wait forever for a stopped consumer. Exception paths
must preserve this ordering too.

The UI's `EditorState` holds one editing copy. Edit application and metadata
belong to the store, and sequencer borrows current/pending references instead of
holding a bank. Old command markers, callback-side bulk copies and switch
acknowledgement backpressure are removed; no parallel old paths remain.
Preview must use the same store/UI protocol, with a non-rendering audio consumer
that returns snapshots and publishes status. Update existing fixtures and older
docs that describe the superseded ownership/delivery model during implementation.

## Verification for implementation

- Run both executables and existing regressions; exercise actual TUI selection,
  editing, paste, play/stop and playheads.
- Assert five-buffer accounting through queued/current/pending/released/free roles;
  no buffer reuse before release and no copies/allocations in audio.
- Exercise the A-edit/B-select example, pending replacement/cancellation,
  immediate selection, current/pending updates and unmatched update return.
- Fill snapshot queue/pool: originals remain editable, latest unsent selection
  survives, accepted events are retained, and publication resumes after release.
- Exercise large device callbacks: queue checks happen between bounded chunks.
- Preserve existing transport timing tests; separately measure chunking effects
  on track rendering before claiming equivalent playback behaviour.
- Exercise stale editing snapshots, rejection plus unrelated valid edit, resync
  with full channels, metadata restoration and blocked UI delivery.
- Verify telemetry continues during steady playback and shutdown works with full
  queues, a sleeping store and outstanding snapshots.

No live-audio performance guarantee follows from UI/audio cadence alone. Measure
latency and callback work, but do not build a generalized messaging framework.
