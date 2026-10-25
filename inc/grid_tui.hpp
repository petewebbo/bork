#pragma once

#include "grid_selection.hpp"
#include "pattern_store.hpp"
#include <array>
#include <cstddef>
#include <cstdio>

namespace bork {

// UI-thread only: one editing copy, its metadata, and a deliberate clipboard.
class GridWidget {
public:
    static constexpr uint32_t kDefaultNoteNumber = DEFAULT_NOTE_NUMBER;
    static constexpr size_t ROWS = 5;
    static constexpr size_t COLS = MAX_PATTERN_STEPS;
    enum class edit_field { note, velocity, length };

    explicit GridWidget(pattern_store& store) : store_(store) {
        desired_playing_ = store_.ui_status_.playing.load(std::memory_order_relaxed);
        select_pattern(0);
    }
    bool loading() const { return session_ == 0 || loaded_session_ != session_ || recovering_; }
    const pattern& editing_copy() const { return editing_; }
    grid_extents selection() const { return selection_.extents(current_row_, current_col_); }
    void move_cursor(int dy, int dx, bool extend = false) {
        selection_.move(current_row_, current_col_, dy, dx, extend, ROWS, COLS);
    }
    void select_to_edge(int dy, int dx) {
        move_cursor(dy * static_cast<int>(ROWS), dx * static_cast<int>(COLS), true);
    }
    void set_cursor(size_t row, size_t col) {
        current_row_ = std::min(row, ROWS - 1);
        current_col_ = std::min(col, COLS - 1);
        selection_.reset();
    }
    bool click_cell(size_t row, size_t col) {
        if (!editable() || row >= ROWS || col >= COLS) return false;
        set_cursor(row, col);
        return togglecell();
    }
    bool input_mode_key(int key) {
        if (awaiting_edit_) {
            if (key == 27) awaiting_edit_ = false;
            else if (key == 'n' || key == 'v' || key == 'l') {
                edit_field_ = key == 'n' ? edit_field::note
                    : key == 'v' ? edit_field::velocity : edit_field::length;
                awaiting_edit_ = false;
            }
            return true;
        }
        if (awaiting_pattern_) return pattern_input(key);
        if (awaiting_copy_) {
            if (key == 27) { awaiting_copy_ = false; message_ = nullptr; }
            else if (key == 'p' || key == 's') {
                awaiting_copy_ = false;
                if (key == 'p') copy_pattern(); else copy_selection();
            }
            return true;
        }
        if (key == 'c') {
            if (editable()) awaiting_copy_ = true;
            return true;
        }
        if (key == 'e') {
            awaiting_edit_ = true;
            message_ = nullptr;
            return true;
        }
        return pattern_input(key);
    }
    // Prompted input must not reach editing/playback/quit controls.
    bool pattern_input(int key) {
        if (!awaiting_pattern_) {
            if (key != 'p' && key != 'P') return false;
            awaiting_pattern_ = true;
            return true;
        }
        if (key == 27) awaiting_pattern_ = false;
        else if (key >= '1' && key <= '8') {
            awaiting_pattern_ = false;
            select_pattern(static_cast<size_t>(key - '1'));
        }
        return true;
    }
    bool select_pattern(size_t id) {
        if (id >= NUM_STORED_PATTERNS) return false;
        if (session_ && id == selected_pattern_) return true;
        store_request request{};
        request.type = store_request::kind::select;
        request.session = session_ + 1;
        request.pattern_id = static_cast<uint8_t>(id);
        if (!enqueue(request)) return false;
        session_ = request.session;
        selection_request_ = request_id_;
        selected_pattern_ = id;
        recovering_ = false;
        resync_request_ = 0;
        recovered_through_ = 0;
        awaiting_copy_ = false;
        selection_.reset();
        return true;
    }
    void poll_notifications() {
        store_result result{};
        while (store_.try_result(result)) {
            if (result.session != session_) continue;
            if (!result.accepted && result.type == store_request::kind::edit &&
                result.request_id > recovered_through_) {
                recovering_ = true;
                message_ = "Edit rejected - restoring authoritative copy";
            }
        }
        editing_snapshot snapshot{};
        while (store_.try_snapshot(snapshot)) {
            if (snapshot.session != session_ || snapshot.pattern_id != selected_pattern_) continue;
            const bool matching_recovery = recovering_ && resync_request_ && snapshot.recovery &&
                snapshot.request_id == resync_request_;
            const bool matching_selection = !recovering_ && !snapshot.recovery &&
                snapshot.request_id == selection_request_ && loaded_session_ != session_;
            if (!matching_recovery && !matching_selection) continue;
            editing_ = snapshot.value;
            notes_ = snapshot.notes;
            loaded_session_ = session_;
            loaded_pattern_ = snapshot.pattern_id;
            recovering_ = false;
            resync_request_ = 0;
            message_ = matching_recovery ? "Editing copy restored" : nullptr;
            refresh_grid();
        }
        if (recovering_ && !resync_request_) {
            store_request request{};
            request.type = store_request::kind::resync;
            request.session = session_;
            request.pattern_id = static_cast<uint8_t>(selected_pattern_);
            if (enqueue(request)) {
                resync_request_ = request_id_;
                // All earlier rejections are covered by this FIFO barrier.
                recovered_through_ = resync_request_;
                message_ = "Edit rejected - restoring authoritative copy";
            }
        }
        if (!session_) select_pattern(0); // Initial admission can be retried too.
        playing_pattern_ = static_cast<size_t>(store_.ui_status_.current_pattern.load(std::memory_order_relaxed));
        switch_pending_ = store_.ui_status_.pending_pattern.load(std::memory_order_relaxed) >= 0;
    }
    bool toggleplay() {
        store_request request{};
        request.type = desired_playing_ ? store_request::kind::stop : store_request::kind::start;
        if (!enqueue(request)) return false;
        desired_playing_ = !desired_playing_;
        return true;
    }
    bool togglecell() {
        if (!editable()) return false;
        return toggle_area({current_row_, current_row_, current_col_, current_col_});
    }
    bool clearrow() {
        if (!editable()) return false;
        return clear_area({current_row_, current_row_, 0, COLS - 1});
    }
    bool toggle_selection() {
        if (!editable()) return false;
        return toggle_area(selection());
    }
    bool clear_selection() {
        if (!editable()) return false;
        const auto area = selection();
        return area.multiple() ? clear_area(area) : clearrow();
    }
    void copy_pattern() {
        if (!editable()) return;
        clipboard_ = editing_;
        clipboard_type_ = clipboard_type::pattern;
        message_ = "Copied entire pattern (including hidden tracks)";
    }
    void copy_selection() {
        if (!editable()) return;
        const auto area = selection();
        clipboard_ = {};
        clipboard_rows_ = area.bottom - area.top + 1;
        clipboard_cols_ = area.right - area.left + 1;
        for (size_t row = 0; row < clipboard_rows_; ++row)
            for (size_t col = 0; col < clipboard_cols_; ++col)
                clipboard_.tracks_[row].steps_[col] = editing_.tracks_[area.top + row].steps_[area.left + col];
        clipboard_type_ = clipboard_type::selection;
        std::snprintf(feedback_.data(), feedback_.size(), "Copied selection: %zu rows x %zu steps",
                      clipboard_rows_, clipboard_cols_);
        message_ = area.multiple() ? feedback_.data() : "Copied step";
    }
    bool paste() {
        if (!editable()) return false;
        if (clipboard_type_ == clipboard_type::empty) { message_ = "Clipboard empty"; return false; }
        pattern_edit edit{};
        if (clipboard_type_ == clipboard_type::pattern) {
            edit.type = pattern_edit::kind::paste_pattern;
            edit.value = clipboard_;
        } else {
            edit.type = pattern_edit::kind::paste_region;
            edit.top = current_row_;
            edit.left = current_col_;
            edit.rows = std::min(clipboard_rows_, ROWS - current_row_);
            edit.cols = std::min(clipboard_cols_, COLS - current_col_);
            for (size_t row = 0; row < edit.rows; ++row)
                for (size_t col = 0; col < edit.cols; ++col)
                    edit.value.tracks_[edit.top + row].steps_[edit.left + col] = clipboard_.tracks_[row].steps_[col];
        }
        if (!submit_edit(edit)) return false;
        message_ = "Pasted";
        return true;
    }
    int note_number(size_t row, size_t col) const { return editing_.tracks_[row].steps_[col].note_; }
    int displayed_value(size_t row, size_t col) const {
        const auto& step = editing_.tracks_[row].steps_[col];
        switch (edit_field_) {
        case edit_field::note: return step.note_;
        case edit_field::velocity: return step.vel_;
        case edit_field::length: return step.len_;
        }
        return 0;
    }
    std::array<std::array<bool, COLS>, ROWS> grid_state_{};
    size_t current_row_{};
    size_t current_col_{};
    size_t selected_pattern_{};
    size_t loaded_pattern_{NUM_STORED_PATTERNS};
    size_t playing_pattern_{};
    bool switch_pending_{};
    bool awaiting_pattern_{};
    bool awaiting_copy_{};
    bool awaiting_edit_{};
    edit_field edit_field_{edit_field::note};
    bool queue_full_{};
    const char* message_{};
private:
    bool editable() {
        if (!loading()) return true;
        message_ = "Loading editing copy - edits and copy disabled";
        return false;
    }
    static pattern_edit region_edit(grid_extents area) {
        pattern_edit edit{};
        edit.top = area.top;
        edit.left = area.left;
        edit.rows = area.bottom - area.top + 1;
        edit.cols = area.right - area.left + 1;
        return edit;
    }
    bool toggle_area(grid_extents area) {
        auto edit = region_edit(area);
        for (size_t row = area.top; row <= area.bottom; ++row)
            for (size_t col = area.left; col <= area.right; ++col) {
                auto step = editing_.tracks_[row].steps_[col];
                if (step.note_) step.note_ = 0;
                else {
                    step.note_ = notes_[row][col];
                    if (step.vel_ <= 0) step.vel_ = 100;
                }
                edit.value.tracks_[row].steps_[col] = step;
            }
        return submit_edit(edit);
    }
    bool clear_area(grid_extents area) {
        auto edit = region_edit(area);
        for (size_t row = area.top; row <= area.bottom; ++row)
            for (size_t col = area.left; col <= area.right; ++col) {
                edit.value.tracks_[row].steps_[col] = editing_.tracks_[row].steps_[col];
                edit.value.tracks_[row].steps_[col].note_ = 0;
            }
        return submit_edit(edit);
    }
    bool submit_edit(const pattern_edit& edit) {
        store_request request{};
        request.type = store_request::kind::edit;
        request.session = session_;
        request.pattern_id = static_cast<uint8_t>(selected_pattern_);
        request.edit = edit;
        if (!enqueue(request)) return false;
        apply_pattern_edit(editing_, notes_, edit);
        refresh_grid();
        return true;
    }
    void refresh_grid() {
        for (size_t row = 0; row < ROWS; ++row)
            for (size_t col = 0; col < COLS; ++col)
                grid_state_[row][col] = note_number(row, col) != 0;
    }
    bool enqueue(store_request& request) {
        request.request_id = request_id_ + 1;
        queue_full_ = !store_.try_submit(request);
        message_ = queue_full_ ? "Edit queue full - edit not applied; try again" : nullptr;
        if (!queue_full_) ++request_id_;
        return !queue_full_;
    }
    pattern_store& store_;
    pattern editing_{};
    remembered_notes notes_{};
    uint64_t request_id_{}, session_{}, loaded_session_{}, selection_request_{};
    uint64_t resync_request_{}, recovered_through_{};
    bool recovering_{}, desired_playing_{};
    grid_selection selection_;
    enum class clipboard_type { empty, selection, pattern };
    clipboard_type clipboard_type_{clipboard_type::empty};
    pattern clipboard_{};
    size_t clipboard_rows_{}, clipboard_cols_{};
    std::array<char, 96> feedback_{};
};

void run_grid_tui(GridWidget& widget, const sequencer_status& status);

} // namespace bork
