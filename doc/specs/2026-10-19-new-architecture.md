# New architecture: UI, pattern store and audio

Date: 2026-10-19
Status: planned; not implemented. Agreed requirements below supersede the earlier
per-pattern buffer pools, two outgoing queues and minimum-revision activation gate.
Open decisions at the end must be resolved before the affected implementation.

## Goal and agreed choices

Separate editing, authoritative storage and real-time playback:

- UI holds only its current editing pattern, not a full bank cache.
- A dedicated non-real-time pattern-store thread owns eight editable originals.
- In addition to those originals, the store owns exactly three preallocated
  full-pattern playback snapshot buffers, shared across the entire bank.
- Audio borrows at most two snapshot references: current and optional pending.
- One combined, bounded FIFO queue carries controls and snapshot references from
  store to audio. It is not restricted to length one.
- Buffer availability limits snapshot publication independently of queue capacity.
- All whole-pattern copies happen outside audio. Publishing means enqueueing a
  pointer and small identifiers, not copying pattern contents into audio.
- UI edits remain optimistic after successful request enqueue.
- UI work wakes the store through a condition variable. Audio returns are polled
  through timed waits; audio does not notify that condition variable.

New selections replace pending selections. Selecting the actual playing pattern
cancels pending playback. Audio decides when a deferred switch occurs.

Preserve musical switch timing rather than waiting for a newer revision. An update
arriving mid-callback is applied at a later callback start, not another pattern
boundary. No chunking, per-track sharing or persistent data structures are included.

## Components

```text
UI — main thread
  One editing copy, current-pattern metadata, cursor, selection, clipboard
       |
       | requests / editing snapshots and results
       v
Pattern store — dedicated non-real-time thread
  Eight editable originals + three playback snapshots TOTAL
       |
       +-- combined FIFO: controls + snapshot references --> Audio
       ^
       | snapshot releases and reliable control outcomes
       |
Audio — callback thread
  Current snapshot reference + optional pending snapshot reference
  Transport, machines and playback state
```

The store is not part of the GUI. A future remote UI can communicate with it
through an adapter while store and audio remain local.

## Ownership and memory

| Data | Owner |
|---|---|
| Eight authoritative editable patterns | Store |
| Three playback snapshot allocations total | Store |
| Queued, current and pending snapshot references | Audio borrows; store retains backing storage |
| Current editing copy | UI |
| Clipboard snapshot | UI |
| Playing/pending state and their adopted revisions | Audio |
| Transport, voices and playback cursors | Audio |
| Playheads/runtime status | Audio produces; store forwards to UI |

The eight originals and three snapshot buffers are separate allocations. A busy
snapshot pool never prevents applying an edit to an original. Audio never reads
an original, and the UI never reads mutable store or audio fields directly.

Per-pattern remembered-note metadata belongs to the store. Editing snapshots
include the current pattern's metadata only. Preserve restoration after toggling,
clearing and revisiting patterns without retaining an eight-pattern metadata cache
in the UI. Playback snapshots need not include editor-only metadata.

The UI editing copy and deliberate clipboard snapshot are not members of the
three-buffer playback pool. No hidden full-bank cache exists in UI or audio.

## Snapshot references and identities

A reference identifies:

- Snapshot buffer ID: one of the three stable-address allocations.
- Borrowed `const pattern*`.
- Stored pattern ID whose contents were copied.
- Pattern revision represented by the snapshot.

Each stored pattern has a monotonically increasing logical edit revision. Buffer
IDs are reused, so buffer ID alone does not identify snapshot contents. Request
IDs separately associate UI operations, selections and their responses.

Audio conceptually holds:

```cpp
snapshot_ref current_;
std::optional<snapshot_ref> pending_;
```

It does not retain a reference array for all eight patterns. A pending selection
also carries its selection request ID and deferred/immediate mode.

Revisions identify freshness and prevent regression; they are not a minimum-version
gate that can postpone an otherwise due pattern switch.

## Snapshot pool lifecycle

Roles rotate across the three allocations. No buffer permanently belongs to a
particular pattern, audio position or editing role.

Typical state:

```text
Store originals: A, B, C, D, E, F, G, H — editable

Snapshot 0: current A — immutable, borrowed by audio
Snapshot 1: pending B — immutable, borrowed by audio
Snapshot 2: queued B update or another selection — immutable
```

In that state all snapshots are occupied. Further edits accumulate in the eight
originals. There is no requirement for a fourth writable playback snapshot.

Publication:

1. Store obtains a free snapshot buffer.
2. Store copies the desired original into it, recording pattern ID and revision.
3. Store enqueues a message carrying its reference in the combined queue.
4. Successful enqueue makes that buffer immutable until explicit release.
5. Failed enqueue does not transfer ownership; the store may refresh/reuse that
   unpublished buffer, keeping the corresponding publication work outstanding.

Release:

1. Audio ceases all use of a superseded, cancelled or no-longer-needed reference.
2. It returns that exact buffer identity/revision to the store.
3. Only after consuming the release may the store overwrite the allocation.

Consuming a queue message does not itself return a buffer: it may now be current
or pending. Queue emptiness is not an ownership signal. Replacing/cancelling a
pending reference, updating a reference, or switching away from current can free
buffers. Unused snapshots also require explicit return.

The fixed pool, not queue length, bounds snapshot memory. When audio holds only
current, two buffers may be queued. When it holds current and pending, only one
additional buffer can be queued. A deeper command queue still has room for controls.

Audio never copies or destroys patterns, resizes buffers, allocates, or performs
owning reference-count operations. Machines must not retain a pattern reference
after audio reports its release. The current sampler copies trigger values;
future machines must obey the same lifetime rule or explicitly retain resources.

## Combined store-to-audio queue

Use one bounded SPSC FIFO. Store is the only producer; audio the only consumer.
UI does not publish directly to it.

Conceptual messages:

- **Select:** target ID, prepared initial snapshot reference, selection request ID,
  and deferred/immediate mode. Selection and its initial contents are one message.
- **Update:** snapshot reference for a pattern already current or pending. This
  changes contents without creating a new selection or changing its request ID.
- **Transport:** controls such as play/stop/tempo; no snapshot buffer required.

Queue capacity and available snapshot count are independent. There is no global
one-snapshot-in-flight rule and no length-one publication queue.

The exact handling of an attached snapshot when a selection names the actual
playing pattern remains to be confirmed below. Cancellation must not schedule a
new deferred switch or lose an accepted content update.

### Callback processing

At callback entry, observe queue depth once and process that captured batch in
FIFO order before rendering. Multiple controls and snapshot adoptions may be
processed in the same callback. Later arrivals wait for a later callback.

Selection B followed by an edit can therefore appear as:

```text
Select B, snapshot B revision 11
Update B, snapshot B revision 12
```

Audio first makes revision 11 pending, then replaces it with revision 12 and returns
the revision 11 buffer. At the musical boundary, the pending snapshot becomes
current. No cross-queue revision dependency is needed.

An update replaces a matching current or pending reference, does not regress its
revision, and returns the old snapshot. If an update no longer matches either
reference, return the incoming buffer without changing selection; this is a
defensive fallback, not the normal store publication policy.

## Selection, replacement and musical timing

| Situation | Audio action |
|---|---|
| A playing; select B with B snapshot | B becomes pending |
| B pending; select C with C snapshot | Release pending B; C replaces it |
| A playing; select A | Cancel and release pending, if any |
| Deferred boundary reached | Release old current; move pending to current; clear pending |
| Update pending B | Replace pending contents; keep selection timing/request identity |
| Update playing B | Replace current contents at callback entry |
| Immediate select B | Replace current at callback entry, with no musical-boundary wait |

Preserve the existing boundary rule during migration. Defining pattern length
when tracks differ is separate work. Deferred switching while stopped waits for
playback. Preserve transport position and sample tails.

### Mid-callback edit example

```text
Audio starts callback and processes queued messages.
Audio renders A; B revision 11 is pending.
Store receives a paste into B and queues revision 12.
Audio reaches the boundary and activates pending B revision 11.
Next callback adopts B revision 12 if that update was queued in time.
```

This short stale-content interval is accepted. Do not miss a whole musical cycle
waiting for the newer revision, and do not poll the queue midway through rendering.
An update queued after the callback's depth observation cannot affect that batch.

If revision 12 is queued by the next callback's depth observation, adopt it before
rendering that block. Store scheduling, pool exhaustion or preparation work can
make it later; there is no unconditional one-block delivery guarantee.

### Store publication policy

Store retains all edits, but publishes content updates only for patterns still
playing or pending. Once selection C supersedes pending B, do not send further
updates to B unless B is still playing. B's original remains up to date for a
future selection.

An update already enqueued before Select C cannot be retracted and remains before
it in FIFO order. Selection messages carry the initial snapshot, so there is no
separate initial publication that can arrive before/after its selection.

Store must distinguish requested selection from audio's confirmed playing state.
A command in flight does not prove a boundary switch has happened. Reconciliation
and scheduling under pool exhaustion remain to be specified below.

Outcomes distinguish switched, cancelled and superseded requests. Include the
responsible request ID and actual playing state. Old outcomes may update known
playback state but cannot clear a newer UI request, including B -> C -> B.
Acknowledgements never change the displayed editing selection.

## Store batching and wake-up

Editing originals, publishing snapshots and processing releases are independent.
Do not wait after each edit for the next audio callback.

Each store pass:

1. Drain snapshot releases and reliable audio outcomes.
2. Observe the UI request queue depth once and process that batch in FIFO order.
3. Apply valid edits to originals and record outstanding publication work.
4. Where buffers and queue space permit, publish prepared selections/updates.
5. Otherwise keep the originals current and retry publication later.

A single queued edit may publish alone. Several already waiting edits can be
combined into one resulting snapshot. Later edits accumulate in originals while
older snapshots are queued/current/pending. Do not introduce a batching timer.

Only unpublished work may be coalesced. The resulting snapshot contains the effects
of accepted edits; intermediate snapshots may be skipped. Do not coalesce arbitrary
transport events or commands whose individual occurrence matters.

Use a condition variable with timed waits, not a separate timer thread:

- UI enqueue updates a mutex-protected wake predicate and notifies the store.
  Avoid a lost notification between checking work and sleeping.
- Audio only pushes non-blocking return records. It never locks or notifies the
  condition variable.
- While awaiting audio progress, use a timed wait initially 1 ms. Measure it rather
  than treating 1 ms as a timing guarantee.
- Pending output-capacity retries also need a wake/retry path.
- When nothing requires retry or audio progress, wait for UI work or shutdown.
- Never sleep with actionable queued work or busy-spin when another thread must
  free capacity first.

Finite request/control queues can still exert backpressure. A full snapshot pool
alone does not prevent editing originals or serving UI snapshots.

## UI editing and snapshots

Store serves separate non-real-time editing snapshots from the originals. They
are not borrowed playback-pool buffers and do not wait for callbacks.

UI holds one editing pattern and its remembered-note metadata. On selection:

1. Send a request tagged with its request ID.
2. Store serves an editing snapshot and prepares/forwards playback selection.
3. These complete independently; editing need not wait for playback handoff.
4. Only a response matching the latest editing selection may replace the UI copy.

While loading, further pattern selections remain allowed but edits to the old
shown pattern are temporarily disabled. Show editing, playing and pending state
separately. Preserve the deliberate clipboard independently of selection changes.

Optimistic editing:

1. Enqueue an explicit desired edit with pattern and request IDs.
2. On success, immediately change the UI copy.
3. On queue-full, leave it unchanged and provide retry feedback.
4. Store validates/applies requests to originals in FIFO order, regardless of
   whether the edited pattern is playing or pending.

Bulk operations are coherent and prepared outside audio. Cancelled or replaced
selections do not discard their stored edits.

### Rejected optimistic edits: resynchronisation barrier

Use a FIFO resynchronisation barrier, not an edit-replay journal.

1. Store reports the rejected edit with its pattern and request IDs.
2. If it affects the current editing copy, UI temporarily pauses edits to that
   pattern and queues a resync request after its already-submitted edits.
3. Store processes preceding requests in order, applying valid edits and rejecting
   invalid ones, then serves an authoritative snapshot when it reaches the barrier.
4. UI installs that snapshot only if it still belongs to the current editing
   selection and resync request, then resumes editing.

The response therefore includes later valid edits submitted before the barrier;
UI does not replay or resend them. Do not roll back immediately to an older copy.
If the request queue is full, remain paused and retry enqueueing the barrier rather
than dropping it or changing the authoritative originals.

Pattern selection remains available during recovery. A response for an old editing
selection cannot replace a newly selected pattern. A rejection concerning a pattern
no longer being edited does not overwrite or pause the unrelated current editing
copy; selecting that pattern later obtains authoritative contents as usual.

Recovery depends only on the UI/store exchange, never on an audio callback.

## Reliable returns and capacity

Audio returns exact snapshot releases and reliable control outcomes. These records
contain identifiers/references, not owning pattern objects.

- No buffer release may be dropped or overwritten.
- Return capacity must cover accepted work, including releases caused by pending
  replacement/cancellation, unused incoming snapshots and boundary activation.
- Reliable outcome capacity must account for deferred and superseded requests.
- A musical-boundary switch cannot depend on the GUI draining a queue at that instant.
- Store must always be able to drain audio returns, even if UI output is blocked.
- Limit outstanding/staged controls; never accumulate an unbounded backlog.
- If no snapshot is free, retain latest content in originals; do not allocate a fourth.

Reliable edit/control results are not disposable. Intermediate playheads/meters
may be skipped. Obsolete UI snapshots may be coalesced, with storage reclaimed
outside audio. Channel layout, capacities and reservation accounting remain open.

The callback must not allocate, lock, wait, perform file I/O, log or destroy
pattern storage. Its queue messages are small and trivially destructible.

## Startup and shutdown

Startup:

1. Initialise eight originals, their revisions and editor metadata.
2. Allocate exactly three playback snapshots.
3. Copy the initial playing pattern into one snapshot and establish audio's
   current reference before activation. Pending starts empty; the other two are free.
4. Start the store worker and audio.
5. Request the initial UI editing snapshot.

Shutdown:

1. Stop accepting new UI requests and wake the store.
2. Stop audio and wait for all callbacks to finish.
3. Account for all current, pending, queued and returned snapshot buffers.
4. Stop/join store, then destroy originals, snapshots and channels.

Storage/channels outlive every callback referencing them. Shutdown must not wait
for a stopped callback to consume a command or return a buffer.

## Implementation boundary and checks

Introduce store ownership/worker and editor/store/audio message definitions.
Update GridWidget to use one editing copy, sequencer to use current plus optional
pending references, and startup/preview/fixtures to use the same protocols.

Do not combine this with a source-tree reorganisation, persistence, networking,
machine replacement, chunking or transport timing rewrite.

Acceptance tests:

- Exactly eight originals plus three playback-pool buffers, not three per pattern.
- Audio holds no bank and at most current plus optional pending references.
- Published/borrowed contents remain unchanged until explicit release.
- All copies/destruction occur outside audio; no reuse inferred from queue emptiness.
- Multiple snapshot messages can occupy the combined queue when buffers permit.
- Controls consume no buffer unless introducing a snapshot.
- Edits continue in originals when all snapshots are occupied.
- Initial selection and its snapshot are one message; later updates preserve FIFO.
- Replacement, cancellation, repeated IDs and stale outcomes behave correctly.
- A mid-callback edit does not postpone the musical switch by a cycle.
- A queued update is adopted before rendering the next observed callback batch.
- Updates are not published for inactive originals merely because they were edited.
- Rapid UI selections reject stale editing snapshots without waiting for callbacks.
- Optimistic queue-full changes no UI data; rejection recovery preserves valid edits.
- Remembered notes, clear, selection toggle, paste and clipboard behaviour survive.
- Wake-ups are not lost, returns are noticed without audio notification, idle does
  not busy-spin, and shutdown works with outstanding work.
- Saturating channels loses no release/outcome and allocates no extra snapshots.

Build both executables and run existing regressions throughout. Measure callback
reference/queue work separately from store copying and wake-up latency.

## Open decisions — discuss before implementing

1. **Reliable channel layout and capacities.** Set combined command, UI request/
   response and audio return capacities, plus accounting for guaranteed releases
   and deferred control outcomes.
2. **Store scheduling with an exhausted pool.** Specify how unsent selections,
   playing/pending dirty updates and later controls are staged and ordered. Define
   handling of newer selections before an older one could acquire a buffer, and
   reconcile in-flight selections with confirmed audio state. Do not resurrect
   an obsolete pending selection or publish unrelated inactive patterns.
3. **Snapshot attached to selection of the playing ID.** Cancellation itself is
   agreed. Decide whether a newer incoming snapshot also updates current, or is
   returned with explicit continued publication of its still-unapplied edits.
   It must not silently lose those edits, regress current contents or leak buffers.

The combined FIFO, exactly three snapshots total in addition to eight originals,
current/optional-pending audio references, full copies outside audio, optimistic
UI, FIFO-barrier rejection recovery and timed wake-up are settled. There is no minimum-revision deadline decision
and no separate publication queue: those were superseded by the agreed model.

## Future remote UI and limits

The UI/store protocol is the future network boundary. Requests, snapshots and
outcomes can be serialized. Pointers remain inside the local store/audio
connection. Reconnection, access control and multi-editor conflicts are future work.

Whole-pattern preparation can eventually slow the store thread. Measure that
separately from audio performance. Alternative storage designs are outside scope.
