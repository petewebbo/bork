#pragma once

#include <ftxui/component/component_base.hpp>
#include <functional>

namespace bork {
class EditorState;
struct sequencer_status;

ftxui::Component make_editor_component(EditorState& state, const sequencer_status& status,
                                       std::function<void()> on_quit);
} // namespace bork
