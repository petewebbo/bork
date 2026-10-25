#include "ui/editor_component.hpp"
#include "ui/pattern_grid.hpp"
#include "ui/editor_state.hpp"
#include "audio/sequencer.hpp"
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>
#include <array>
#include <string>
#include <utility>
#undef NDEBUG
#include <cassert>

namespace {
using namespace ftxui;
struct fixture {
    bork::pattern_store store;
    bork::sequencer audio;
    bork::EditorState state;
    bork::sequencer_status status;
    bool quit{};
    Component editor;
    explicit fixture(const bork::pattern_storage& initial = {})
        : store(initial), audio(store.exchange_, store.initial_snapshot()), state(store),
          editor(bork::make_editor_component(state, status, [this] { quit = true; })) { flush(); }
    void flush() {
        for (int pass = 0; pass < 6; ++pass) {
            store.pump();
            audio.process_commands();
            audio.publish_status(store.audio_status_, bork::EditorState::ROWS);
            store.pump();
            state.poll_notifications();
        }
    }
    Screen render(int width = 90, int height = 15) {
        Screen screen(width, height);
        Render(screen, editor->Render());
        return screen;
    }
    void key(char c) { assert(editor->OnEvent(Event::Character(c))); }
};

std::pair<int, int> find(const Screen& screen, const std::string& value) {
    for (int y = 0; y < screen.dimy(); ++y)
        for (int x = 0; x < screen.dimx(); ++x) {
            std::string candidate;
            for (int offset = 0; x + offset < screen.dimx() && candidate.size() < value.size(); ++offset) {
                const auto& cell = screen.at(x + offset, y);
                candidate += cell.empty() ? " " : cell;
            }
            if (candidate == value) return {x, y};
        }
    return {-1, -1};
}
std::string cell_text(const Screen& screen, int x, int y) {
    std::string value;
    for (int offset = 0; offset < 4; ++offset) {
        const auto& cell = screen.at(x + offset, y);
        value += cell.empty() ? " " : cell;
    }
    return value;
}
Event mouse(int x, int y, Mouse::Motion motion = Mouse::Pressed) {
    Mouse event;
    event.x = x;
    event.y = y;
    event.button = Mouse::Left;
    event.motion = motion;
    return Event::Mouse("mouse", event);
}
}

void test_ui_components() {
    const auto original_support = Terminal::ColorSupport();
    Terminal::SetColorSupport(Terminal::TrueColor);
    bork::pattern_storage initial{};
    initial[0].tracks_[0].steps_[0] = {.note_ = 67, .vel_ = 81, .len_ = 12};
    initial[0].tracks_[0].steps_[1] = {.note_ = 0, .vel_ = 42, .len_ = 6};
    initial[0].tracks_[4].steps_[15] = {.note_ = 73, .vel_ = 99, .len_ = 9};
    fixture f(initial);
    auto screen = f.render();
    const auto [first_x, first_y] = find(screen, "G4  ");
    const auto [last_x, last_y] = find(screen, "C#5 ");
    assert(first_x >= 0 && last_x > first_x && last_y > first_y);
    assert(screen.at(first_x + 6, first_y) == "·");
    assert(screen.at(first_x + 7, first_y) == "·");
    assert(screen.CellAt(first_x, first_y).underlined);
    f.key('e');
    f.key('v');
    screen = f.render();
    assert(cell_text(screen, first_x, first_y) == "  81");
    assert(cell_text(screen, first_x + 5, first_y) == "  42");
    assert(find(screen, "G4").first == -1);
    f.key('e');
    f.key('l');
    screen = f.render();
    assert(cell_text(screen, first_x, first_y) == "  12");
    assert(cell_text(screen, first_x + 5, first_y) == "   6");
    f.key('e');
    f.key('n');
    assert(find(f.render(), "G4  ") == std::make_pair(first_x, first_y));

    // Only press toggles; labels, gutters, release, and drag cannot toggle another cell.
    assert(f.editor->OnEvent(mouse(last_x, last_y)));
    assert(f.state.current_row_ == 4 && f.state.current_col_ == 15);
    assert(f.state.note_number(4, 15) == 0);
    f.editor->OnEvent(mouse(last_x, last_y, Mouse::Released));
    f.editor->OnEvent(mouse(first_x, first_y, Mouse::Moved));
    f.editor->OnEvent(mouse(first_x + 4, first_y));
    const auto [label_x, label_y] = find(screen, "Kick");
    f.editor->OnEvent(mouse(label_x, label_y));
    assert(f.state.note_number(4, 15) == 0 && f.state.note_number(0, 0) == 67);
    assert(f.state.current_row_ == 4 && f.state.current_col_ == 15);
    f.flush();

    // Reflected boxes follow the DOM even when the same custom grid is translated.
    auto grid = bork::make_pattern_grid(f.state, f.status);
    Screen translated(110, 22);
    Render(translated, vbox({text("offset"), hbox({text("       "), grid->Render()})}));
    const auto moved = find(translated, "G4  ");
    assert(moved.first != first_x && moved.second != first_y);
    assert(grid->OnEvent(mouse(moved.first, moved.second)));
    assert(f.state.current_row_ == 0 && f.state.current_col_ == 0);
    assert(f.state.note_number(0, 0) == 0);
    f.flush();
    f.render();
    f.key('x');
    assert(f.state.note_number(0, 0) == 67);
    f.flush();

    // Typed navigation, shifted selection, edge selection, and single-cell reset.
    assert(f.editor->OnEvent(Event::ArrowDown));
    assert(f.editor->OnEvent(Event::ArrowRight));
    assert(f.state.current_row_ == 1 && f.state.current_col_ == 1);
    assert(f.editor->OnEvent(Event::ArrowLeft));
    assert(f.editor->OnEvent(Event::ArrowUp));
    assert(f.editor->OnEvent(Event::Special("\x1b[1;2C")));
    assert(f.state.selection().multiple() && f.state.selection().right == 1);
    assert(f.editor->OnEvent(Event::Special("\x1b[1;2B")));
    assert(f.editor->OnEvent(Event::Special("\x1b[1;2D")));
    assert(f.editor->OnEvent(Event::Special("\x1b[1;2A")));
    assert(f.state.current_row_ == 0 && f.state.current_col_ == 0);
    assert(f.editor->OnEvent(Event::Special("\x1b[1;6C")));
    assert(f.state.current_col_ == 15 && f.state.selection().left == 0);
    assert(f.editor->OnEvent(Event::Special("\x1b[1;6B")));
    assert(f.state.current_row_ == 4 && f.state.selection().top == 0);
    assert(f.editor->OnEvent(Event::Special("\x1b[1;6D")));
    assert(f.editor->OnEvent(Event::Special("\x1b[1;6A")));
    assert(f.state.current_row_ == 0 && f.state.current_col_ == 0);
    assert(f.editor->OnEvent(Event::End));
    assert(f.state.current_col_ == 15 && !f.state.selection().multiple());
    assert(f.editor->OnEvent(Event::Home));
    assert(f.state.current_col_ == 0);

    // Playhead foreground changes never remove the selected background or cursor underline.
    for (const auto support : {Terminal::TrueColor, Terminal::Palette256, Terminal::Palette16,
                               Terminal::Palette1}) {
        Terminal::SetColorSupport(support);
        f.state.set_cursor(0, 0);
        assert(f.editor->OnEvent(Event::Special("\x1b[1;2C")));
        f.status.playing.store(false);
        const auto before = f.render().CellAt(first_x + 5, first_y);
        f.status.publish_position(0, 0, 1);
        f.status.playing.store(true);
        screen = f.render();
        const auto during = screen.CellAt(first_x + 5, first_y);
        assert(during.background_color == before.background_color);
        assert(during.underlined && during.bold && !during.dim);
        assert(during.inverted == before.inverted);
        if (support == Terminal::Palette1) assert(during.inverted);
        else assert(during.foreground_color != before.foreground_color);
        f.status.publish_position(0, 1, 1);
        const auto wrong_pattern = f.render().CellAt(first_x + 5, first_y);
        assert(wrong_pattern.foreground_color == before.foreground_color);
        f.status.publish_position(0, 0, 1);
        f.status.playing.store(false);
        assert(f.render().CellAt(first_x + 5, first_y).foreground_color == before.foreground_color);
    }
    Terminal::SetColorSupport(Terminal::TrueColor);
    f.state.set_cursor(0, 0);
    f.flush();

    // The root consumes every event while prompting, including unknown specials and quit.
    for (char prompt : {'c', 'P', 'e'}) {
        f.key(prompt);
        const auto mode = f.state.mode_;
        assert(mode != bork::input_mode::normal);
        for (auto event : {Event::ArrowDown, Event::End, Event::Special("\x1b[1;2C"),
                           Event::Special("\x1b[1;6B"), Event::Special("\x1b[99~"),
                           Event::Character('x'), Event::Character(' '), Event::Character('q'),
                           Event::Character('Q'), mouse(last_x, last_y)})
            assert(f.editor->OnEvent(event));
        assert(f.state.mode_ == mode && !f.quit);
        assert(f.state.current_row_ == 0 && f.state.current_col_ == 0);
        assert(f.state.note_number(0, 0) == 67);
        assert(f.store.outstanding_requests() == 0 && !f.audio.is_playing_);
        assert(f.editor->OnEvent(Event::Escape));
        assert(f.state.mode_ == bork::input_mode::normal && !f.quit);
    }
    assert(f.editor->OnEvent(Event::Escape) && !f.quit);
    f.key('c');
    f.key('s');
    assert(f.editor->OnEvent(Event::ArrowRight));
    f.key('v');
    assert(f.state.note_number(0, 1) == 67);
    f.flush();
    f.key('c');
    f.key('p');
    assert(f.state.mode_ == bork::input_mode::normal);
    f.key(' ');
    f.flush();
    assert(f.audio.is_playing_);
    f.key(' ');
    f.flush();
    assert(!f.audio.is_playing_);
    f.key('P');
    f.key('8');
    f.flush();
    assert(f.state.selected_pattern_ == 7);
    assert(find(f.render(), "PATTERN 08").first >= 0);

    // Resize fallback uses the allocated Screen, hides the grid, and suppresses edits.
    for (const auto [width, height] : std::array<std::pair<int, int>, 4>{{{89, 15}, {90, 14}, {20, 3}, {1, 1}}}) {
        const auto row = f.state.current_row_;
        const auto col = f.state.current_col_;
        const auto pending = f.store.outstanding_requests();
        screen = f.render(width, height);
        assert(find(screen, "Kick").first == -1);
        f.editor->OnEvent(Event::ArrowDown);
        f.editor->OnEvent(mouse(first_x, first_y));
        f.editor->OnEvent(Event::Character('x'));
        f.editor->OnEvent(Event::Character(' '));
        assert(f.state.current_row_ == row && f.state.current_col_ == col);
        assert(f.store.outstanding_requests() == pending);
        f.key('e');
        assert(f.state.mode_ == bork::input_mode::edit_select);
        assert(f.editor->OnEvent(Event::Escape) && !f.quit);
    }
    assert(find(f.render(120, 30), "Open HH").first >= 0);
    assert(f.editor->OnEvent(Event::ArrowDown));
    assert(f.state.current_row_ == 1);
    f.key('q');
    assert(f.quit);
    f.quit = false;
    f.render(89, 14);
    f.key('Q');
    assert(f.quit);
    Terminal::SetColorSupport(original_support);
}
