# Grid selection and clipboard

Status: implemented.
Uppercase `C` enters copy mode, uppercase `V` pastes, and lowercase `c`
clears the selection, or the current row when no multi-cell selection exists.

## Scope

Add rectangular grid selection and an internal application clipboard. Copy/paste
operates on the UI-owned pattern storage and sends accepted changes through the
existing command queue. No system clipboard integration or cross-process data.

## Selection

- Normal arrow movement moves the cursor and collapses selection to that cell.
- Shift + arrow starts or extends a rectangular selection.
- The anchor is the cursor position before the first Shift movement; the cursor
  is the moving endpoint. Reversing movement shrinks or crosses the anchor.
- Selection is defined by inclusive extents:
  `top = min(anchor.row, cursor.row)`, `bottom = max(...)`, and equivalent
  `left`/`right`. It can therefore be a single cell, row, column or rectangle.
- Movement clamps at the existing grid edges; selection never wraps.
- Releasing Shift does not clear the selection. A later Shift + arrow continues
  from the same anchor until normal movement or another reset action.
- Clicking a grid cell collapses selection to that cell and retains the current
  click-to-toggle behaviour. Shift-click selection is outside this scope.
- Changing the displayed pattern resets selection to the current cursor cell;
  acknowledging an audio switch does not reset it again.
- Selection is limited to the five visible tracks and sixteen visible steps.

### Rendering and input

- Selected cells use a light-grey background with contrasting text, regardless
  of whether a cell contains a note. Include empty cells in the selection.
- Keep the cursor distinguishable inside a multi-cell selection, for example with
  an underline. Preserve dark-grey active-cell cursor styling for a single-cell
  selection; light-grey selection styling takes precedence for multi-selection.
- Selection colour has a basic-colour/monochrome fallback.
- Detect Shift from each arrow event; do not depend on key-release events.
  Ncurses support for shifted arrows varies by terminal/terminfo. Register known
  extended keys where available and document/test supported terminals. Do not
  silently treat unsupported sequences as destructive editing commands.

## Copy command mode

- Uppercase `C` enters copy mode. The persistent top status bar displays:
  `Copy: p = pattern | s = selection | Esc = cancel`.
- While awaiting that choice, input does not edit cells, move the cursor, toggle
  playback or enter pattern selection. Continue polling audio acknowledgements
  and runtime status.
- `p` copies the entire displayed stored pattern, including tracks not currently
  visible in the grid. This is an application snapshot, not an audio-state read.
- `s` copies the inclusive selected rectangle. With no multi-cell selection this
  copies the current cell.
- A valid choice replaces the clipboard and exits copy mode. Report the copied
  dimensions/type in the status bar.
- Escape exits copy mode without changing the clipboard or quitting.
- Other keys leave the prompt active. Lowercase `p` in this mode means copy
  pattern, not enter the pattern-selection prompt.
- Lowercase `c` clears the selection/current row outside prompted modes;
  uppercase `C` is reserved for the copy prompt.

## Selection editing

- `x` toggles every cell in the selected rectangle independently: active cells
  become empty, and empty cells become active using their remembered/default
  note and velocity. Without multi-selection this edits only the cursor cell.
- Lowercase `c` clears notes throughout a multi-cell selected rectangle. With no
  multi-cell selection, it clears the entire current row. Empty cells are harmless.
  Preserve velocity, length and remembered notes for re-enabling.
- These actions do not collapse the selection or move the cursor.
- Submit each action as one coherent batch, just like paste. If submission fails,
  no selected cell changes in storage or audio; show retry feedback.

## Clipboard representation

The clipboard owns its data; later source edits or pattern changes do not alter it.

Two variants:

1. **Pattern:** complete `bork::pattern` value, including track metadata currently
   represented by that type. Does not copy transport, voices, machine resources,
   samples, playing state or other runtime engine state.
2. **Selection:** row/column dimensions plus row-major full `step` values.

Selection copies include empty steps and all step fields (note, velocity, length),
not just visible labels or active-note flags. The clipboard persists across
pattern switches and remains reusable after paste.

## Paste

- Uppercase `V`, outside prompted modes, pastes the clipboard into the displayed
  stored pattern. It does not switch patterns or change playback state.
- Empty clipboard: no edits or commands; show `Clipboard empty`.
- Selection clipboard: paste its top-left at the cursor. Ignore the destination
  selection's size. Clip rows and columns at the visible grid's bottom/right
  edges; never wrap, shift the origin or overwrite hidden tracks.
- Copy empty cells as well: they clear corresponding destination steps.
- Pattern clipboard: replace the entire displayed stored pattern at pattern
  origin, independent of cursor/selection, including hidden tracks and metadata.
- Keep cursor and selection extents after paste. Refresh note labels, active-cell
  rendering and remembered notes used for toggling from the pasted data.
- Paste uses a snapshot of the clipboard, so overlapping source/destination is
  naturally safe. Source pattern need not still be selected.

## Command delivery and consistency

- Audio must receive explicit desired step/pattern values tagged with displayed
  pattern ID and activation generation; never read UI storage from the callback.
- If a switch is pending, paste targets the newly displayed/requested pattern,
  not the last acknowledged audio pattern. FIFO ordering preserves activation
  before its subsequent edits.
- Apply a paste coherently at a callback boundary: do not let a large paste be
  partly applied in different callbacks or partly committed on queue-full.
- Use a bounded paste-batch handoff rather than naively enqueueing many
  individual step commands. Prepare the batch on the UI thread,
  publish one command, and return ownership through a completion path. Queue or
  transfer-slot exhaustion rejects the whole paste and leaves storage unchanged.
- Commit the UI storage update only after successful batch submission. Report
  failure with a retry message. Clipboard and selection survive failed paste.
- Audio batch application must be bounded and allocation-free. For current small
  fixed-size patterns, a prepared fixed-capacity batch/full-pattern copy is
  acceptable; document and measure its callback cost. Do not reuse the pattern
  switch spare slot while it is submitted/audio-owned.
- Copy and selection movement require no audio commands.

## Confirmed decisions

- Whole-pattern paste replaces the displayed pattern at its origin. Copy/paste
  includes all pattern data and hidden tracks; rectangular selection remains
  limited to visible cells.
- Normal movement/click collapses selection; Shift movement extends its anchor.
- `x` toggles the entire selection. Lowercase `c` clears a multi-cell selection,
  or the current row otherwise. Uppercase `C` opens copy mode; uppercase `V` pastes.
- Single-cell active cursor stays dark grey; multi-selection uses light grey
  with an underline distinguishing the cursor.

## Acceptance tests

- Shift movement produces correct extents in every direction, including crossing
  the anchor, shrinking, edge clamping, row/column selection and a single cell.
- Normal movement, click and pattern selection reset selection as specified.
- Selected empty/active cells render consistently; cursor remains distinguishable.
- Copy prompt routes p/s/Escape correctly without editing or exiting the app.
- Whole-pattern and rectangular copies are independent snapshots of all fields.
- Selection paste at every edge clips correctly without wrapping; empty copied
  cells clear destinations and repeated/overlapping paste works.
- Whole-pattern paste includes hidden tracks and leaves runtime state unchanged.
- Clipboard survives pattern changes, cancellation, empty paste and failed enqueue.
- Selection toggle handles mixed active/empty cells independently; selection
  clear affects only its extents and preserves note/velocity/length metadata.
- Uppercase C enters copy mode; uppercase V pastes. Lowercase c clears a
  multi-cell selection or the current row when selection is a single cell.
- Queue/slot exhaustion cannot partially update UI or audio patterns for paste,
  selection toggle or selection clear.
- Paste following pending activation affects the intended ID/generation only.
- Exercise shifted arrows and status prompts in the actual target terminal,
  including resize while copy mode/selection is active.
