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
        switching.patterns_[1].tracks_[0].steps_[0] = {.note_ = 72, .vel_ = 90};
        switching.transport_.track_transport_[0].next_step_ = 7;
        switching.apply_command({.type_ = bork::sequencer_command::type::activate_pattern,
                                 .pattern_id_ = 1});
        assert(switching.pattern_id_ == 1 && !switching.is_playing_);
        assert(switching.current_pattern().tracks_[0].steps_[0].note_ == 72);
        assert(switching.transport_.track_transport_[0].next_step_ == 7);
        const auto* ack = switching.exchange_.completed.front();
        assert(ack && ack->pattern_id == 1 && ack->result == bork::pattern_switch_result::switched);
        switching.exchange_.completed.pop();
        switching.apply_command({.type_ = bork::sequencer_command::type::set_step,
                                 .value_ = {.note_ = 1}}); // Different pattern.
        assert(switching.current_pattern().tracks_[0].steps_[0].note_ == 72);
        switching.apply_command({.type_ = bork::sequencer_command::type::set_step,
                                 .value_ = {.note_ = 74}, .pattern_id_ = 1});
        assert(switching.current_pattern().tracks_[0].steps_[0].note_ == 74);
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
        pitched_pattern.tracks_[0].steps_[0] = {.note_ = 61, .vel_ = 90};
        bork::command_queue queue(8);
        bork::pattern_storage stored{};
        stored[0] = pitched_pattern;
        bork::GridWidget ui(stored, queue, pitched.exchange_);
        assert(ui.note_number(0, 0) == 61);
        assert(ui.togglecell());
        assert(ui.note_number(0, 0) == 0);
        assert(ui.togglecell());
        assert(ui.note_number(0, 0) == 61);
        pitched.process_commands(queue);
        assert(pitched_pattern.tracks_[0].steps_[0].note_ == 61);
        assert(ui.clearrow());
        assert(ui.togglecell());
        pitched.process_commands(queue);
        assert(pitched_pattern.tracks_[0].steps_[0].note_ == 61);
    }
    bork::sequencer engine{};
    auto& pattern = engine.current_pattern();
    pattern.tracks_[0].steps_[3] = {.note_ = 1, .vel_ = 80, .len_ = 12};
    pattern.tracks_[4].steps_[14] = {.note_ = 1, .vel_ = 40};
    bork::command_queue commands(bork::COMMAND_QUEUE_CAPACITY);
    bork::pattern_storage storage{};
    storage[0] = pattern;
    bork::GridWidget widget(storage, commands, engine.exchange_);
    assert(!engine.is_playing_);
    assert(widget.toggleplay());
    assert(engine.process_commands(commands) == 1);
    assert(engine.is_playing_);
    assert(widget.toggleplay());
    assert(engine.process_commands(commands) == 1);
    assert(!engine.is_playing_);
    assert(widget.grid_state_[0][3]);
    assert(widget.grid_state_[4][14]);
    assert(!widget.grid_state_[2][0]);
    widget.current_col_ = 3;
    assert(widget.togglecell());
    // UI edits must not mutate live sequencer state before consumption.
    assert(pattern.tracks_[0].steps_[3].note_ == 1);
    assert(!widget.grid_state_[0][3]);
    assert(engine.process_commands(commands) == 1);
    assert(pattern.tracks_[0].steps_[3].note_ == 0);
    assert(pattern.tracks_[0].steps_[3].vel_ == 80);
    assert(widget.togglecell());
    assert(engine.process_commands(commands) == 1);
    assert(pattern.tracks_[0].steps_[3].note_ == 1);
    assert(pattern.tracks_[0].steps_[3].len_ == 12);
    widget.current_row_ = 2;
    widget.current_col_ = 0;
    assert(widget.togglecell());
    assert(widget.clearrow());
    assert(widget.togglecell());
    assert(engine.process_commands(commands) == 3); // FIFO: enable, clear, enable
    assert(pattern.tracks_[2].steps_[0].note_ == bork::GridWidget::kDefaultNoteNumber);
    assert(pattern.tracks_[2].steps_[0].vel_ == 100);
    assert(widget.clearrow());
    assert(engine.process_commands(commands) == 1);
    for (const auto& step : pattern.tracks_[2].steps_) assert(step.note_ == 0);
    for (bool active : widget.grid_state_[2]) assert(!active);
    assert(pattern.tracks_[4].steps_[14].note_ == 1);

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
        assert(commands.try_push(bork::sequencer_command{.type_ = bork::sequencer_command::type::clear_track}));
    assert(engine.process_commands(commands) == bork::COMMAND_QUEUE_CAPACITY);
    assert(!commands.front());
    assert(engine.process_commands(commands) == 0);

    // Invalid indices and tempo values are ignored, not used as array indices.
    engine.apply_command({.type_ = bork::sequencer_command::type::set_step, .track_ = 255, .step_ = 255});
    engine.apply_command({.type_ = bork::sequencer_command::type::clear_track, .track_ = 255});
    engine.transport_.sample_rate_ = 48000;
    engine.apply_command({.type_ = bork::sequencer_command::type::set_tempo, .tempo_ = 150});
    const auto samples_per_tick = engine.transport_.track_transport_[0].samples_per_tick_;
    assert(std::abs(samples_per_tick - 100.0f) < 0.001f);
    engine.apply_command({.type_ = bork::sequencer_command::type::set_tempo, .tempo_ = std::numeric_limits<float>::quiet_NaN()});
    assert(engine.transport_.track_transport_[0].samples_per_tick_ == samples_per_tick);

    // Exercise actual producer/consumer publication and repeated ring wraparound.
    bork::command_queue concurrent(8);
    std::thread producer([&] {
        for (int i = 1; i <= 10000; ++i) {
            const bork::sequencer_command command{
                .type_ = bork::sequencer_command::type::set_step,
                .value_ = {.note_ = i},
            };
            while (!concurrent.try_push(command)) std::this_thread::yield();
        }
    });
    for (int i = 1; i <= 10000; ++i) {
        const bork::sequencer_command* command;
        while (!(command = concurrent.front())) std::this_thread::yield();
        assert(command->value_.note_ == i);
        concurrent.pop();
    }
    producer.join();
}
