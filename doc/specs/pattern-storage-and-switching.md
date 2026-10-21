# Store-owned patterns and snapshot switching

Status: implemented. The detailed protocol is in
`2026-10-19-new-architecture.md`; this replaces the previous audio-owned bank design.

## Ownership

Store owns eight editable originals and five shared playback snapshot buffers.
UI holds one editing copy and its metadata plus a deliberate clipboard. Audio
borrows only current and optional pending immutable references. It never reads
store originals or holds the complete bank.

Only an explicit release returns a snapshot allocation to the store. A separate
capacity-five release queue covers every allocation. Published/queued contents
remain immutable until release is consumed.

## Selection

Press P then 1–8. Escape cancels number entry. Successful submission requests an
editing copy independently of musical playback. While loading, editing/copying is
disabled; further selections remain available. Stale editing snapshots are ignored.

Store copies the selected original into a free snapshot and publishes one message
containing reference and selection mode through a capacity-three queue:

- Different from current, deferred: replace pending and return its old snapshot.
- Same as current: cancel pending and adopt the incoming current contents.
- Immediate: replace current and clear pending.
- At the existing track-zero boundary: move pending to current, return old current.

Deferred switching waits while stopped. Transport position and sample tails are
preserved. New selection publication takes priority over dirty content updates.
If resources are busy, retain only the latest unsent selection; originals retain
all accepted edits. Already queued messages remain FIFO.

## Updates and status

Store edits originals, then publishes snapshots for relevant playing/pending or
in-flight targets. Audio replaces a matching reference and returns the old buffer.
An update matching neither current nor pending is returned unused. Mid-chunk edits
wait for another queue check, not another musical boundary.

Start/stop/tempo use a separate capacity-64 event queue. Audio processes events
then snapshot messages before bounded approximately 5 ms chunks. There is no
cross-queue order, snapshot revision or reliable audio switch acknowledgement.
Store forwards latest playing/current/pending and playhead telemetry to UI.

UI request results report store acceptance/rejection, not audio activation.
Rejected optimistic edits recover through a FIFO resync barrier. Queue-full
leaves UI data unchanged. No callback-side edit application, bulk payload markers,
full-pattern copies or acknowledgement backpressure remain.

## Checks

The unified regression executable covers selection replacement/cancellation,
immediate/deferred switches, content updates, finite pool reuse, store editing,
UI loading/recovery, metadata and clipboard preservation. Actual timing and live
output still require runtime checks; no new pattern-length policy is introduced.
