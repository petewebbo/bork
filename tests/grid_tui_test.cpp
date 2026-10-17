#include <grid_tui.hpp>
#include <midi_notes.hpp>
#include <string_view>
#include <sequencer.hpp>
#undef NDEBUG
#include <cassert>
#include <limits>
#include <thread>

int main() {
    {
        bork::sequencer switching{};
        switching._patterns.slots[1]._tracks[0]._steps[0] = {._note = 72, ._vel = 90};
        switching._transport._track_transport[0]._next_step = 7;
        switching.apply_command({._type = bork::sequencer_command::type::activate_pattern,
                                 ._pattern_id = 1, ._slot = 1});
        assert(switching._current_slot == 1 && !switching._is_playing);
        assert(switching.current_pattern()._tracks[0]._steps[0]._note == 72);
        assert(switching._transport._track_transport[0]._next_step == 7);
        const auto* ack = switching._patterns.completed.front();
        assert(ack && ack->retired_slot == 0 && ack->pattern_id == 1);
        switching._patterns.completed.pop();
        switching.apply_command({._type = bork::sequencer_command::type::set_step,
                                 ._value = {._note = 1}}); // Different pattern.
        assert(switching.current_pattern()._tracks[0]._steps[0]._note == 72);
        switching.apply_command({._type = bork::sequencer_command::type::set_step,
                                 ._value = {._note = 74}, ._pattern_id = 1});
        assert(switching.current_pattern()._tracks[0]._steps[0]._note == 74);
    }
    bork::sequencer_status status;
    const auto& published = status;
    assert(!published.playing.load(std::memory_order_relaxed));
    std::thread publisher([&] { status.playing.store(true, std::memory_order_relaxed); });
    publisher.join();
    assert(published.playing.load(std::memory_order_relaxed));
    status.playing.store(false, std::memory_order_relaxed);
    assert(!published.playing.load(std::memory_order_relaxed));
    assert(std::string_view(bork::lookup_midi_note(0)->name.data()) == "C-1");
    assert(std::string_view(bork::lookup_midi_note(60)->name.data()) == "C4");
    assert(std::string_view(bork::lookup_midi_note(61)->name.data()) == "C#4");
    assert(std::string_view(bork::lookup_midi_note(127)->name.data()) == "G9");
    assert(std::abs(bork::lookup_midi_note(69)->frequency - 440.0) < 0.000001);
    assert(!bork::lookup_midi_note(-1) && !bork::lookup_midi_note(128));
    {
        bork::sequencer pitched{};
        auto& pitched_pattern = pitched.current_pattern();
        pitched_pattern._tracks[0]._steps[0] = {._note = 61, ._vel = 90};
        bork::command_queue queue(8);
        bork::pattern_storage stored{};
        stored[0] = pitched_pattern;
        bork::GridWidget ui(stored, queue, pitched._patterns);
        assert(ui.note_number(0, 0) == 61);
        assert(ui.togglecell());
        assert(ui.note_number(0, 0) == 0);
        assert(ui.togglecell());
        assert(ui.note_number(0, 0) == 61);
        pitched.process_commands(queue);
        assert(pitched_pattern._tracks[0]._steps[0]._note == 61);
        assert(ui.clearrow());
        assert(ui.togglecell());
        pitched.process_commands(queue);
        assert(pitched_pattern._tracks[0]._steps[0]._note == 61);
    }
    bork::sequencer engine{};
    auto& pattern = engine.current_pattern();
    pattern._tracks[0]._steps[3] = {._note = 1, ._vel = 80, ._len = 12};
    pattern._tracks[4]._steps[14] = {._note = 1, ._vel = 40};
    bork::command_queue commands(bork::COMMAND_QUEUE_CAPACITY);
    bork::pattern_storage storage{};
    storage[0] = pattern;
    bork::GridWidget widget(storage, commands, engine._patterns);
    assert(!engine._is_playing);
    assert(widget.toggleplay());
    assert(engine.process_commands(commands) == 1);
    assert(engine._is_playing);
    assert(widget.toggleplay());
    assert(engine.process_commands(commands) == 1);
    assert(!engine._is_playing);
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
    assert(pattern._tracks[2]._steps[0]._note == bork::GridWidget::kDefaultNoteNumber);
    assert(pattern._tracks[2]._steps[0]._vel == 100);
    assert(widget.clearrow());
    assert(engine.process_commands(commands) == 1);
    for (const auto& step : pattern._tracks[2]._steps) assert(step._note == 0);
    for (bool active : widget.grid_state_[2]) assert(!active);
    assert(pattern._tracks[4]._steps[14]._note == 1);

    // Full queue rejects both cell and whole-row edits without changing the UI.
    bork::command_queue tiny(1);
    bork::pattern_storage full_storage{};
    full_storage[0] = pattern;
    bork::pattern_exchange full_exchange;
    bork::GridWidget full_widget(full_storage, tiny, full_exchange);
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
