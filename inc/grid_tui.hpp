#pragma once

#include "pattern_exchange.hpp"
#include "grid_selection.hpp"
#include "sequencer_command.hpp"
#include "sequencer_status.hpp"
#include <array>
#include <cstddef>
#include <cstdio>

namespace bork {

// UI-thread only. Storage is never accessed by the audio thread.
class GridWidget {
public:
    static constexpr uint32_t kDefaultNoteNumber = 60;
    static constexpr size_t ROWS = 5;
    static constexpr size_t COLS = MAX_PATTERN_STEPS;

    GridWidget(pattern_storage& storage, command_queue& commands, pattern_exchange& exchange)
        : storage_(storage), commands_(commands), exchange_(exchange) {
        for (size_t id = 0; id < NUM_STORED_PATTERNS; ++id)
            for (size_t row = 0; row < ROWS; ++row)
                for (size_t col = 0; col < COLS; ++col) {
                    const int note = storage_[id]._tracks[row]._steps[col]._note;
                    notes_[id][row][col] = note != 0 ? note : kDefaultNoteNumber;
                }
        refresh_grid();
    }
    grid_extents selection() const { return selection_.extents(current_row_, current_col_); }
    void move_cursor(int dy, int dx, bool extend = false) {
        selection_.move(current_row_, current_col_, dy, dx, extend, ROWS, COLS);
    }
    void set_cursor(size_t row, size_t col) {
        current_row_ = std::min(row, ROWS - 1);
        current_col_ = std::min(col, COLS - 1);
        selection_.reset();
    }
    bool input_mode_key(int key) {
        if (awaiting_pattern_) return pattern_input(key);
        if (awaiting_copy_) {
            if (key == 27) { awaiting_copy_ = false; message_ = nullptr; }
            else if (key == 'p' || key == 's') {
                awaiting_copy_ = false;
                if (key == 'p') copy_pattern(); else copy_selection();
            }
            return true;
        }
        if (key == 'C') { awaiting_copy_ = true; return true; }
        return pattern_input(key);
    }
    // Return true when prompted input was consumed; caller must not dispatch
    // it to normal editing/playback/quit controls while awaiting a number.
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
        if (switch_pending_) { message_ = "Pattern switch pending - try again"; return false; }
        if (id >= NUM_STORED_PATTERNS) return false;
        if (id == selected_pattern_) { message_ = nullptr; return true; }
        exchange_.slots[spare_slot_] = storage_[id];
        const sequencer_command command{
            ._type = sequencer_command::type::activate_pattern,
            ._generation = generation_ + 1,
            ._pattern_id = static_cast<uint8_t>(id),
            ._slot = spare_slot_,
        };
        if (!enqueue(command)) return false;
        ++generation_;
        selected_pattern_ = id;
        switch_pending_ = true;
        selection_.reset();
        refresh_grid(); // Display immediately; audio catches up at callback entry.
        return true;
    }
    void poll_notifications() {
        for (size_t i = 0, count = exchange_.edits_completed.size(); i < count; ++i) {
            const uint8_t slot = *exchange_.edits_completed.front();
            edit_free_[slot] = true;
            exchange_.edits_completed.pop();
        }
        if (const auto* ack = exchange_.completed.front()) {
            // One outstanding activation; this ack returns its retired slot.
            if (switch_pending_ && ack->generation == generation_ &&
                ack->pattern_id == selected_pattern_) {
                spare_slot_ = ack->retired_slot;
                playing_pattern_ = ack->pattern_id;
                switch_pending_ = false;
            }
            exchange_.completed.pop();
        }
    }
    bool toggleplay() {
        return enqueue({._type = sequencer_command::type::toggle_play});
    }
    bool togglecell() {
        auto step = storage_[selected_pattern_]._tracks[current_row_]._steps[current_col_];
        const bool active = !grid_state_[current_row_][current_col_];
        if (active) {
            if (step._vel <= 0) step._vel = 100;
            step._note = notes_[selected_pattern_][current_row_][current_col_];
        } else step._note = 0;
        const sequencer_command command{
            ._type = sequencer_command::type::set_step,
            ._track = static_cast<uint8_t>(current_row_),
            ._step = static_cast<uint8_t>(current_col_),
            ._value = step,
            ._generation = generation_,
            ._pattern_id = static_cast<uint8_t>(selected_pattern_),
        };
        if (!enqueue(command)) return false;
        storage_[selected_pattern_]._tracks[current_row_]._steps[current_col_] = step;
        grid_state_[current_row_][current_col_] = active;
        return true;
    }
    bool clearrow() {
        const sequencer_command command{
            ._type = sequencer_command::type::clear_track,
            ._track = static_cast<uint8_t>(current_row_),
            ._generation = generation_,
            ._pattern_id = static_cast<uint8_t>(selected_pattern_),
        };
        if (!enqueue(command)) return false;
        for (size_t col = 0; col < COLS; ++col) {
            grid_state_[current_row_][col] = false;
            storage_[selected_pattern_]._tracks[current_row_]._steps[col]._note = 0;
        }
        return true;
    }
    bool toggle_selection() {
        const auto area = selection();
        if (!area.multiple()) return togglecell();
        auto edited = storage_[selected_pattern_];
        for (size_t row = area.top; row <= area.bottom; ++row)
            for (size_t col = area.left; col <= area.right; ++col) {
                auto& step = edited._tracks[row]._steps[col];
                if (step._note) step._note = 0;
                else {
                    step._note = notes_[selected_pattern_][row][col];
                    if (step._vel <= 0) step._vel = 100;
                }
            }
        return submit_edit(edited);
    }
    bool clear_selection() {
        const auto area = selection();
        if (!area.multiple()) return clearrow();
        auto edited = storage_[selected_pattern_];
        for (size_t row = area.top; row <= area.bottom; ++row)
            for (size_t col = area.left; col <= area.right; ++col)
                edited._tracks[row]._steps[col]._note = 0;
        return submit_edit(edited);
    }
    void copy_pattern() {
        clipboard_ = storage_[selected_pattern_];
        clipboard_type_ = clipboard_type::pattern;
        message_ = "Copied entire pattern (including hidden tracks)";
    }
    void copy_selection() {
        const auto area = selection();
        clipboard_ = {};
        clipboard_rows_ = area.bottom - area.top + 1;
        clipboard_cols_ = area.right - area.left + 1;
        for (size_t row = 0; row < clipboard_rows_; ++row)
            for (size_t col = 0; col < clipboard_cols_; ++col)
                clipboard_._tracks[row]._steps[col] =
                    storage_[selected_pattern_]._tracks[area.top + row]._steps[area.left + col];
        clipboard_type_ = clipboard_type::selection;
        std::snprintf(feedback_.data(), feedback_.size(), "Copied selection: %zu rows x %zu steps",
                      clipboard_rows_, clipboard_cols_);
        message_ = feedback_.data();
    }
    bool paste() {
        if (clipboard_type_ == clipboard_type::empty) { message_ = "Clipboard empty"; return false; }
        auto edited = storage_[selected_pattern_];
        const bool whole = clipboard_type_ == clipboard_type::pattern;
        const size_t rows = whole ? ROWS : std::min(clipboard_rows_, ROWS - current_row_);
        const size_t cols = whole ? COLS : std::min(clipboard_cols_, COLS - current_col_);
        if (whole) edited = clipboard_;
        else for (size_t row = 0; row < rows; ++row)
            for (size_t col = 0; col < cols; ++col)
                edited._tracks[current_row_ + row]._steps[current_col_ + col] = clipboard_._tracks[row]._steps[col];
        if (!submit_edit(edited)) return false;
        // Pasted notes replace remembered notes only in the affected region.
        for (size_t row = 0; row < rows; ++row)
            for (size_t col = 0; col < cols; ++col) {
                const size_t r = whole ? row : current_row_ + row;
                const size_t c = whole ? col : current_col_ + col;
                const int note = note_number(r, c);
                notes_[selected_pattern_][r][c] = note ? note : kDefaultNoteNumber;
            }
        message_ = "Pasted";
        return true;
    }
    int note_number(size_t row, size_t col) const {
        return storage_[selected_pattern_]._tracks[row]._steps[col]._note;
    }
    std::array<std::array<bool, COLS>, ROWS> grid_state_{};
    size_t current_row_{};
    size_t current_col_{};
    size_t selected_pattern_{};
    size_t playing_pattern_{}; // Last acknowledged selection, independent of play/stop.
    bool switch_pending_{};
    bool awaiting_pattern_{};
    bool awaiting_copy_{};
    bool queue_full_{};
    const char* message_{};
private:
    bool submit_edit(const pattern& edited) {
        poll_notifications();
        size_t slot = 0;
        while (slot < NUM_EDIT_SLOTS && !edit_free_[slot]) ++slot;
        if (slot == NUM_EDIT_SLOTS) { message_ = "Edit transfers busy - try again"; return false; }
        exchange_.edits[slot] = edited;
        const sequencer_command command{
            ._type = sequencer_command::type::replace_pattern,
            ._generation = generation_,
            ._pattern_id = static_cast<uint8_t>(selected_pattern_),
            ._slot = static_cast<uint8_t>(slot),
        };
        if (!enqueue(command)) return false;
        edit_free_[slot] = false;
        storage_[selected_pattern_] = edited;
        refresh_grid();
        return true;
    }
    void refresh_grid() {
        for (size_t row = 0; row < ROWS; ++row)
            for (size_t col = 0; col < COLS; ++col)
                grid_state_[row][col] = note_number(row, col) != 0;
    }
    bool enqueue(const sequencer_command& command) {
        queue_full_ = !commands_.try_push(command);
        message_ = queue_full_ ? "Edit queue full - edit not applied; try again" : nullptr;
        return !queue_full_;
    }
    std::array<std::array<std::array<int, COLS>, ROWS>, NUM_STORED_PATTERNS> notes_{};
    pattern_storage& storage_;
    command_queue& commands_;
    pattern_exchange& exchange_;
    uint64_t generation_{};
    uint8_t spare_slot_{1};
    grid_selection selection_;
    enum class clipboard_type { empty, selection, pattern };
    clipboard_type clipboard_type_{clipboard_type::empty};
    pattern clipboard_{};
    size_t clipboard_rows_{}, clipboard_cols_{};
    std::array<char, 96> feedback_{};
    std::array<bool, NUM_EDIT_SLOTS> edit_free_ = [] {
        std::array<bool, NUM_EDIT_SLOTS> free{};
        free.fill(true);
        return free;
    }();
};

void run_grid_tui(GridWidget& widget, const sequencer_status& status);

} // namespace bork
