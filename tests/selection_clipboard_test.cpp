#include <grid_tui.hpp>
#include <sequencer.hpp>
#undef NDEBUG
#include <cassert>

struct fixture {
    bork::sequencer audio;
    bork::pattern_storage storage{};
    bork::command_queue commands{32};
    bork::GridWidget ui{storage, commands, audio._patterns};
    void flush() { audio.process_commands(commands); ui.poll_notifications(); }
};

int main() {
    {
        fixture f;
        assert(!f.ui.paste() && f.commands.empty());
        f.ui.set_cursor(2, 3);
        f.ui.move_cursor(-1, -2, true);
        auto area = f.ui.selection();
        assert(area.top == 1 && area.bottom == 2 && area.left == 1 && area.right == 3);
        f.ui.move_cursor(2, 3, true); // Cross the anchor.
        area = f.ui.selection();
        assert(area.top == 2 && area.bottom == 3 && area.left == 3 && area.right == 4);
        f.ui.move_cursor(99, 99, true);
        assert(f.ui.current_row_ == 4 && f.ui.current_col_ == 15);
        f.ui.move_cursor(0, -1); // Normal movement collapses.
        assert(!f.ui.selection().multiple());
        f.ui.move_cursor(-99, -99, true);
        area = f.ui.selection();
        assert(area.top == 0 && area.left == 0 && area.bottom == 4 && area.right == 14);
        f.ui.set_cursor(0, 0);
        f.ui.move_cursor(0, 2, true); // Row selection.
        assert(f.ui.selection().bottom == 0 && f.ui.selection().right == 2);
        assert(f.ui.toggle_selection());
        assert(f.storage[0]._tracks[0]._steps[2]._note == 60);
        assert(f.audio.current_pattern()._tracks[0]._steps[2]._note == 0);
        f.flush();
        assert(f.audio.current_pattern()._tracks[0]._steps[2]._note == 60);
        f.ui.copy_selection();
        f.ui.set_cursor(4, 15);
        assert(f.ui.paste()); // 1x3 clipped to 1x1.
        f.flush();
        assert(f.storage[0]._tracks[4]._steps[15]._note == 60);
        assert(f.storage[0]._tracks[4]._steps[14]._note == 0);
        assert(f.storage[0]._tracks[5]._steps[0]._note == 0);
    }
    {
        fixture f;
        // A mixed selection: toggle independently; clearing preserves metadata.
        f.ui.togglecell();
        f.ui.move_cursor(1, 1, true);
        assert(f.ui.toggle_selection());
        assert(f.storage[0]._tracks[0]._steps[0]._note == 0);
        assert(f.storage[0]._tracks[1]._steps[1]._note == 60);
        f.flush();
        assert(f.ui.clear_selection());
        assert(f.ui.selection().multiple());
        assert(f.storage[0]._tracks[1]._steps[1]._note == 0);
        assert(f.storage[0]._tracks[1]._steps[1]._vel == 100);
        f.flush();
        assert(f.ui.toggle_selection());
        f.flush();
        f.ui.set_cursor(1, 1);
        assert(f.ui.clear_selection()); // No multi-select: clears entire row.
        assert(f.storage[0]._tracks[1]._steps[0]._note == 0);
        assert(f.storage[0]._tracks[0]._steps[0]._note == 60);
    }
    {
        fixture f;
        f.storage[0]._tracks[0]._steps[0] = {._note = 67, ._vel = 81, ._len = 9};
        f.storage[0]._tracks[7]._steps[15] = {._note = 74, ._vel = 99, ._len = 13};
        f.storage[0]._tracks[7]._size = 12;
        assert(f.ui.input_mode_key('C') && f.ui.awaiting_copy_);
        assert(f.ui.input_mode_key('x') && f.ui.input_mode_key('q'));
        assert(f.commands.empty());
        assert(f.ui.input_mode_key('p') && !f.ui.awaiting_copy_);
        assert(!f.ui.awaiting_pattern_);
        // Pattern clipboard is a snapshot, not a reference.
        f.storage[0]._tracks[7]._steps[15]._note = 1;
        assert(f.ui.select_pattern(1));
        f.ui.set_cursor(4, 15);
        assert(f.ui.paste()); // Queued after activation; replaces whole pattern at origin.
        f.flush();
        assert(f.audio._pattern_id == 1);
        assert(f.audio.current_pattern()._tracks[7]._steps[15]._note == 74);
        assert(f.storage[1]._tracks[7]._size == 12);
        assert(f.storage[1]._tracks[0]._steps[0]._len == 9);
        assert(f.ui.current_row_ == 4 && f.ui.current_col_ == 15);
        f.ui.set_cursor(0, 0);
        assert(f.ui.togglecell() && f.ui.togglecell()); // Pasted note remembered.
        assert(f.ui.note_number(0, 0) == 67);
        f.flush();
        assert(f.ui.input_mode_key('C') && f.ui.input_mode_key(27));
        assert(!f.ui.awaiting_copy_);
        assert(f.ui.paste()); // Cancel leaves clipboard unchanged.
        f.flush();
    }
    {
        fixture f;
        // Includes empty cells and all fields; overlap does not change source snapshot.
        f.storage[0]._tracks[0]._steps[0] = {._note = 63, ._vel = 77, ._len = 8};
        f.ui.move_cursor(0, 1, true);
        assert(f.ui.input_mode_key('C') && f.ui.input_mode_key('s'));
        f.ui.set_cursor(0, 1);
        assert(f.ui.paste());
        assert(f.storage[0]._tracks[0]._steps[1]._note == 63);
        assert(f.storage[0]._tracks[0]._steps[1]._len == 8);
        assert(f.storage[0]._tracks[0]._steps[2]._note == 0);
        f.flush();
        f.ui.set_cursor(3, 14);
        assert(f.ui.paste());
        f.flush();
        assert(f.storage[0]._tracks[3]._steps[14]._vel == 77);
        assert(f.storage[0]._tracks[3]._steps[15]._note == 0);
        // Selection resets on pattern selection, not on later acknowledgement.
        f.ui.move_cursor(-1, -1, true);
        assert(f.ui.select_pattern(2));
        assert(!f.ui.selection().multiple());
        f.ui.move_cursor(-1, -1, true);
        f.flush();
        assert(f.ui.selection().multiple());
    }
    {
        fixture f;
        f.ui.copy_pattern();
        for (size_t i = 0; i < bork::PATTERN_EDIT_QUEUE_CAPACITY; ++i) assert(f.ui.paste());
        f.ui.move_cursor(1, 1, true);
        assert(!f.ui.toggle_selection()); // Payload queue full: no partial changes.
        assert(!f.ui.clear_selection());
        assert(f.storage[0]._tracks[0]._steps[0]._note == 0);
        f.audio.process_commands(f.commands); // No completion polling needed for reuse.
        assert(f.ui.toggle_selection());
        f.flush();
        bork::command_queue full(1);
        bork::pattern_exchange exchange;
        bork::GridWidget blocked(f.storage, full, exchange);
        blocked.copy_pattern();
        assert(full.try_push({._type = bork::sequencer_command::type::toggle_play}));
        assert(!blocked.paste());
        assert(blocked.note_number(0, 0) == 60);
        assert(full.front());
        full.pop(); // Test-only consumer frees capacity.
        assert(exchange.edits.empty()); // Rejected submission published no payload.
        assert(blocked.paste());
    }
}
