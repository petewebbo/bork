#include "ui/grid_tui.hpp"
#include "audio/sequencer.hpp"
#undef NDEBUG
#include <cassert>

namespace {
struct fixture {
    bork::pattern_store store;
    bork::sequencer audio;
    bork::GridWidget ui;
    explicit fixture(const bork::pattern_storage& initial = {})
        : store(initial), audio(store.exchange_, store.initial_snapshot()), ui(store) { flush(); }
    void flush() {
        for (int pass = 0; pass < 6; ++pass) {
            store.pump();
            audio.process_commands();
            audio.publish_status(store.audio_status_, bork::GridWidget::ROWS);
            store.pump();
            ui.poll_notifications();
        }
    }
    const bork::pattern& editing() const { return ui.editing_copy(); }
};
}

void test_selection_clipboard() {
    {
        bork::pattern_storage initial{};
        initial[0].tracks_[0].steps_[1] = {.note_ = 67, .vel_ = 81, .len_ = 9};
        initial[0].tracks_[0].steps_[2] = {.note_ = 72, .vel_ = 90};
        fixture f(initial);
        f.ui.set_cursor(0, 1);
        assert(f.ui.input_mode_key('c') && f.ui.input_mode_key('s'));
        f.ui.set_cursor(0, 3);
        assert(f.ui.paste());
        f.flush();
        assert(f.editing().tracks_[0].steps_[3].note_ == 67);
        assert(f.editing().tracks_[0].steps_[3].vel_ == 81);
        assert(f.editing().tracks_[0].steps_[3].len_ == 9);
        assert(f.editing().tracks_[0].steps_[4].note_ == 0);
        assert(f.ui.input_mode_key('P') && f.ui.input_mode_key(27));
        assert(!f.ui.awaiting_pattern_ && f.ui.selected_pattern_ == 0);
    }
    {
        fixture f;
        f.ui.set_cursor(2, 5);
        f.ui.select_to_edge(0, 1);
        auto area = f.ui.selection();
        assert(f.ui.current_col_ == 15 && area.left == 5 && area.right == 15);
        assert(area.top == 2 && area.bottom == 2);
        f.ui.select_to_edge(0, -1);
        area = f.ui.selection();
        assert(f.ui.current_col_ == 0 && area.left == 0 && area.right == 5);
        f.ui.select_to_edge(-1, 0);
        area = f.ui.selection();
        assert(f.ui.current_row_ == 0 && area.top == 0 && area.bottom == 2);
        f.ui.select_to_edge(1, 0);
        area = f.ui.selection();
        assert(f.ui.current_row_ == 4 && area.top == 2 && area.bottom == 4);
        f.ui.set_cursor(2, 5);
        f.ui.move_cursor(0, -1, true);
        f.ui.select_to_edge(0, 1);
        assert(f.ui.selection().left == 5 && f.ui.selection().right == 15);
        f.ui.set_cursor(0, 0);
        f.ui.select_to_edge(-1, 0);
        assert(!f.ui.selection().multiple() && f.ui.current_row_ == 0);
    }
    {
        fixture f;
        assert(!f.ui.paste() && f.store.outstanding_requests() == 0);
        f.ui.set_cursor(2, 3);
        f.ui.move_cursor(-1, -2, true);
        auto area = f.ui.selection();
        assert(area.top == 1 && area.bottom == 2 && area.left == 1 && area.right == 3);
        f.ui.move_cursor(2, 3, true);
        area = f.ui.selection();
        assert(area.top == 2 && area.bottom == 3 && area.left == 3 && area.right == 4);
        f.ui.move_cursor(99, 99, true);
        assert(f.ui.current_row_ == 4 && f.ui.current_col_ == 15);
        f.ui.move_cursor(0, -1);
        assert(!f.ui.selection().multiple());
        f.ui.move_cursor(-99, -99, true);
        area = f.ui.selection();
        assert(area.top == 0 && area.left == 0 && area.bottom == 4 && area.right == 14);
        f.ui.set_cursor(0, 0);
        f.ui.move_cursor(0, 2, true);
        assert(f.ui.selection().bottom == 0 && f.ui.selection().right == 2);
        assert(f.ui.toggle_selection());
        assert(f.editing().tracks_[0].steps_[2].note_ == 60);
        assert(f.store.original(0).tracks_[0].steps_[2].note_ == 0);
        assert(f.audio.current_pattern().tracks_[0].steps_[2].note_ == 0);
        f.flush();
        assert(f.audio.current_pattern().tracks_[0].steps_[2].note_ == 60);
        f.ui.copy_selection();
        f.ui.set_cursor(4, 15);
        assert(f.ui.paste());
        f.flush();
        assert(f.editing().tracks_[4].steps_[15].note_ == 60);
        assert(f.editing().tracks_[4].steps_[14].note_ == 0);
        assert(f.editing().tracks_[5].steps_[0].note_ == 0);
    }
    {
        fixture f;
        assert(f.ui.togglecell());
        f.ui.move_cursor(1, 1, true);
        assert(f.ui.toggle_selection());
        assert(f.editing().tracks_[0].steps_[0].note_ == 0);
        assert(f.editing().tracks_[1].steps_[1].note_ == 60);
        f.flush();
        assert(f.ui.clear_selection());
        assert(f.ui.selection().multiple());
        assert(f.editing().tracks_[1].steps_[1].note_ == 0);
        assert(f.editing().tracks_[1].steps_[1].vel_ == 100);
        f.flush();
        assert(f.ui.toggle_selection());
        f.flush();
        f.ui.set_cursor(1, 1);
        assert(f.ui.clear_selection());
        assert(f.editing().tracks_[1].steps_[0].note_ == 0);
        assert(f.editing().tracks_[0].steps_[0].note_ == 60);
    }
    {
        bork::pattern_storage initial{};
        initial[0].tracks_[0].steps_[0] = {.note_ = 67, .vel_ = 81, .len_ = 9};
        initial[0].tracks_[7].steps_[15] = {.note_ = 74, .vel_ = 99, .len_ = 13};
        initial[0].tracks_[7].size_ = 12;
        fixture f(initial);
        assert(f.ui.input_mode_key('c') && f.ui.awaiting_copy_);
        assert(f.ui.input_mode_key('x') && f.ui.input_mode_key('q'));
        assert(f.store.outstanding_requests() == 0);
        assert(f.ui.input_mode_key('p') && !f.ui.awaiting_copy_);
        assert(!f.ui.awaiting_pattern_);
        f.ui.set_cursor(0, 0);
        assert(f.ui.togglecell()); // Clipboard is an independent snapshot.
        assert(f.ui.select_pattern(1));
        f.ui.copy_selection(); // Loading copy cannot replace the pattern clipboard.
        f.ui.set_cursor(4, 15);
        assert(!f.ui.paste());
        f.flush();
        assert(f.ui.paste());
        f.flush();
        assert(f.audio.pending_ && f.audio.pending_->pattern_id == 1);
        assert(f.audio.pending_->value->tracks_[7].steps_[15].note_ == 74);
        assert(f.store.original(1).tracks_[7].size_ == 12);
        assert(f.store.original(1).tracks_[0].steps_[0].len_ == 9);
        assert(f.ui.current_row_ == 4 && f.ui.current_col_ == 15);
        f.ui.set_cursor(0, 0);
        assert(f.ui.togglecell() && f.ui.togglecell());
        assert(f.ui.note_number(0, 0) == 67);
        f.flush();
        assert(f.ui.input_mode_key('c') && f.ui.input_mode_key(27));
        assert(!f.ui.awaiting_copy_ && f.ui.paste());
        f.flush();
        // All eight remembered-note rows are replaced, including hidden tracks.
        assert(f.store.notes(1)[7][15] == 74);
        assert(f.store.notes(1)[7][14] == 60);
    }
    {
        bork::pattern_storage initial{};
        initial[0].tracks_[0].steps_[0] = {.note_ = 63, .vel_ = 77, .len_ = 8};
        initial[0].tracks_[0].steps_[2] = {.note_ = 79, .vel_ = 50};
        initial[0].tracks_[3].steps_[13] = {.note_ = 82, .vel_ = 70};
        fixture f(initial);
        f.ui.move_cursor(0, 1, true);
        assert(f.ui.input_mode_key('c') && f.ui.input_mode_key('s'));
        f.ui.set_cursor(0, 1);
        assert(f.ui.paste());
        assert(f.editing().tracks_[0].steps_[1].note_ == 63);
        assert(f.editing().tracks_[0].steps_[1].len_ == 8);
        assert(f.editing().tracks_[0].steps_[2].note_ == 0);
        f.flush();
        assert(f.store.notes(0)[0][2] == 60); // Pasted empty replaces remembered pitch.
        f.ui.set_cursor(0, 2);
        assert(f.ui.togglecell() && f.ui.note_number(0, 2) == 60);
        f.ui.set_cursor(3, 14);
        assert(f.ui.paste());
        f.flush();
        assert(f.editing().tracks_[3].steps_[14].vel_ == 77);
        assert(f.editing().tracks_[3].steps_[15].note_ == 0);
        assert(f.store.notes(0)[3][13] == 82); // Untouched destination metadata is preserved.
        f.ui.move_cursor(-1, -1, true);
        assert(f.ui.select_pattern(2));
        assert(!f.ui.selection().multiple());
        f.ui.move_cursor(-1, -1, true);
        f.flush();
        assert(f.ui.selection().multiple());
        assert(f.ui.select_pattern(0));
        f.flush();
        f.ui.set_cursor(3, 13);
        assert(f.ui.clearrow() && f.ui.togglecell());
        assert(f.ui.note_number(3, 13) == 82); // Metadata returns with an editing selection.
    }
    {
        fixture f;
        f.ui.copy_pattern();
        for (size_t i = 0; i < 64; ++i) assert(f.ui.paste());
        f.ui.move_cursor(1, 1, true);
        assert(!f.ui.toggle_selection() && !f.ui.clear_selection());
        assert(f.editing().tracks_[0].steps_[0].note_ == 0);
        assert(f.editing().tracks_[1].steps_[1].note_ == 0);
        f.store.pump();
        assert(!f.ui.toggle_selection());
        f.ui.poll_notifications();
        assert(f.ui.toggle_selection());
        f.flush();
        assert(f.store.original(0).tracks_[1].steps_[1].note_ == 60);
    }
    {
        // An invalid imported length makes whole-paste reject atomically. A valid
        // later rectangle must not replay the unrelated optimistic pasted cells.
        bork::pattern_storage initial{};
        initial[0].tracks_[7].size_ = bork::MAX_PATTERN_STEPS + 1;
        initial[0].tracks_[0].steps_[0] = {.note_ = 67, .vel_ = 90};
        initial[1].tracks_[0].steps_[0] = {.note_ = 74, .vel_ = 80};
        fixture f(initial);
        f.ui.copy_pattern();
        assert(f.ui.select_pattern(1));
        f.flush();
        assert(f.ui.paste());
        assert(f.ui.note_number(0, 0) == 67);
        f.ui.set_cursor(1, 1);
        assert(f.ui.togglecell());
        f.store.pump();
        assert(f.store.original(1).tracks_[0].steps_[0].note_ == 74);
        assert(f.store.original(1).tracks_[1].steps_[1].note_ == 60);
        f.ui.poll_notifications();
        assert(f.ui.loading());
        assert(!f.ui.togglecell() && !f.ui.clearrow() && !f.ui.paste());
        assert(!f.ui.click_cell(0, 0));
        f.ui.copy_pattern(); // Recovery also guards copy, preserving the invalid clipboard.
        f.store.pump(); // Barrier recovers without another audio callback.
        f.ui.poll_notifications();
        assert(!f.ui.loading());
        assert(f.ui.note_number(0, 0) == 74 && f.ui.note_number(1, 1) == 60);
        f.ui.set_cursor(0, 0);
        assert(f.ui.clearrow() && f.ui.togglecell());
        assert(f.ui.note_number(0, 0) == 74);
        f.flush();
        assert(f.ui.paste()); // Rejection can start a second barrier in the same session.
        f.store.pump();
        f.ui.poll_notifications();
        assert(f.ui.loading());
        f.store.pump(); // Recovery snapshot is queued before a newer editing selection.
        assert(f.ui.select_pattern(2));
        f.store.pump();
        f.ui.poll_notifications();
        assert(f.ui.loading() && f.ui.selected_pattern_ == 2);
        f.store.pump();
        f.ui.poll_notifications();
        assert(!f.ui.loading() && f.ui.loaded_pattern_ == 2);
        assert(f.ui.note_number(0, 0) == 0);
        assert(f.ui.select_pattern(1));
        f.flush();
        assert(f.ui.note_number(0, 0) == 74 && f.ui.note_number(1, 1) == 60);
    }
}
