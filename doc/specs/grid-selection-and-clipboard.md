# Grid selection and clipboard

Status: implemented.
Lowercase `c` enters copy mode and lowercase `v` pastes.
There is no clear keybinding; uppercase `C` and `V` are unbound.

## Scope

Rectangular grid selection and an internal application clipboard operate on the
UI's one editing copy. Accepted operation-scoped edits go to the dedicated store.
No system clipboard integration or cross-process data.

## Selection

- Normal arrow movement moves the cursor and collapses selection to that cell.
- Shift + arrow starts or extends a rectangular selection.
- Ctrl + Shift + arrow moves to the grid edge in that direction and selects
  everything between anchor and endpoint. Right/left goes to last/first step;
  down/up goes to last/first visible track. Keep the existing anchor, or start
  one at the cursor if no selection has been started.
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
- Requesting another editing pattern resets selection to the current cursor cell;
  audio status changes do not reset it again.
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

- Lowercase `c` enters copy mode. The persistent top status bar displays:
  `Copy: p = pattern | s = selection | Esc = cancel`.
- While awaiting that choice, input does not edit cells, move the cursor, toggle
  playback or enter pattern selection. Continue polling store responses and status.
- `p` copies the entire displayed stored pattern, including tracks not currently
  visible in the grid. This is an application snapshot, not an audio-state read.
- `s` copies the inclusive selected rectangle. With no multi-cell selection this
  copies the current cell.
- A valid choice replaces the clipboard and exits copy mode. Report the copied
  dimensions/type in the status bar.
- Escape exits copy mode without changing the clipboard or quitting.
- Other keys leave the prompt active. Lowercase `p` in this mode means copy
  pattern, not enter the pattern-selection prompt.
- There is no clear keybinding. Uppercase `C` and `V` are unbound.

## Selection editing

- `x` toggles every cell in the selected rectangle independently: active cells
  become empty, and empty cells become active using their remembered/default
  note and velocity. Without multi-selection this edits only the cursor cell.
- Toggling does not collapse the selection or move the cursor.
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

- Lowercase `v`, outside prompted modes, pastes the clipboard into the displayed
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

- UI submits desired destination values to the store. Region operations change
  only their footprint; whole-pattern paste intentionally replaces all contents.
- Store validates and commits pattern and remembered-note changes atomically.
  All accepted edits persist in the named original, whether playing or not.
- UI updates optimistically only after successful request admission. Queue/credit
  exhaustion leaves data unchanged; clipboard and selection survive failed paste.
- Store publishes immutable full-pattern snapshot references to audio. Audio
  adopts references before rendering chunks; no pattern copies happen in audio.
- If a switch is pending, paste targets the loaded editing pattern, not actual
  audio current. A newer snapshot updates current/pending without another selection.
- Loading/recovery disables edits and copy actions; further selections remain
  available. Rejected edits use an authoritative FIFO resync barrier.
- Copy and selection movement require no audio commands.
- See `2026-10-19-new-architecture.md` for channel capacities, immutable-buffer
  lifetime, backpressure and UI/store recovery. The former four-payload/marker
  protocol and callback-side copies are removed.

## Confirmed decisions

- Whole-pattern paste replaces the displayed pattern at its origin. Copy/paste
  includes all pattern data and hidden tracks; rectangular selection remains
  limited to visible cells.
- Normal movement/click collapses selection; Shift movement extends its anchor.
- `x` toggles the entire selection. Lowercase `c` opens copy mode; lowercase `v` pastes.
- Single-cell active cursor stays dark grey; multi-selection uses light grey
  with an underline distinguishing the cursor.

## Acceptance tests

- Shift movement produces correct extents in every direction, including crossing
  the anchor, shrinking, edge clamping, row/column selection and a single cell.
- Ctrl+Shift movement reaches all four edges without wrapping, preserves an
  existing Shift-selection anchor, and handles movement already at an edge.
- Normal movement, click and pattern selection reset selection as specified.
- Selected empty/active cells render consistently; cursor remains distinguishable.
- Copy prompt routes p/s/Escape correctly without editing or exiting the app.
- Whole-pattern and rectangular copies are independent snapshots of all fields.
- Selection paste at every edge clips correctly without wrapping; empty copied
  cells clear destinations and repeated/overlapping paste works.
- Whole-pattern paste includes hidden tracks and leaves runtime state unchanged.
- Clipboard survives pattern changes, cancellation, empty paste and failed enqueue.
- Selection toggle handles mixed active/empty cells independently.
- Lowercase c enters copy mode; lowercase v pastes. Opening copy mode preserves
  existing notes. Uppercase C and V have no binding.
- Queue exhaustion cannot partially update UI or store patterns for paste or
  selection toggle.
- Paste following activation affects the intended pattern ID in FIFO order,
  including after switching away and back to the same pattern.
- Exercise shifted arrows and status prompts in the actual target terminal,
  including resize while copy mode/selection is active.
