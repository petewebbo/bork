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
constexpr short SELECTED_ACTIVE_PAIR = 6, STATUS_PAIR = 7, SELECTION_PAIR = 8;
constexpr short PLAYHEAD_ROW_FIRST = 9, PLAYHEAD_EMPTY = 14,
                PLAYHEAD_CURSOR = 15, PLAYHEAD_SELECTION = 16, PLAYHEAD_EMPTY_CURSOR = 17;
constexpr int SHIFT_UP = 0x2000, SHIFT_DOWN = 0x2001,
              SHIFT_LEFT = 0x2002, SHIFT_RIGHT = 0x2003;
constexpr int CTRL_SHIFT_UP = 0x2004, CTRL_SHIFT_DOWN = 0x2005,
              CTRL_SHIFT_LEFT = 0x2006, CTRL_SHIFT_RIGHT = 0x2007;
constexpr const char* names[] = {"Kick", "Snare", "Clap", "HH", "Open HH"};

struct Terminal {
    Terminal() {
        std::setlocale(LC_ALL, "");
        if (!initscr()) throw std::runtime_error("Could not initialise terminal");
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        // Standard xterm modifier encoding, also used by many modern terminals.
        // Keep native ncurses shifted-arrow codes as a fallback below.
        define_key("\033[1;2A", SHIFT_UP);
        define_key("\033[1;2B", SHIFT_DOWN);
        define_key("\033[1;2D", SHIFT_LEFT);
        define_key("\033[1;2C", SHIFT_RIGHT);
        define_key("\033[1;6A", CTRL_SHIFT_UP);
        define_key("\033[1;6B", CTRL_SHIFT_DOWN);
        define_key("\033[1;6D", CTRL_SHIFT_LEFT);
        define_key("\033[1;6C", CTRL_SHIFT_RIGHT);
        timeout(30); // Refresh telemetry even when no keys arrive (~33 FPS).
        curs_set(0);
        mousemask(ALL_MOUSE_EVENTS, nullptr);
        mouseinterval(200);
        if (has_colors()) {
            start_color();
            const short colors[] = {COLOR_YELLOW, COLOR_MAGENTA, COLOR_CYAN, COLOR_BLUE, COLOR_GREEN};
            for (short row = 0; row < 5; ++row) init_pair(row + 1, COLOR_BLACK, colors[row]);
            short dark_grey = COLORS >= 256 ? 240 : COLORS >= 16 ? 8 : COLOR_BLACK;
            if (can_change_color() && COLORS > 16) {
                dark_grey = 16;
                init_color(dark_grey, 220, 220, 220);
            }
            init_pair(SELECTED_ACTIVE_PAIR, COLOR_WHITE, dark_grey);
            init_pair(STATUS_PAIR, COLOR_WHITE, dark_grey);
            short light_grey = COLORS >= 256 ? 250 : COLOR_WHITE;
            if (can_change_color() && COLORS > 17) {
                light_grey = 17;
                init_color(light_grey, 750, 750, 750);
            }
            init_pair(SELECTION_PAIR, COLOR_BLACK, light_grey);
            short soft_red = COLORS >= 256 ? 210 : COLORS >= 16 ? 9 : COLOR_RED;
            if (can_change_color() && COLORS > 18) {
                soft_red = 18;
                init_color(soft_red, 900, 400, 400);
            }
            for (short row = 0; row < 5; ++row)
                init_pair(PLAYHEAD_ROW_FIRST + row, soft_red, colors[row]);
            init_pair(PLAYHEAD_EMPTY, soft_red, COLOR_BLACK);
            init_pair(PLAYHEAD_CURSOR, soft_red, dark_grey);
            init_pair(PLAYHEAD_SELECTION, soft_red, light_grey);
            init_pair(PLAYHEAD_EMPTY_CURSOR, soft_red, COLOR_WHITE);
        }
    }
    ~Terminal() { endwin(); }
};
bool fits() { return getmaxx(stdscr) >= MIN_WIDTH && getmaxy(stdscr) >= MIN_HEIGHT; }
void draw_status_bar(const GridWidget& widget) {
    const int width = getmaxx(stdscr), height = getmaxy(stdscr);
    if (width < 1 || height < 1) return;
    attrset(has_colors() ? COLOR_PAIR(STATUS_PAIR) : A_REVERSE);
    // Always paint the entire top row, including when there is no message.
    mvhline(0, 0, ' ', width);
    const char* text = widget.awaiting_pattern_ ? "Select pattern 1-8 | Esc: cancel"
        : widget.awaiting_copy_ ? "Copy: p = pattern | s = selection | Esc = cancel"
        : widget.message_ ? widget.message_
        : widget.switch_pending_ ? "Pattern switch pending" : "";
    if (width > 2) mvaddnstr(0, 1, text, width - 2);
    attrset(A_NORMAL);
}
void draw(const GridWidget& widget, const sequencer_status& status) {
    erase();
    if (!fits()) {
        const char* prompt = "Resize terminal (90 x 15) | q quits";
        if (getmaxy(stdscr) > 1 && getmaxx(stdscr) > 1)
            mvaddnstr(1, 0, prompt, getmaxx(stdscr) - 1);
        draw_status_bar(widget);
        refresh();
        return;
    }
    const bool playing = status.playing.load(std::memory_order_relaxed);
    attrset(A_BOLD);
    mvprintw(1, 2, "BORK - PATTERN %02d | %s | Audio: %02d%s",
             static_cast<int>(widget.selected_pattern_ + 1),
             playing ? "PLAYING" : "STOPPED",
             static_cast<int>(widget.playing_pattern_ + 1),
             widget.switch_pending_ ? " | Switch pending" : "");
    attrset(A_NORMAL);
    mvaddstr(2, 2, "Shift+arrows: select | x: toggle | c: clear | C: copy | V: paste | P: pattern");
    for (size_t col = 0; col < GridWidget::COLS; ++col)
        mvprintw(TOP - 2, LEFT + col * PITCH + 1, "%02d", static_cast<int>(col + 1));
    const auto area = widget.selection();
    for (size_t row = 0; row < GridWidget::ROWS; ++row) {
        const int y = TOP + row * ROW_H;
        attrset(A_BOLD);
        mvaddstr(y, 2, names[row]);
        const auto current_step = status.current_step(row,
            static_cast<uint8_t>(widget.selected_pattern_), playing);
        for (size_t col = 0; col < GridWidget::COLS; ++col) {
            const bool active = widget.grid_state_[row][col];
            const bool selected = row == widget.current_row_ && col == widget.current_col_;
            int style = active ? A_BOLD : A_DIM;
            if (area.multiple() && area.contains(row, col)) {
                style = has_colors() ? COLOR_PAIR(SELECTION_PAIR) : A_REVERSE;
                if (selected) style |= A_UNDERLINE;
            } else if (selected && active && has_colors())
                style |= COLOR_PAIR(SELECTED_ACTIVE_PAIR);
            else {
                if (active && has_colors()) style |= COLOR_PAIR(row + 1);
                if (selected) style |= A_REVERSE;
            }
            if (col == current_step) {
                // Change foreground only; retain track/cursor/selection backgrounds.
                // Do not reverse a red pair: that would turn the background red.
                if (has_colors()) {
                    short pair = PLAYHEAD_EMPTY;
                    if (area.multiple() && area.contains(row, col)) pair = PLAYHEAD_SELECTION;
                    else if (selected) pair = active ? PLAYHEAD_CURSOR : PLAYHEAD_EMPTY_CURSOR;
                    else if (active) pair = PLAYHEAD_ROW_FIRST + row;
                    style &= ~(A_COLOR | A_REVERSE | A_DIM);
                    style |= COLOR_PAIR(pair) | A_BOLD;
                } else style = (style & ~A_DIM) | A_BOLD | A_UNDERLINE;
            }
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
    draw_status_bar(widget);
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
        if ((widget.awaiting_pattern_ || widget.awaiting_copy_) && key == KEY_MOUSE) {
            MEVENT ignored{};
            getmouse(&ignored);
            continue;
        }
        if (widget.input_mode_key(key)) continue;
        if (key == 'q' || key == 'Q' || key == 27) return;
        if (!fits()) continue;
        switch (key) {
        case KEY_UP: widget.move_cursor(-1, 0); break;
        case KEY_DOWN: widget.move_cursor(1, 0); break;
        case KEY_LEFT: widget.move_cursor(0, -1); break;
        case KEY_RIGHT: widget.move_cursor(0, 1); break;
        case SHIFT_UP: case KEY_SR: widget.move_cursor(-1, 0, true); break;
        case SHIFT_DOWN: case KEY_SF: widget.move_cursor(1, 0, true); break;
        case SHIFT_LEFT: case KEY_SLEFT: widget.move_cursor(0, -1, true); break;
        case SHIFT_RIGHT: case KEY_SRIGHT: widget.move_cursor(0, 1, true); break;
        case CTRL_SHIFT_UP: widget.select_to_edge(-1, 0); break;
        case CTRL_SHIFT_DOWN: widget.select_to_edge(1, 0); break;
        case CTRL_SHIFT_LEFT: widget.select_to_edge(0, -1); break;
        case CTRL_SHIFT_RIGHT: widget.select_to_edge(0, 1); break;
        case KEY_HOME: widget.set_cursor(widget.current_row_, 0); break;
        case KEY_END: widget.set_cursor(widget.current_row_, GridWidget::COLS - 1); break;
        case 'x': widget.toggle_selection(); break;
        case ' ': widget.toggleplay(); break;
        case 'c': widget.clear_selection(); break;
        case 'V': widget.paste(); break;
        case KEY_MOUSE: {
            MEVENT event{};
            if (getmouse(&event) != OK || !(event.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED))) break;
            const int dx = event.x - LEFT, dy = event.y - TOP;
            if (dx < 0 || dy < 0 || dx % PITCH >= CELL_W || dy % ROW_H != 0) break;
            const auto row = static_cast<size_t>(dy / ROW_H), col = static_cast<size_t>(dx / PITCH);
            if (row >= GridWidget::ROWS || col >= GridWidget::COLS) break;
            widget.set_cursor(row, col);
            widget.togglecell();
            break;
        }
        default: break;
        }
    }
}
} // namespace bork
