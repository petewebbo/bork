#include "grid_tui.hpp"
#include <chrono>
#include <thread>

// UI-only preview: a dummy consumer discards commands instead of rendering audio.
int main() {
    bork::pattern_storage storage{};
    bork::pattern_exchange exchange;
    bork::command_queue commands(bork::COMMAND_QUEUE_CAPACITY);
    bork::GridWidget widget(storage, commands, exchange);
    bork::sequencer_status status; // Preview has no audio: always stopped.
    std::atomic<bool> stop{false};
    std::thread consumer([&] {
        uint8_t current_slot = 0;
        while (!stop.load()) {
            const auto count = commands.size();
            for (size_t i = 0; i < count; ++i) {
                const auto& command = *commands.front();
                if (command._type == bork::sequencer_command::type::activate_pattern) {
                    const bork::pattern_switched ack{command._generation, command._pattern_id, current_slot};
                    current_slot = command._slot;
                    exchange.completed.try_push(ack);
                } else if (command._type == bork::sequencer_command::type::replace_pattern) {
                    exchange.edits_completed.try_push(command._slot);
                }
                commands.pop();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    try {
        bork::run_grid_tui(widget, status);
    } catch (...) {
        stop.store(true);
        consumer.join();
        throw;
    }
    stop.store(true);
    consumer.join();
}
