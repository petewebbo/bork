#include "grid_tui.hpp"

// Standalone UI preview; the main bork target supplies its live sequencer pattern.
int main() {
    bork::pattern pattern{};
    bork::run_grid_tui(pattern);
}
