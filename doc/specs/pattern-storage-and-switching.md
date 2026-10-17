# Pattern storage and double-buffered switching

Status: implemented. The UI displays the requested pattern immediately on
successful submission rather than waiting for audio acknowledgement.

## Ownership

- UI owns eight in-memory stored patterns (IDs 1–8), displays and edits them
  directly. Pattern 1 starts with the startup pattern; 2–8 start empty.
- Audio renders from one of two preallocated pattern slots. The other slot is
  UI-writable only while spare. Runtime transport/voices are separate.
- UI copies a selected stored pattern into the spare, then publishes one
  activation command. It cannot touch that slot again until audio retires it.
- Queues and slots outlive callbacks. No audio access to UI storage; no UI reads
  of audio current. Slot ownership is transferred by SPSC publication.

## Selection and editing

1. Press P, then 1–8. The always-visible top status bar prompts
   'Select pattern 1–8 / Esc: cancel'. Number selection clears the prompt;
   Escape cancels without quitting. Other keys cannot edit/control playback
   while awaiting a number. Poll status/acknowledgements as usual. The bar also
   displays feedback and stays visible when empty; no popup is used.
2. Copy storage[N] into the spare slot and enqueue activation with slot index
   and pattern ID. Queue-full leaves display and
   selection unchanged and shows a retry message.
3. On enqueue success, immediately display/edit storage[N]. Show its ID and a
   separate pending/now-playing indication until audio acknowledges adoption.
4. At callback entry audio processes its captured command batch in FIFO order,
   adopts the pending slot by index swap, and acknowledges pattern ID and
   retired slot. No full-pattern copy occurs in audio.
5. UI consumes acknowledgement, updates now-playing indication and regains the
   retired slot as spare. It does not change its displayed selection on ack.

Every step/clear-track edit carries a pattern ID. UI commits a storage edit only
after enqueue succeeds. Audio applies it only to the matching current pattern.
The UI is the sole editor and submits all operations synchronously through one
FIFO command queue, so activation precedes subsequent edits even across callback
batches. No activation version counter is needed. If adoption is later deferred,
pattern IDs can route edits to current/pending; deferred scheduling and
cancellation are not implemented in this work.

Only one switch may be outstanding until acknowledgement is consumed. Reject
further requests with feedback; never overwrite a submitted slot. Selecting the
currently selected pattern is a no-op when no request is pending.

## Playback and completion

- Switch at the next callback that observes activation, including when sequencer
  playback is stopped. PortAudio callbacks continue while transport is stopped.
- Preserve transport position and existing sample tails.
- Dedicated acknowledgement SPSC queue has room for every outstanding request
  (one). Ownership notifications must never be dropped or block the callback.
- No synchronous inactive-stream switching path, persistence, musical-boundary
  scheduling or machine replacement is included.
- Snapshot queue-depth processing remains unchanged. Pattern-target validation,
  pointer/index adoption and acknowledgement are bounded, non-allocating work.

## Acceptance

- Eight patterns retain independent edits across switches.
- Display changes on successful submission, not before or on acknowledgement.
- Audio changes only at callback entry; selection works with transport stopped.
- Edits after submission affect selected storage/new audio activation, never old
  audio current. Repeated visits to the same pattern retain FIFO edit ordering.
- Queue-full does not change display/storage; pending request rejects another.
- Repeated swaps safely recycle slots and preserve position/tails.
- Status-bar input handles numbers/cancellation, blocks editing while awaiting
  a number, and survives resize. The top bar remains visible when empty.
- Idle UI polls acknowledgements; shutdown with a pending request is safe.
