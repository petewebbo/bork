# Audio-owned patterns and deferred switching

Status: implemented. Successful submission changes the displayed pattern
immediately; audio owns the complete stored pattern bank.

## Ownership

- Audio owns all eight stored patterns, the playing pattern ID, and an optional
  pending selection. Pattern selection never transfers pattern contents.
- UI keeps a separate display/edit cache, initialised before callbacks start.
  It is the sole editor and updates its cache only after successful submission.
- No UI reads or writes of the live audio bank are allowed after activation.
- Commands, edit payloads, responses and status outlive callbacks.

## Selection

Press P then 1–8. The permanent top bar prompts for the number; Escape cancels
number entry. Other keys cannot edit/control playback while awaiting a number.

On selection, enqueue only the pattern ID, deferred/immediate mode and a switch
request ID. On success, display the selected cached pattern and mark the request
pending. Queue-full leaves the selection unchanged. Selecting the displayed
pattern again is a no-op.

Audio handles requests in FIFO order:

- Different from the playing ID: set or replace the pending selection.
- Same as the playing ID: clear the pending selection and acknowledge cancellation.
- At the existing track-0 cycle boundary: activate the latest pending selection
  and acknowledge the switch. Deferred switching while stopped waits for playback.
- Immediate requests activate without waiting for the boundary.

For example: playing A, select B, select C means C replaces B. Selecting A instead
cancels. No replacement handshake, spare slot, or slot-return response exists.
Transport and sample tails are not reset.

## Editing

Every step edit, row clear and full-pattern edit carries its target pattern ID.
Audio applies it to that stored pattern whether it is playing, pending or neither.
Replacing or cancelling a pending selection does not discard its edits.

Bulk edits retain their four-entry payload queue plus ordered command marker.
Every marker consumes one payload. Invalid pattern IDs are discarded safely.
Single and bulk edits preserve ordering with selection commands.

## Responses

A response contains the actual playing pattern ID, switched/cancelled result,
and the request ID responsible for that outcome. Superseded pending selections
need no response because they transfer no ownership.

The UI always updates its playing indicator from a response, but only clears
pending state when the response matches its latest submitted request ID. This
also handles B -> C -> B and a boundary activation racing with cancellation.
Acknowledgement does not change the displayed selection.

The response queue is bounded and non-blocking. If full, command processing
stops before the next activation command without popping it. Ordinary edits can
continue until that command. The UI drains responses during its regular refresh.
An unsuccessful boundary acknowledgement leaves the pending selection intact.
No command or completion is silently discarded because the response queue is full.

## Checks

- Independent pattern contents survive activation, replacement and cancellation.
- Pending patterns accept step edits, row clears, bulk toggles and paste.
- A -> pending B -> pending C activates C; selecting A cancels instead.
- Acknowledging an older selection cannot clear the latest request.
- Selection failure leaves UI state unchanged; response backpressure retains commands.
- Tests cover immediate activation, deferred activation and repeated cancellation.

No persistence, machine replacement, new pattern-length policy, or transport
scheduling changes are included.
