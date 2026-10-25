#pragma once

#include <algorithm>
#include <cstddef>

namespace bork {
struct grid_extents {
    size_t top, bottom, left, right; // Inclusive.
    bool multiple() const { return top != bottom || left != right; }
    bool contains(size_t row, size_t col) const {
        return row >= top && row <= bottom && col >= left && col <= right;
    }
};

class grid_selection {
public:
    grid_extents extents(size_t row, size_t col) const {
        if (!extended_) return {row, row, col, col};
        return {std::min(anchor_row_, row), std::max(anchor_row_, row),
                std::min(anchor_col_, col), std::max(anchor_col_, col)};
    }
    void reset() { extended_ = false; }
    void move(size_t& row, size_t& col, int dy, int dx, bool extend,
              size_t rows, size_t cols) {
        if (extend && !extended_) { anchor_row_ = row; anchor_col_ = col; }
        extended_ = extend;
        row = static_cast<size_t>(std::clamp(static_cast<int>(row) + dy, 0, static_cast<int>(rows - 1)));
        col = static_cast<size_t>(std::clamp(static_cast<int>(col) + dx, 0, static_cast<int>(cols - 1)));
    }
private:
    size_t anchor_row_{}, anchor_col_{};
    bool extended_{};
};
} // namespace bork
