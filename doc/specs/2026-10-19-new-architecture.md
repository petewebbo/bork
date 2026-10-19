# New architecture: UI, pattern store and audio

Date: 2026-10-19
Status: planned; not implemented.

## Goal

Separate editing, authoritative pattern storage and real-time playback. The UI
holds only its current editing pattern. Large pattern copies, allocation and
reclamation stay outside the audio callback.

Preserve playback behaviour: a new selection replaces the pending pattern;
selecting the playing pattern cancels the pending switch. Audio alone decides
when a deferred switch occurs.

## Components

```text
UI — main thread
  One editing copy, cursor, selection, clipboard
       |
       | requests / snapshots and results
       v
Pattern store — dedicated non-real-time thread
  Authoritative pattern bank, edits, snapshots, immutable versions
       |
       | prepared versions and playback requests
       | adoption, switch and release results
       v
Audio — callback thread
  Read-only pattern references, playing/pending IDs, transport, machines
```

The pattern store is not part of the GUI. A future remote GUI can communicate
with it through a network adapter while the store and audio remain local.

## Ownership

| Data | Owner |
|---|---|
| Latest authoritative version of every pattern | Pattern store |
| Previous versions still needed by audio | Pattern store, retained until released |
| Current editing copy | UI |
| Clipboard snapshot | UI |
| Playing and pending pattern IDs | Audio |
| Transport, voices and playback cursors | Audio |
| Published playheads and status | Audio produces; store forwards to UI |

No component reads another component's mutable ordinary fields.

Per-pattern remembered-note metadata moves from the UI into the store. Editing
snapshots include this information for the current pattern only, preserving note
restoration after toggling, clearing and revisiting a pattern without retaining
an eight-pattern editing cache in the UI.

The clipboard remains a deliberate user-created snapshot, not a hidden bank
cache.

## Immutable pattern versions

A version cannot change after publication to audio.

When an edit arrives, the store:

1. Produces an updated version on its own thread.
2. Makes it the authoritative version.
3. Publishes its address and identity to audio.
4. Retains previous versions until audio no longer references them.

Initially, copying the whole pattern on the store thread is sufficient. Do not
introduce chunked storage or copy-on-write infrastructure until measurements
justify it.

Audio keeps a small array of read-only references, one per pattern ID. It neither
owns nor copies their contents. At callback entry, adopting an update replaces
the relevant reference with constant-sized work. A playback switch changes the
playing ID, not the pattern contents.

Keeping references for all stored IDs means a deferred selection uses the
latest version adopted for that pattern.

## Editing flow

```text
UI                         Pattern store                    Audio
Edit step ---------------> Validate and apply
Update editing copy        Prepare new version
                           Publish version ----------------> Adopt at block start
                           <------------------------------- Release old version
                           Reclaim old version when unused
```

Requests contain explicit desired values and a pattern ID. Retain the distinction
between ordinary edits and coherent bulk operations.

- Edits to the playing pattern become audible after version adoption.
- Edits to the pending pattern update what will play when it activates.
- Edits remain stored if a pending switch is replaced or cancelled.
- Bulk operations are prepared entirely outside the callback.

The UI may retain its current optimistic behaviour: update its editing copy after
successful request enqueue. If the store rejects a request, return an error and
an authoritative replacement snapshot rather than silently diverging. A rejection
must reconcile any later optimistic edits; it must not blindly overwrite them
with an older snapshot.

## Pattern selection

A selection request goes to the store, which:

1. Returns an editing snapshot of the requested pattern.
2. Forwards the playback-selection request to audio.

These complete independently.

### Editing snapshot

The snapshot is prepared when the store handles the request. It does not wait
for a callback or musical boundary.

While loading:

- Further selections remain allowed.
- Edits to the old displayed pattern are temporarily disabled.
- Only a snapshot matching the latest selection request is installed.

The UI replaces its single editing copy rather than accumulating snapshots.
An editing snapshot becoming available does not mean playback has switched.
Display editing, playing and pending state separately.

### Playback selection

| Situation | Audio action |
|---|---|
| A playing; select B | Pending becomes B |
| B pending; select C | Pending becomes C |
| A playing; select A | Cancel pending selection |
| Boundary reached | Activate the pending ID |
| Edit pending C | Adopt C's updated version without changing its scheduled activation |

Preserve the existing boundary rule during migration. Defining a pattern boundary
when tracks have different lengths is a separate decision. Immediate selection
remains supported by the command protocol.

## Protocols and ordering

Use two distinct protocols, not one command structure carrying every concern.

### UI to store

Requests:

- Select pattern.
- Set step or clear row.
- Apply bulk edit.
- Transport controls.

Responses:

- Editing snapshot.
- Accepted or rejected edit result.
- Playback-selection outcome.
- Latest runtime status.

### Store to audio

Commands:

- Adopt an immutable version for a pattern ID.
- Select a pattern, deferred or immediate.
- Transport controls.

Returns:

- Version adopted and previous version released.
- Selection switched, cancelled or superseded.

The store is the sole producer of audio commands. The UI no longer sends directly
to audio. FIFO ordering preserves edit/version publication and playback requests.

Keep two identities distinct:

- Request ID: associates a response with a user operation.
- Version ID: identifies a particular immutable pattern allocation.

A stale playback response may update known playing state, but cannot clear a
newer UI request. Repeated IDs, such as B -> C -> B, must remain unambiguous.

## Lifetime and real-time guarantees

The store owns allocations. Audio borrows references.

Do not use owning `shared_ptr` destruction on audio. A pointer replacement must
not accidentally delete a large pattern, release samples or run other heavyweight
destruction.

A version remains alive while it is:

- The store's latest version;
- Queued for adoption; or
- Referenced by audio.

Audio reports release only after it has stopped using the old version. The store
then decides whether to destroy it. Publishing a newer version alone does not
make an older queued version safe to reclaim.

Machines must not retain pattern references beyond that lifetime. The current
sampler copies trigger values. Future machines must follow the same rule or
explicitly retain the resources they need.

### Bounded communication

Reuse the existing SPSC queue dependency, with one producer and one consumer per
channel and fixed queue capacities.

- Audio-bound messages contain small, trivially destructible values and borrowed
  references, not owning pattern objects.
- Limit the number of in-flight audio requests.
- Reserve reliable return capacity for those requests, including deferred
  outcomes and superseded requests. A musical-boundary switch must not depend on
  the GUI draining a queue at that moment.
- If publication capacity is exhausted, the store applies backpressure instead
  of accumulating unlimited versions.
- The callback must not allocate, lock, wait, perform file I/O, log or destroy
  pattern storage.

Reliable edit/switch outcomes and lifetime releases cannot be dropped. Intermediate
playhead and meter updates may be skipped. Obsolete UI snapshot responses may be
coalesced, provided their storage is reclaimed off the audio thread.

## Startup and shutdown

Use a dedicated store thread so large copies stall neither audio nor the UI.

Startup:

1. Create the store and initial versions.
2. Establish audio's initial references before activation.
3. Start the store worker and audio.
4. Request the initial editing snapshot.

Shutdown:

1. Stop accepting UI requests.
2. Stop audio and wait for callbacks to finish.
3. Drain returns and release audio/queued references.
4. Stop the store worker and destroy its versions.

The store and communication channels outlive every callback that can reference
them. Shutdown must not require a stopped callback to consume another command.

## Implementation boundary

Introduce only the pieces needed for these responsibilities:

- `pattern_store.hpp/.cpp`: bank, edit application, snapshots and version lifetime.
- Editor/store message definitions.
- Store/audio message definitions.

Change existing components:

- `GridWidget`: own one editing copy rather than reference a full pattern bank.
- Sequencer: read immutable versions rather than own and edit stored patterns.
- Application startup: wire the three components together.
- Preview and test fixtures: use the same store-facing UI protocol.

Do not include a general event bus, networking framework, persistence layer,
source-tree reorganisation, machine replacement or transport timing rewrite.

## Implementation order and checks

1. **Store and single-pattern editing snapshots.** Test rapid selection, stale
   snapshot rejection and preserving remembered-note behaviour.
2. **Immutable audio adoption and reclamation.** Test that queued and playing
   versions cannot be freed early, including multiple updates to the same ID.
3. **Playback selection through the store.** Test replacement, cancellation,
   repeated IDs and boundary races.
4. **All edit paths and metadata.** Cover step edits, clear, selection toggle,
   paste, non-playing edits and optimistic-edit rejection/resynchronisation.
5. **Backpressure and shutdown.** Fill queues deliberately. Verify bounded
   in-flight storage and no dropped reliable outcomes or lifetime releases.

Verify that audio performs no pattern-sized copy, allocation, lock acquisition or
pattern destruction. Build both executables and run the existing regressions
throughout. Measure callback work separately from store-thread preparation cost.

## Future remote UI

The UI/store protocol is the future network boundary. Requests, snapshots and
outcomes can be serialized; pointers stay strictly inside the local store/audio
connection.

Networking still requires serialization, reconnect/resynchronization and access
control. Multiple simultaneous editors require conflict rules. None are included
in this implementation.

## Known limit

Whole-pattern preparation may eventually slow the store thread as patterns grow.
Measure that first. Smaller immutable chunks are a possible later improvement,
not a reason to copy large patterns in the audio callback.
