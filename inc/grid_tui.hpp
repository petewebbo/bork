#pragma once

#include "pattern_exchange.hpp"
#include "sequencer_command.hpp"
#include "sequencer_status.hpp"
#include <array>
#include <cstddef>

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
        refresh_grid(); // Display immediately; audio catches up at callback entry.
        return true;
    }
    void poll_notifications() {
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
    bool queue_full_{};
    const char* message_{};
private:
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
};

void run_grid_tui(GridWidget& widget, const sequencer_status& status);

} // namespace bork
