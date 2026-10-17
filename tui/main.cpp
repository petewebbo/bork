#include "grid_tui.hpp"
#include <chrono>
#include <thread>

// UI-only preview: a dummy consumer discards commands instead of rendering audio.
int main() {
    bork::pattern pattern{};
    bork::command_queue commands(bork::COMMAND_QUEUE_CAPACITY);
    bork::GridWidget widget(pattern, commands);
    std::atomic<bool> stop{false};
    std::thread consumer([&] {
        while (!stop.load()) {
            const auto count = commands.size();
            for (size_t i = 0; i < count; ++i) commands.pop();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    try {
        bork::run_grid_tui(widget);
    } catch (...) {
        stop.store(true);
        consumer.join();
        throw;
    }
    stop.store(true);
    consumer.join();
}
