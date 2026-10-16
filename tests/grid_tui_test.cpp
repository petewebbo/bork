#include <grid_tui.hpp>
#undef NDEBUG
#include <cassert>

int main() {
    bork::pattern pattern{};
    pattern._tracks[0]._steps[3] = {._note = 1, ._vel = 80, ._len = 12};
    pattern._tracks[4]._steps[14] = {._note = 1, ._vel = 40};
    bork::GridWidget widget(pattern);
    assert(widget.grid_state_[0][3]);
    assert(widget.grid_state_[4][14]);
    assert(!widget.grid_state_[2][0]);
    widget.current_col_ = 3;
    widget.togglecell();
    assert(pattern._tracks[0]._steps[3]._note == 0);
    assert(pattern._tracks[0]._steps[3]._vel == 80);
    widget.togglecell();
    assert(pattern._tracks[0]._steps[3]._note == 1);
    assert(pattern._tracks[0]._steps[3]._len == 12);
    widget.current_row_ = 2;
    widget.current_col_ = 0;
    widget.togglecell();
    assert(pattern._tracks[2]._steps[0]._note == 1);
    assert(pattern._tracks[2]._steps[0]._vel == 100);
    widget.clearrow();
    for (const auto& step : pattern._tracks[2]._steps) assert(step._note == 0);
    for (bool active : widget.grid_state_[2]) assert(!active);
    assert(pattern._tracks[4]._steps[14]._note == 1);
}
