#include "ui/editor_component.hpp"
#include "ui/editor_state.hpp"
#include "ui/pattern_grid.hpp"
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>
#include <cstdio>
#include <utility>

namespace bork {
namespace {
using namespace ftxui;

const char* status_text(const EditorState& state) {
    switch (state.mode_) {
    case input_mode::pattern_select: return "Select pattern 1-8 | Esc: cancel";
    case input_mode::copy: return state.selection().multiple()
        ? "Copy: p = pattern | s = selection | Esc = cancel"
        : "Copy: p = pattern | s = step | Esc = cancel";
    case input_mode::edit_select: return "Edit field: n = note | v = velocity | l = length | Esc = cancel";
    case input_mode::normal: break;
    }
    if (state.message_) return state.message_;
    if (state.loading()) return "Loading editing copy - edits and copy disabled";
    return state.switch_pending_ ? "Pattern switch pending" : "";
}

// Use the actual allocated DOM box, including headless screens and resize frames.
class responsive_layout final : public Node {
public:
    responsive_layout(Element full, Element small, bool& fits)
        : Node({std::move(full), std::move(small)}), fits_(fits) {}
    void SetBox(Box box) override {
        Node::SetBox(box);
        fits_ = box.x_max - box.x_min + 1 >= 90 && box.y_max - box.y_min + 1 >= 15;
        children_[fits_ ? 0 : 1]->SetBox(box);
    }
    void Render(Screen& screen) override { children_[fits_ ? 0 : 1]->Render(screen); }
private:
    bool& fits_;
};

class editor_component final : public ComponentBase {
public:
    editor_component(EditorState& state, const sequencer_status& status, std::function<void()> on_quit)
        : state_(state), status_(status), on_quit_(std::move(on_quit)),
          grid_(make_pattern_grid(state, status)) { Add(grid_); }
    Element OnRender() override {
        auto status = hbox({text(" "), text(status_text(state_)), filler()});
        if (Terminal::ColorSupport() == Terminal::Palette1) status |= inverted;
        else status = status | bgcolor(Color::GrayDark) | color(Color::White);
        char heading[128];
        std::snprintf(heading, sizeof(heading), "BORK - PATTERN %02d | %s | Audio: %02d%s",
                      static_cast<int>(state_.selected_pattern_ + 1),
                      status_.playing.load(std::memory_order_relaxed) ? "PLAYING" : "STOPPED",
                      static_cast<int>(state_.playing_pattern_ + 1),
                      state_.switch_pending_ ? " | Switch pending" : "");
        auto full = vbox({status,
            hbox({text("  "), text(heading) | bold}),
            hbox({text("  "), text("Shift+arrows: select | x: toggle | c: copy | v: paste | P: pattern | e: edit field")}),
            grid_->Render(), filler()});
        auto small = vbox({status, text("Resize terminal (90 x 15) | q quits"), filler()});
        return std::make_shared<responsive_layout>(std::move(full), std::move(small), fits_);
    }
    bool OnEvent(Event event) override {
        // Modes get first refusal, before mouse, navigation, transport, or quit.
        if (state_.mode_ != input_mode::normal) {
            if (event == Event::Escape) state_.input_mode_key(27);
            else if (event.is_character() && event.character().size() == 1)
                state_.input_mode_key(static_cast<unsigned char>(event.character()[0]));
            return true;
        }
        if (event == Event::Escape) return true;
        if (event.is_character() && event.character().size() == 1) {
            const auto key = static_cast<unsigned char>(event.character()[0]);
            if (state_.input_mode_key(key)) return true;
            if (key == 'q' || key == 'Q') {
                on_quit_();
                return true;
            }
        }
        return fits_ && grid_->OnEvent(std::move(event));
    }
private:
    EditorState& state_;
    const sequencer_status& status_;
    std::function<void()> on_quit_;
    Component grid_;
    bool fits_{};
};
} // namespace

ftxui::Component make_editor_component(EditorState& state, const sequencer_status& status,
                                       std::function<void()> on_quit) {
    return std::make_shared<editor_component>(state, status, std::move(on_quit));
}
} // namespace bork
