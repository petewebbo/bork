#pragma once

#include "pattern.hpp"
#include <array>
#include <cstddef>

namespace bork {

// Temporary UI model: snapshot on construction, direct writes on edits.
// Intentionally unsynchronised with the audio callback for this prototype.
class GridWidget {
public:
    static constexpr size_t ROWS = 5;
    static constexpr size_t COLS = MAX_PATTERN_STEPS;
    explicit GridWidget(pattern& pattern) : pattern_(pattern) {
        for (size_t row = 0; row < ROWS; ++row)
            for (size_t col = 0; col < COLS; ++col)
                grid_state_[row][col] = pattern_._tracks[row]._steps[col]._note != 0;
    }
    void togglecell() {
        auto& step = pattern_._tracks[current_row_]._steps[current_col_];
        auto& active = grid_state_[current_row_][current_col_];
        active = !active;
        if (active) {
            if (step._vel <= 0) step._vel = 100;
            step._note = 1;
        } else {
            step._note = 0; // Preserve velocity and length for the next toggle.
        }
    }
    void clearrow() {
        for (size_t col = 0; col < COLS; ++col) {
            grid_state_[current_row_][col] = false;
            pattern_._tracks[current_row_]._steps[col]._note = 0;
        }
    }
    std::array<std::array<bool, COLS>, ROWS> grid_state_{};
    size_t current_row_{};
    size_t current_col_{};
private:
    pattern& pattern_;
};

void run_grid_tui(pattern& pattern);

} // namespace bork
