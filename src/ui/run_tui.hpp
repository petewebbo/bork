#pragma once

namespace bork {
class EditorState;
struct sequencer_status;

void run_tui(EditorState& editor, const sequencer_status& status);
} // namespace bork
