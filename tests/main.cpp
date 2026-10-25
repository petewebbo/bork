#include <cstdio>

void test_portaudio();
void test_grid_tui();
void test_pattern_switching();
void test_edit_batches();
void test_selection_clipboard();
void test_playheads();
void test_ui_components();

int main() {
    const struct {
        const char* name;
        void (*run)();
    } suites[] = {
        {"portaudio_wrapper", test_portaudio},
        {"grid_tui_model", test_grid_tui},
        {"pattern_switching", test_pattern_switching},
        {"edit_batches", test_edit_batches},
        {"selection_clipboard", test_selection_clipboard},
        {"playhead_telemetry", test_playheads},
        {"ui_components", test_ui_components},
    };
    for (const auto& suite : suites) {
        std::printf("Running %s\n", suite.name);
        std::fflush(stdout);
        suite.run();
        std::printf("Passed %s\n", suite.name);
    }
    std::puts("All test suites passed.");
}
