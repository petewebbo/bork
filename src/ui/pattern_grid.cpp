#include "ui/pattern_grid.hpp"
#include "ui/editor_state.hpp"
#include "model/midi_notes.hpp"
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>
#include <array>
#include <cstdio>
#include <string>
#include <utility>

namespace bork {
namespace {
using namespace ftxui;
constexpr std::array<const char*, EditorState::ROWS> names{"Kick", "Snare", "Clap", "HH", "Open HH"};
constexpr std::array<Color::Palette16, EditorState::ROWS> track_colors{
    Color::Yellow, Color::Magenta, Color::Cyan, Color::Blue, Color::Green};

class pattern_grid final : public ComponentBase {
public:
    pattern_grid(EditorState& state, const sequencer_status& status) : state_(state), status_(status) {
        reset_boxes();
    }
    bool Focusable() const override { return true; }
    Element OnRender() override {
        reset_boxes();
        const auto capability = Terminal::ColorSupport();
        const bool colors = capability != Terminal::Palette1;
        const Color dark_grey = capability == Terminal::TrueColor ? Color::RGB(56, 56, 56)
            : capability == Terminal::Palette256 ? Color(Color::Grey23) : Color(Color::GrayDark);
        const Color light_grey = capability == Terminal::TrueColor ? Color::RGB(191, 191, 191)
            : capability == Terminal::Palette256 ? Color(Color::Grey74) : Color(Color::GrayLight);
        const Color soft_red = capability == Terminal::TrueColor ? Color::RGB(230, 102, 102)
            : capability == Terminal::Palette256 ? Color(Color::LightCoral) : Color(Color::RedLight);
        const auto area = state_.selection();
        const bool playing = status_.playing.load(std::memory_order_relaxed) && !state_.loading();
        Elements header{text("") | size(WIDTH, EQUAL, 8)};
        for (size_t col = 0; col < EditorState::COLS; ++col) {
            char number[5];
            std::snprintf(number, sizeof(number), " %02d ", static_cast<int>(col + 1));
            header.push_back(text(number));
            if (col + 1 != EditorState::COLS) header.push_back(text(" "));
        }
        Elements rows{hbox(std::move(header)), text("")};
        for (size_t row = 0; row < EditorState::ROWS; ++row) {
            Elements cells{text(names[row]) | bold | size(WIDTH, EQUAL, 8)};
            const auto playhead = status_.current_step(row, static_cast<uint8_t>(state_.loaded_pattern_), playing);
            for (size_t col = 0; col < EditorState::COLS; ++col) {
                const bool active = state_.grid_state_[row][col];
                const bool cursor = row == state_.current_row_ && col == state_.current_col_;
                const bool selected = area.multiple() && area.contains(row, col);
                const bool at_playhead = col == playhead;
                std::string value;
                if (state_.edit_field_ != EditorState::edit_field::note) {
                    char number[16];
                    std::snprintf(number, sizeof(number), "%4d", state_.displayed_value(row, col));
                    value = number;
                } else if (active) {
                    const auto* note = lookup_midi_note(state_.note_number(row, col));
                    value = note ? note->name.data() : "????";
                    value.resize(4, ' ');
                } else value = " ·· ";
                auto cell = text(value) | size(WIDTH, EQUAL, 4);
                if (active || at_playhead) cell |= bold;
                else if (!selected && !cursor) cell |= dim;
                if (colors) {
                    Color foreground = active ? Color(Color::Black) : Color(Color::GrayLight);
                    Color background = active ? Color(track_colors[row]) : Color(Color::Black);
                    if (selected) { background = light_grey; foreground = Color::Black; }
                    else if (cursor) { background = dark_grey; foreground = Color::White; }
                    // Foreground only: playback must not erase selection or cursor styling.
                    if (at_playhead) foreground = soft_red;
                    cell = cell | color(foreground) | bgcolor(background);
                } else if (selected || cursor) cell |= inverted;
                if (cursor || (!colors && at_playhead)) cell |= underlined;
                cells.push_back(cell | reflect(boxes_[row][col]));
                if (col + 1 != EditorState::COLS) cells.push_back(text(" "));
            }
            rows.push_back(hbox(std::move(cells)));
            rows.push_back(text(""));
        }
        return hbox({text("  "), vbox(std::move(rows)), filler()});
    }
    bool OnEvent(Event event) override {
        if (state_.mode_ != input_mode::normal) return true;
        if (event.is_mouse()) {
            const auto& mouse = event.mouse();
            if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed) return false;
            for (size_t row = 0; row < EditorState::ROWS; ++row)
                for (size_t col = 0; col < EditorState::COLS; ++col)
                    if (boxes_[row][col].Contain(mouse.x, mouse.y)) {
                        TakeFocus();
                        state_.click_cell(row, col);
                        return true;
                    }
            return false;
        }
        if (event == Event::ArrowUp) state_.move_cursor(-1, 0);
        else if (event == Event::ArrowDown) state_.move_cursor(1, 0);
        else if (event == Event::ArrowLeft) state_.move_cursor(0, -1);
        else if (event == Event::ArrowRight) state_.move_cursor(0, 1);
        // FTXUI 7 has no named Shift/Ctrl+Shift arrows; use only xterm's modifier encodings.
        else if (event == Event::Special("\x1b[1;2A")) state_.move_cursor(-1, 0, true);
        else if (event == Event::Special("\x1b[1;2B")) state_.move_cursor(1, 0, true);
        else if (event == Event::Special("\x1b[1;2D")) state_.move_cursor(0, -1, true);
        else if (event == Event::Special("\x1b[1;2C")) state_.move_cursor(0, 1, true);
        else if (event == Event::Special("\x1b[1;6A")) state_.select_to_edge(-1, 0);
        else if (event == Event::Special("\x1b[1;6B")) state_.select_to_edge(1, 0);
        else if (event == Event::Special("\x1b[1;6D")) state_.select_to_edge(0, -1);
        else if (event == Event::Special("\x1b[1;6C")) state_.select_to_edge(0, 1);
        else if (event == Event::Home) state_.set_cursor(state_.current_row_, 0);
        else if (event == Event::End) state_.set_cursor(state_.current_row_, EditorState::COLS - 1);
        else if (event == Event::Character('x')) state_.toggle_selection();
        else if (event == Event::Character(' ')) state_.toggleplay();
        else if (event == Event::Character('v')) state_.paste();
        else return false;
        return true;
    }
private:
    void reset_boxes() {
        for (auto& row : boxes_)
            for (auto& box : row) box = Box{0, -1, 0, -1};
    }
    EditorState& state_;
    const sequencer_status& status_;
    std::array<std::array<Box, EditorState::COLS>, EditorState::ROWS> boxes_{};
};
} // namespace

ftxui::Component make_pattern_grid(EditorState& state, const sequencer_status& status) {
    return std::make_shared<pattern_grid>(state, status);
}
} // namespace bork
