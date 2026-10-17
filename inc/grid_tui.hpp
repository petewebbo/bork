#pragma once

#include "pattern.hpp"
#include "sequencer_command.hpp"
#include <array>
#include <cstddef>

namespace bork {

// UI-thread only: owns a snapshot, never a reference to live audio state.
class GridWidget {
public:
    static constexpr size_t ROWS = 5;
    static constexpr size_t COLS = MAX_PATTERN_STEPS;
    GridWidget(const pattern& initial_pattern, command_queue& commands)
        : pattern_(initial_pattern), commands_(commands) {
        for (size_t row = 0; row < ROWS; ++row)
            for (size_t col = 0; col < COLS; ++col)
                grid_state_[row][col] = pattern_._tracks[row]._steps[col]._note != 0;
    }
    bool togglecell() {
        auto step = pattern_._tracks[current_row_]._steps[current_col_];
        const bool active = !grid_state_[current_row_][current_col_];
        if (active) {
            if (step._vel <= 0) step._vel = 100;
            step._note = 1;
        } else {
            step._note = 0;
        }
        const sequencer_command command{
            ._type = sequencer_command::type::set_step,
            ._track = static_cast<uint8_t>(current_row_),
            ._step = static_cast<uint8_t>(current_col_),
            ._value = step,
        };
        if (!enqueue(command)) return false;
        pattern_._tracks[current_row_]._steps[current_col_] = step;
        grid_state_[current_row_][current_col_] = active;
        return true;
    }
    bool clearrow() {
        const sequencer_command command{
            ._type = sequencer_command::type::clear_track,
            ._track = static_cast<uint8_t>(current_row_),
        };
        if (!enqueue(command)) return false;
        for (size_t col = 0; col < COLS; ++col) {
            grid_state_[current_row_][col] = false;
            pattern_._tracks[current_row_]._steps[col]._note = 0;
        }
        return true;
    }
    std::array<std::array<bool, COLS>, ROWS> grid_state_{};
    size_t current_row_{};
    size_t current_col_{};
    bool queue_full_{};
private:
    bool enqueue(const sequencer_command& command) {
        queue_full_ = !commands_.try_push(command);
        return !queue_full_;
    }
    pattern pattern_;
    command_queue& commands_;
};

void run_grid_tui(GridWidget& widget);

} // namespace bork
