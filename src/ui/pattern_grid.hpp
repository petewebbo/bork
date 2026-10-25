#pragma once

#include <ftxui/component/component_base.hpp>

namespace bork {
class EditorState;
struct sequencer_status;

ftxui::Component make_pattern_grid(EditorState& state, const sequencer_status& status);
} // namespace bork
