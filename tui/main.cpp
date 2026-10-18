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
        uint8_t current_pattern = 0;
        while (!stop.load()) {
            const auto count = commands.size();
            for (size_t i = 0; i < count; ++i) {
                const auto& command = *commands.front();
                if (command.type_ == bork::sequencer_command::type::activate_pattern) {
                    if (exchange.completed.size() == exchange.completed.capacity()) break;
                    const auto result = command.pattern_id_ == current_pattern
                        ? bork::pattern_switch_result::cancelled : bork::pattern_switch_result::switched;
                    current_pattern = command.pattern_id_;
                    exchange.completed.try_push(bork::pattern_switch_ack{
                        current_pattern, result, command.request_id_});
                } else if (command.type_ == bork::sequencer_command::type::update_pattern) {
                    // Like audio, consume exactly one payload per command marker.
                    if (exchange.edits.front()) exchange.edits.pop();
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
