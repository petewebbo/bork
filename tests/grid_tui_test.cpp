#include <grid_tui.hpp>
#include <sequencer.hpp>
#undef NDEBUG
#include <cassert>
#include <limits>
#include <thread>

int main() {
    bork::sequencer engine{};
    auto& pattern = engine._pattern;
    pattern._tracks[0]._steps[3] = {._note = 1, ._vel = 80, ._len = 12};
    pattern._tracks[4]._steps[14] = {._note = 1, ._vel = 40};
    bork::command_queue commands(bork::COMMAND_QUEUE_CAPACITY);
    bork::GridWidget widget(pattern, commands);
    assert(widget.grid_state_[0][3]);
    assert(widget.grid_state_[4][14]);
    assert(!widget.grid_state_[2][0]);
    widget.current_col_ = 3;
    assert(widget.togglecell());
    // UI edits must not mutate live sequencer state before consumption.
    assert(pattern._tracks[0]._steps[3]._note == 1);
    assert(!widget.grid_state_[0][3]);
    assert(engine.process_commands(commands) == 1);
    assert(pattern._tracks[0]._steps[3]._note == 0);
    assert(pattern._tracks[0]._steps[3]._vel == 80);
    assert(widget.togglecell());
    assert(engine.process_commands(commands) == 1);
    assert(pattern._tracks[0]._steps[3]._note == 1);
    assert(pattern._tracks[0]._steps[3]._len == 12);
    widget.current_row_ = 2;
    widget.current_col_ = 0;
    assert(widget.togglecell());
    assert(widget.clearrow());
    assert(widget.togglecell());
    assert(engine.process_commands(commands) == 3); // FIFO: enable, clear, enable
    assert(pattern._tracks[2]._steps[0]._note == 1);
    assert(pattern._tracks[2]._steps[0]._vel == 100);
    assert(widget.clearrow());
    assert(engine.process_commands(commands) == 1);
    for (const auto& step : pattern._tracks[2]._steps) assert(step._note == 0);
    for (bool active : widget.grid_state_[2]) assert(!active);
    assert(pattern._tracks[4]._steps[14]._note == 1);

    // Full queue rejects both cell and whole-row edits without changing the UI.
    bork::command_queue tiny(1);
    bork::GridWidget full_widget(pattern, tiny);
    assert(full_widget.togglecell());
    assert(!full_widget.togglecell());
    assert(full_widget.queue_full_ && full_widget.grid_state_[0][0]);
    full_widget.current_col_ = 3;
    assert(!full_widget.clearrow());
    assert(full_widget.grid_state_[0][3]);
    assert(engine.process_commands(tiny) == 1);
    assert(full_widget.clearrow());
    assert(!full_widget.queue_full_);
    assert(engine.process_commands(tiny) == 1);

    // Process the entire captured backlog, including more than the former 64 cap.
    for (size_t i = 0; i < bork::COMMAND_QUEUE_CAPACITY; ++i)
        assert(commands.try_push(bork::sequencer_command{._type = bork::sequencer_command::type::clear_track}));
    assert(engine.process_commands(commands) == bork::COMMAND_QUEUE_CAPACITY);
    assert(!commands.front());
    assert(engine.process_commands(commands) == 0);

    // Invalid indices and tempo values are ignored, not used as array indices.
    engine.apply_command({._type = bork::sequencer_command::type::set_step, ._track = 255, ._step = 255});
    engine.apply_command({._type = bork::sequencer_command::type::clear_track, ._track = 255});
    engine._transport._sample_rate = 48000;
    engine.apply_command({._type = bork::sequencer_command::type::set_tempo, ._tempo = 150});
    const auto samples_per_tick = engine._transport._track_transport[0]._samples_per_tick;
    assert(std::abs(samples_per_tick - 100.0f) < 0.001f);
    engine.apply_command({._type = bork::sequencer_command::type::set_tempo, ._tempo = std::numeric_limits<float>::quiet_NaN()});
    assert(engine._transport._track_transport[0]._samples_per_tick == samples_per_tick);

    // Exercise actual producer/consumer publication and repeated ring wraparound.
    bork::command_queue concurrent(8);
    std::thread producer([&] {
        for (int i = 1; i <= 10000; ++i) {
            const bork::sequencer_command command{
                ._type = bork::sequencer_command::type::set_step,
                ._value = {._note = i},
            };
            while (!concurrent.try_push(command)) std::this_thread::yield();
        }
    });
    for (int i = 1; i <= 10000; ++i) {
        const bork::sequencer_command* command;
        while (!(command = concurrent.front())) std::this_thread::yield();
        assert(command->_value._note == i);
        concurrent.pop();
    }
    producer.join();
}
