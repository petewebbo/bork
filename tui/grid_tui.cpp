#include "grid_tui.hpp"
#include <curses.h>
// ncurses also defines COLS; keep the grid's existing constant unambiguous.
#undef COLS
#include <clocale>
#include <stdexcept>

namespace bork {
namespace {
constexpr int LEFT = 10, TOP = 5, CELL_W = 4, PITCH = 5, ROW_H = 2;
constexpr int MIN_WIDTH = LEFT + (GridWidget::COLS - 1) * PITCH + CELL_W + 1;
constexpr int MIN_HEIGHT = TOP + (GridWidget::ROWS - 1) * ROW_H + 2;
constexpr const char* names[] = {"Kick", "Snare", "Clap", "HH", "Open HH"};

struct Terminal {
    Terminal() {
        std::setlocale(LC_ALL, "");
        if (!initscr()) throw std::runtime_error("Could not initialise terminal");
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        curs_set(0);
        mousemask(ALL_MOUSE_EVENTS, nullptr);
        mouseinterval(200);
        if (has_colors()) {
            start_color();
            const short colors[] = {COLOR_YELLOW, COLOR_MAGENTA, COLOR_CYAN, COLOR_BLUE, COLOR_GREEN};
            for (short row = 0; row < 5; ++row) init_pair(row + 1, COLOR_BLACK, colors[row]);
        }
    }
    ~Terminal() { endwin(); }
};
bool fits() { return getmaxx(stdscr) >= MIN_WIDTH && getmaxy(stdscr) >= MIN_HEIGHT; }
void draw(const GridWidget& widget) {
    erase();
    if (!fits()) {
        const char* prompt = "Resize terminal (90 x 15) | q quits";
        if (getmaxy(stdscr) > 0 && getmaxx(stdscr) > 1)
            mvaddnstr(0, 0, prompt, getmaxx(stdscr) - 1);
        refresh();
        return;
    }
    attrset(A_BOLD);
    mvaddstr(1, 2, "BORK - PATTERN 01");
    attrset(A_NORMAL);
    mvaddstr(2, 2, "Click / Space / x: toggle | Arrows: move | C: clear row | q: quit");
    for (size_t col = 0; col < GridWidget::COLS; ++col)
        mvprintw(TOP - 2, LEFT + col * PITCH + 1, "%02d", static_cast<int>(col + 1));
    for (size_t row = 0; row < GridWidget::ROWS; ++row) {
        const int y = TOP + row * ROW_H;
        attrset(A_BOLD);
        mvaddstr(y, 2, names[row]);
        for (size_t col = 0; col < GridWidget::COLS; ++col) {
            const bool active = widget.grid_state_[row][col];
            const bool selected = row == widget.current_row_ && col == widget.current_col_;
            int style = active ? A_BOLD : A_DIM;
            if (active && has_colors()) style |= COLOR_PAIR(row + 1);
            if (selected) style |= A_REVERSE;
            attrset(style);
            mvaddstr(y, LEFT + col * PITCH, active ? " ON " : "  · ");
        }
    }
    attrset(A_NORMAL);
    if (widget.queue_full_)
        mvaddstr(MIN_HEIGHT - 1, 2, "Edit queue full - edit not applied; try again");
    refresh();
}
} // namespace

void run_grid_tui(GridWidget& widget) {
    Terminal terminal;
    for (;;) {
        draw(widget);
        const int key = getch();
        if (key == 'q' || key == 'Q' || key == 27 || key == ERR) return;
        if (!fits()) continue;
        switch (key) {
        case KEY_UP: if (widget.current_row_ > 0) --widget.current_row_; break;
        case KEY_DOWN: if (widget.current_row_ + 1 < GridWidget::ROWS) ++widget.current_row_; break;
        case KEY_LEFT: if (widget.current_col_ > 0) --widget.current_col_; break;
        case KEY_RIGHT: if (widget.current_col_ + 1 < GridWidget::COLS) ++widget.current_col_; break;
        case KEY_HOME: widget.current_col_ = 0; break;
        case KEY_END: widget.current_col_ = GridWidget::COLS - 1; break;
        case ' ': case 'x': widget.togglecell(); break;
        case 'C': widget.clearrow(); break;
        case KEY_MOUSE: {
            MEVENT event{};
            if (getmouse(&event) != OK || !(event.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED))) break;
            const int dx = event.x - LEFT, dy = event.y - TOP;
            if (dx < 0 || dy < 0 || dx % PITCH >= CELL_W || dy % ROW_H != 0) break;
            const auto row = static_cast<size_t>(dy / ROW_H), col = static_cast<size_t>(dx / PITCH);
            if (row >= GridWidget::ROWS || col >= GridWidget::COLS) break;
            widget.current_row_ = row;
            widget.current_col_ = col;
            widget.togglecell();
            break;
        }
        default: break;
        }
    }
}
} // namespace bork
