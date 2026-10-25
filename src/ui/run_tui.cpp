#include "ui/run_tui.hpp"
#include "ui/editor_component.hpp"
#include "ui/editor_state.hpp"
#include <ftxui/component/app.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/loop.hpp>
#include <chrono>
#include <thread>

namespace bork {
void run_tui(EditorState& editor, const sequencer_status& status) {
    auto app = ftxui::App::Fullscreen();
    auto component = make_editor_component(editor, status, app.ExitLoopClosure());
    ftxui::Loop loop(&app, component);
    while (!loop.HasQuitted()) {
        const auto next_refresh = std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
        editor.poll_notifications();
        app.PostEvent(ftxui::Event::Custom);
        loop.RunOnce();
        std::this_thread::sleep_until(next_refresh);
    }
}
} // namespace bork
