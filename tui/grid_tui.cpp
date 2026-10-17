#include "grid_tui.hpp"
#include "midi_notes.hpp"
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
        timeout(50); // Refresh telemetry even when no keys arrive.
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
void draw_pattern_popup() {
    constexpr int width = 30, height = 5;
    if (getmaxx(stdscr) < width || getmaxy(stdscr) < height) return;
    const int x = (getmaxx(stdscr) - width) / 2;
    const int y = (getmaxy(stdscr) - height) / 2;
    attrset(A_NORMAL);
    for (int row = 0; row < height; ++row) mvhline(y + row, x, ' ', width);
    mvhline(y, x + 1, ACS_HLINE, width - 2);
    mvhline(y + height - 1, x + 1, ACS_HLINE, width - 2);
    mvvline(y + 1, x, ACS_VLINE, height - 2);
    mvvline(y + 1, x + width - 1, ACS_VLINE, height - 2);
    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + width - 1, ACS_URCORNER);
    mvaddch(y + height - 1, x, ACS_LLCORNER);
    mvaddch(y + height - 1, x + width - 1, ACS_LRCORNER);
    attrset(A_BOLD);
    mvaddstr(y + 1, x + 3, "Select pattern 1-8");
    attrset(A_NORMAL);
    mvaddstr(y + 3, x + 3, "Esc: cancel");
}
void draw(const GridWidget& widget, const sequencer_status& status) {
    erase();
    if (!fits()) {
        const char* prompt = "Resize terminal (90 x 15) | q quits";
        if (getmaxy(stdscr) > 0 && getmaxx(stdscr) > 1)
            mvaddnstr(0, 0, prompt, getmaxx(stdscr) - 1);
        if (widget.awaiting_pattern_) draw_pattern_popup();
        refresh();
        return;
    }
    attrset(A_BOLD);
    mvprintw(1, 2, "BORK - PATTERN %02d | %s | Audio: %02d%s",
             static_cast<int>(widget.selected_pattern_ + 1),
             status.playing.load(std::memory_order_relaxed) ? "PLAYING" : "STOPPED",
             static_cast<int>(widget.playing_pattern_ + 1),
             widget.switch_pending_ ? " | Switch pending" : "");
    attrset(A_NORMAL);
    mvaddstr(2, 2, "Click/x: edit | Space: play | Arrows: move | C: clear | P: pattern | q: quit");
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
            const int x = LEFT + col * PITCH;
            if (active) {
                const auto* note = lookup_midi_note(widget.note_number(row, col));
                mvprintw(y, x, "%-4s", note ? note->name.data() : "????");
            } else {
                mvaddstr(y, x, " ·· ");
            }
        }
    }
    attrset(A_NORMAL);
    if (widget.message_) mvaddstr(MIN_HEIGHT - 1, 2, widget.message_);
    if (widget.awaiting_pattern_) draw_pattern_popup();
    refresh();
}
} // namespace

void run_grid_tui(GridWidget& widget, const sequencer_status& status) {
    Terminal terminal;
    for (;;) {
        widget.poll_notifications();
        draw(widget, status);
        const int key = getch();
        widget.poll_notifications(); // Ack may have arrived while waiting for input.
        if (key == ERR) continue;
        if (widget.awaiting_pattern_ && key == KEY_MOUSE) {
            MEVENT ignored{};
            getmouse(&ignored);
            continue;
        }
        if (widget.pattern_input(key)) continue;
        if (key == 'q' || key == 'Q' || key == 27) return;
        if (!fits()) continue;
        switch (key) {
        case KEY_UP: if (widget.current_row_ > 0) --widget.current_row_; break;
        case KEY_DOWN: if (widget.current_row_ + 1 < GridWidget::ROWS) ++widget.current_row_; break;
        case KEY_LEFT: if (widget.current_col_ > 0) --widget.current_col_; break;
        case KEY_RIGHT: if (widget.current_col_ + 1 < GridWidget::COLS) ++widget.current_col_; break;
        case KEY_HOME: widget.current_col_ = 0; break;
        case KEY_END: widget.current_col_ = GridWidget::COLS - 1; break;
        case 'x': widget.togglecell(); break;
        case ' ': widget.toggleplay(); break;
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
