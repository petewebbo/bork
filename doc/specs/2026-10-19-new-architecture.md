# New architecture: UI, pattern store and audio

Date: 2026-10-19
Status: planned; not implemented. Agreed design below; open decisions are listed
at the end and must be resolved before the affected implementation work.

## Goal and agreed choices

Separate editing, authoritative pattern storage and real-time playback.

- UI holds only its current editing pattern, not a full bank cache.
- A dedicated non-real-time pattern-store thread owns the complete bank.
- Audio reads immutable full-pattern snapshots through borrowed references.
- Use three preallocated pattern buffers per stored pattern.
- Use whole-pattern copies initially, all outside the audio callback.
- Keep a length-one pattern-publication queue separate from the larger control
  queue. Both run from store to audio.
- Make dependencies across those two queues explicit using pattern revisions.
- UI edits remain optimistic after successful request enqueue.
- Store wakes for UI work through a condition variable and checks audio returns
  through timed waits. Audio never signals that condition variable.

Preserve playback behaviour: a new selection replaces the pending pattern;
selecting the playing pattern cancels it. Audio decides when a deferred switch
occurs. No chunking, persistent trees or per-track sharing are included.

## Components

```text
UI — main thread
  One editing copy, cursor, selection, clipboard
       |
       | requests / editing snapshots and results
       v
Pattern store — dedicated non-real-time thread
  Authoritative bank, edits, snapshots, buffer ownership
       |
       +-- pattern-publication queue, capacity 1 --> Audio
       +-- separate bounded control queue --------> Audio
       ^
       | buffer releases and control outcomes
       |
Audio — callback thread
  Read-only pattern references, playing/pending IDs, transport, machines
```

The pattern store is not part of the GUI. A future remote GUI can communicate
with it through an adapter while the store and audio remain local.

## Ownership

| Data | Owner |
|---|---|
| Authoritative editable pattern bank | Pattern store |
| All three buffers belonging to each pattern | Pattern store |
| Queued and audio-referenced snapshots | Store retains storage; audio borrows immutable references |
| Current editing copy | UI |
| Clipboard snapshot | UI |
| Playing and pending IDs and adopted revisions | Audio |
| Transport, voices and playback cursors | Audio |
| Playheads and runtime status | Audio produces; store forwards to UI |

No component reads another component's mutable ordinary fields.

Per-pattern remembered-note metadata moves into the store. UI editing snapshots
include the current pattern's metadata only. Preserve note restoration after
editing, clearing and revisiting a pattern without retaining an eight-pattern
metadata cache in the UI. This metadata need not be part of playback snapshots.

The clipboard remains a deliberate user-created snapshot, not a hidden bank
cache. It is separate from the three playback buffers per pattern.

## Full-pattern buffers and revisions

Allocate three stable-address full-pattern buffers for every stored pattern
before audio starts. With eight patterns, that is 24 buffers: 37,632 bytes at the
current 1,568-byte pattern size, excluding metadata and UI/clipboard copies.

The store's writable buffer is its authoritative pattern contents. Do not add a
fourth full-pattern bank merely to hold another mutable copy.

Track these distinct identities:

- Pattern ID: which stored pattern the contents belong to.
- Buffer ID: which of that pattern's three allocations contains the snapshot.
- Revision: monotonically increasing logical edit revision for that pattern.
- Request ID: identifies a UI operation and its responses.

An immutable snapshot records the revision of the complete state it contains.
All constituent edits up to that revision are included. A revision is not the
buffer ID: allocations are reused across many revisions.

Audio keeps a borrowed `const pattern*` and adopted revision for every stored ID.
Adopting new contents updates that reference; selecting a playback pattern changes
an ID. Updating pattern B does not select B. Selecting B does not copy B.

### Buffer roles

At a representative point in time:

```text
A: audio-referenced, immutable
B: published to the length-one queue, immutable
C: authoritative working buffer; store applies further edits here
```

Buffer letters are illustrative. Their roles rotate; no allocation is permanently
an audio buffer or an editing buffer.

A publication contains a pointer and small identifiers, not inline pattern data.
Audio does not copy pattern contents when consuming it.

### Adoption and reuse

1. At callback entry, audio takes B from the publication queue.
2. It replaces the reference to A with B and records B's revision.
3. After it can no longer access A, it returns A's identity to the store.
4. The store receives that release. An empty publication queue alone does not
   grant permission to overwrite A.
5. If C is dirty, the store copies C into returned A on its own thread. A is now
   ready to become the next working buffer, with the same revision as C.
6. The store tries to publish C. On successful enqueue, C becomes immutable and
   A becomes authoritative/writable for subsequent edits.
7. If enqueue fails, C remains the working buffer. Any later attempt must ensure
   its replacement working buffer has been refreshed from the latest C.

The result after successful publication is:

```text
B: audio-referenced, immutable
C: published, immutable
A: authoritative working buffer
```

If no changes are dirty, do not copy and publish merely because a buffer was
returned. Retain an available spare until needed.

The global length-one publication queue permits at most one queued snapshot
across the bank. Each pattern still has its own three buffers and audio reference.
Other patterns may be dirty while that queue is occupied. Their working buffers
continue collecting edits; publication scheduling must not starve them.

### Immutability and release

Once a buffer is published, neither thread changes its contents until audio has
explicitly released it back to the store. This includes a buffer still waiting
in the queue. Publication is a transfer of read access, not a pattern copy.

Release notifications must identify the exact buffer and snapshot revision.
Queued snapshots cannot be reclaimed merely because the store has newer edits.

Machines must not retain pattern references after audio reports release. The
current sampler copies trigger values. Future machines must follow that rule or
explicitly retain the resources they need.

No owning `shared_ptr` destruction, pattern destruction, resizing or allocation
belongs on audio. Storage and reclamation remain store responsibilities.

## Store batching and publication

Applying edits, publishing snapshots and reusing returned buffers are independent.
The store must not wait after one edit for the next callback before accepting
another edit.

For each processing pass:

1. Drain available audio buffer releases and reliable outcomes.
2. Observe the UI request queue depth once and process that batch in FIFO order.
3. Apply edits to the appropriate working buffers and mark them dirty.
4. If publication capacity and a spare buffer are available, publish dirty work.
5. Otherwise retain the dirty working state and continue accepting later edits.

A batch containing one edit may publish that edit alone. Several already queued
edits can publish together. Requests arriving after the batch observation belong
to a later pass. Do not introduce a batching-delay timer.

Further edits accumulate in the writable buffer while audio owns older buffers.
Only unpublished work may be coalesced. Coalescing pattern state does not lose
accepted edits: the resulting state contains their effects. Do not coalesce
transport events or other commands whose individual occurrence matters.

There is no unbounded list of prepared snapshots. The fixed buffer pool bounds
playback snapshot storage. Finite request queues can still exert backpressure if
producers outrun the store; this is distinct from waiting for audio after every
edit.

## Store wake-up mechanism

Use a condition variable with a timed wait on the store thread. No separate timer
thread is needed.

- UI enqueues a request, updates a mutex-protected wake predicate and notifies the
  condition variable. This must avoid a lost notification between checking work
  and sleeping.
- Audio only writes release/outcome records to its non-blocking return channels.
  It does not acquire the wake mutex or notify the condition variable.
- While awaiting audio progress, the store uses a timed wait, initially 1 ms, and
  drains returns when it wakes. Treat 1 ms as a starting point to measure.
- Pending output that requires retrying after a consumer frees capacity also
  needs a wake/retry path; do not sleep indefinitely with such work outstanding.
- When nothing is dirty, in flight or waiting for output capacity, the store can
  wait indefinitely for UI work or shutdown.
- Do not sleep while actionable work is already queued. Do not busy-spin on work
  that cannot progress until another thread frees capacity.

All wait, mutex and wake operations are outside the callback.

## UI editing and snapshots

The store supplies a separate editing snapshot; it is not one of audio's borrowed
playback buffers. Copying that snapshot is non-real-time work.

UI editing remains optimistic:

1. Construct an explicit desired edit tagged with pattern and request IDs.
2. Try to enqueue it to the store.
3. On success, update the current UI editing copy immediately.
4. On queue-full, leave that copy unchanged and display retry feedback.

The store validates and applies accepted requests in FIFO order. Step edits,
clear-row and bulk edits all target the named stored pattern whether playing,
pending or neither. Replacing/cancelling a pending selection does not discard
that pattern's edits. Prepare bulk operations entirely outside audio.

A rejection must return an error and reconcile the authoritative state without
silently discarding later accepted or still-pending optimistic edits. The exact
resynchronisation protocol remains an open decision below.

### Selecting the editing pattern

On a selection request, the store independently:

1. Serves an editing snapshot from its authoritative state.
2. Forwards a playback-selection request through the control queue.

Serving the UI does not wait for audio adoption or a musical boundary.

While the requested editing snapshot is loading:

- Further selections remain allowed.
- Edits to the old displayed pattern are temporarily disabled.
- Only the snapshot matching the latest selection request is installed.
- The UI replaces its single editing copy rather than accumulating snapshots.

An editing snapshot becoming available does not mean playback has switched.
Display editing, playing and pending state separately.

## Two store-to-audio queues and cross-queue ordering

Both queues are SPSC, with the store as sole producer and audio as sole consumer.
The UI never publishes directly to audio.

### Pattern-publication queue: capacity one

Each record contains:

- Pattern ID.
- Borrowed pointer to the immutable full-pattern buffer.
- Buffer ID.
- Snapshot revision.

Audio adopts publications at callback entry. It performs reference/metadata
changes and returns the previous buffer; it never copies the pattern.

### Control queue: separate, larger and bounded

Commands include:

- Select pattern: request ID, target pattern ID, required minimum revision,
  and deferred/immediate mode.
- Transport controls, tagged with their request IDs where responses are needed.

Controls do not consume a free pattern buffer and are not held behind a full
pattern-publication slot. They remain FIFO within the control queue.

### Revision dependency

When the store handles selection of B, it records B's authoritative revision at
that point as the selection's required minimum revision. This covers all accepted
edits to B preceding that selection.

The control and publication queues do not themselves impose a shared FIFO order.
Audio must therefore enforce:

```text
adopted_revision[B] >= selection.required_revision
```

before activating B. A newer coalesced snapshot satisfies the dependency; an exact
intermediate snapshot is unnecessary. Later edits can arrive through later
publications without creating a new selection request.

At callback entry, handle available publications and controls before rendering.
A selection whose dependency is not ready becomes pending; it must not block
processing later controls such as stop, replacement or cancellation.

- Replacing a selection replaces its target, request ID, mode and required revision.
- Selecting the actual playing ID cancels pending selection without waiting for a
  pattern publication. Subsequent edits to the playing pattern still adopt normally.
- Stop has no pattern-revision dependency.
- Deferred activation requires both revision readiness and the playback boundary.
- Immediate activation also requires revision readiness, but not a musical boundary.

The handling of a required revision missing its intended boundary must be confirmed
before implementation; see open decisions. Never silently activate stale contents.

## Playback outcomes

| Situation | Audio action |
|---|---|
| A playing; select B | Pending becomes B with its revision requirement |
| B pending; select C | C replaces B and its requirement |
| A playing; select A | Cancel pending selection |
| Boundary reached and required revision adopted | Activate the pending ID |
| Edit pending C | Publish/adopt updated C contents without creating a new selection |

Preserve the existing boundary rule during migration. Defining pattern length
when tracks differ is separate work. Immediate selection remains supported.

Outcomes distinguish switched, cancelled and superseded selections. Include the
responsible request ID and actual playing pattern ID. Older outcomes may update
known playback state but cannot clear a newer UI request, including B -> C -> B.
Acknowledgements do not change the displayed editing selection.

## Return channels and backpressure

Buffer releases and reliable control outcomes travel from audio back to the store.
They are not UI snapshot payloads and contain no owning pattern objects.

- Reserve reliable outcome/release capacity before accepting work that will need it.
- Deferred and superseded selections must be included in that accounting.
- A boundary switch must not require the GUI to drain a queue at that instant.
- No release may be dropped: doing so would permanently remove a reusable buffer.
- Bound outstanding control work; do not accumulate an unlimited store-side backlog.
- A full publication queue leaves edits in working buffers; it does not stop the
  store from applying further edits or serving UI snapshots.
- A blocked UI must not prevent the store from draining audio returns.

Reliable edit/switch results cannot be dropped. Intermediate playhead/meter updates
may be skipped. Obsolete UI snapshot responses may be coalesced, with any storage
reclaimed outside audio. Exact return-channel layout and capacities remain to be
confirmed below.

The callback must not allocate, lock, wait, perform file I/O, log or destroy pattern
storage. All queue messages on its boundary are small and trivially destructible.

## Startup and shutdown

Startup:

1. Allocate and initialise the three buffers per pattern and editing metadata.
2. Establish audio's initial immutable references and revisions before activation.
3. Establish the store's working/spare roles without exposing writable data to audio.
4. Start the store worker and audio.
5. Request the UI's initial editing snapshot.

Shutdown:

1. Stop accepting new UI requests and wake the store if sleeping.
2. Stop audio and wait for all callbacks to finish.
3. Drain returns and account for queued, referenced and working buffers.
4. Stop/join the store worker, then destroy its storage and channels.

Storage and channels outlive every callback that can reference them. Shutdown must
not require a stopped callback to consume another command or return another buffer.

## Implementation boundary

Introduce only:

- `pattern_store.hpp/.cpp`: authoritative bank, edits, snapshots, buffer roles and worker.
- Editor/store message definitions.
- Store/audio publication, control and return message definitions.

Update existing components:

- `GridWidget`: one editing copy and its metadata, not a full bank reference.
- Sequencer: borrowed immutable references and revision-gated selection, not stored edits.
- Application startup/shutdown: wire the three components and their lifetimes.
- Preview and fixtures: use the same store-facing UI protocol.

No networking implementation, general event bus, persistence layer, source-tree
reorganisation, machine replacement, transport timing rewrite or chunking.

## Acceptance tests

### Buffer safety and publication

- All three buffers rotate through their roles without callback-side pattern copies.
- Published/queued contents remain unchanged until explicit release.
- An empty submission queue does not permit reuse before a release arrives.
- Edits continue while an older publication awaits the next callback.
- Multiple waiting edits publish together; no timer deliberately delays publication.
- No dirty changes means no unnecessary publication.
- Every dirty pattern eventually publishes; a busy pattern cannot starve another.
- The pool stays at three full-pattern buffers per pattern under backlog.

### UI and editing

- UI retains one editing pattern, plus its metadata and deliberate clipboard snapshot.
- Rapid selection rejects stale snapshots without waiting for audio.
- Optimistic edits update only after successful enqueue; queue-full changes no UI data.
- Step edits, clear, selection toggle and paste preserve stored and remembered-note state.
- Rejection/resynchronisation preserves later valid optimistic edits.

### Playback and ordering

- A -> pending B -> pending C activates C; selecting A cancels.
- Playback cannot select an older revision than the request requires.
- Newer coalesced revisions satisfy earlier minimum-revision dependencies.
- A not-ready selection does not block stop, cancellation or replacement controls.
- Old outcomes cannot finish the latest request, including repeated pattern IDs.
- Agreed missed-boundary and immediate-readiness behaviour is tested explicitly.

### Scheduling and lifecycle

- UI notifications cannot be lost at the store's sleep transition.
- Timed wake-up notices audio releases without audio notification or busy-spinning.
- Idle store can sleep until work or shutdown; pending retries cannot sleep forever.
- Saturating reliable channels loses no buffer releases or control outcomes.
- Shutdown succeeds with queued publications, pending selections and a sleeping store.

Build both executables and run existing regressions throughout. Measure callback
work separately from store-thread copy cost and wake-up latency.

## Open decisions — confirm before implementation

These are deliberately not silently chosen by this document:

1. **Missed revision deadline.** If a deferred selection's required snapshot is
   not adopted by the intended boundary, the proposal is to retain the selection
   and wait for the next boundary, never switch mid-cycle or use stale contents.
   Confirm that behaviour. For an immediate request awaiting a revision, confirm
   activation at the first callback where it is ready.
2. **Optimistic rejection/resynchronisation.** Choose the concrete snapshot and
   processed-request watermark/replay protocol. Later valid edits must survive;
   replacing the UI copy with an older snapshot is not sufficient.
3. **Reliable channels and limits.** Specify UI request/response and audio control/
   return capacities, return-channel separation, and the reservation accounting
   that guarantees room for deferred outcomes and buffer releases.
4. **Dirty-pattern scheduling.** Specify a fair publication order when several
   patterns are dirty and share the global length-one publication queue.

The already agreed three buffers per pattern, optimistic UI, two outgoing audio
queues, full-pattern copying off audio, and timed wake-up are not open decisions.

## Future remote UI and limits

The UI/store protocol is the future network boundary. Requests, snapshots and
outcomes can be serialized. Pointers stay strictly inside the local store/audio
connection. Reconnection, access control and multiple-editor conflict rules are
not implemented by this design.

Full-pattern copies can eventually slow the store thread. Measure that separately
from audio performance. This specification intentionally excludes chunking and
other alternative storage designs.
