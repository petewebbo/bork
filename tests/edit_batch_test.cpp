#include <sequencer.hpp>
#undef NDEBUG
#include <cassert>
#include <thread>

int main() {
    bork::sequencer audio;
    bork::command_queue commands(16);
    for (size_t i = 0; i < bork::PATTERN_EDIT_QUEUE_CAPACITY; ++i) {
        bork::pattern_edit edit{};
        edit.value.tracks_[7].steps_[15] = {.note_ = static_cast<int>(60 + i), .vel_ = 90, .len_ = 12};
        edit.value.tracks_[7].size_ = 16;
        assert(bork::try_submit_pattern_edit(commands, audio.patterns_.edits, edit));
    }
    // Payload-full rejects without publishing an orphan command marker.
    assert(!bork::try_submit_pattern_edit(commands, audio.patterns_.edits, {}));
    assert(commands.size() == bork::PATTERN_EDIT_QUEUE_CAPACITY);
    assert(audio.current_pattern().tracks_[7].steps_[15].note_ == 0);
    assert(audio.process_commands(commands) == bork::PATTERN_EDIT_QUEUE_CAPACITY);
    assert(audio.current_pattern().tracks_[7].steps_[15].note_ == 63);
    assert(audio.current_pattern().tracks_[7].size_ == 16);
    assert(audio.patterns_.edits.empty());

    // Command-full rejects without publishing an orphan payload.
    bork::command_queue full(1);
    assert(full.try_push({.type_ = bork::sequencer_command::type::toggle_play}));
    assert(!bork::try_submit_pattern_edit(full, audio.patterns_.edits, {}));
    assert(audio.patterns_.edits.empty() && full.size() == 1);
    audio.process_commands(full);
    assert(bork::try_submit_pattern_edit(full, audio.patterns_.edits, {}));
    audio.process_commands(full); // Space reusable immediately, no completion polling.
    assert(audio.patterns_.edits.empty());

    // A payload for another pattern is consumed; the next payload stays aligned.
    bork::pattern_edit edit{};
    edit.pattern_id = 7;
    edit.value.tracks_[0].steps_[0].note_ = 1;
    assert(bork::try_submit_pattern_edit(commands, audio.patterns_.edits, edit));
    edit.pattern_id = 0;
    edit.value.tracks_[0].steps_[0].note_ = 67;
    assert(bork::try_submit_pattern_edit(commands, audio.patterns_.edits, edit));
    audio.process_commands(commands);
    assert(audio.current_pattern().tracks_[0].steps_[0].note_ == 67);
    assert(audio.patterns_.edits.empty());

    // Activation, replacement and ordinary edits obey one common FIFO order.
    assert(commands.try_push({.type_ = bork::sequencer_command::type::activate_pattern,
                              .pattern_id_ = 1, .slot_ = 1}));
    edit.pattern_id = 1;
    edit.value.tracks_[0].steps_[0].note_ = 72;
    assert(bork::try_submit_pattern_edit(commands, audio.patterns_.edits, edit));
    assert(commands.try_push({.type_ = bork::sequencer_command::type::set_step,
                              .step_ = 1, .value_ = {.note_ = 80},
                              .pattern_id_ = 1}));
    audio.process_commands(commands);
    assert(audio.current_pattern().tracks_[0].steps_[0].note_ == 72);
    assert(audio.current_pattern().tracks_[0].steps_[1].note_ == 80);
    assert(audio.patterns_.edits.empty());
    assert(audio.patterns_.completed.front());
    audio.patterns_.completed.pop();

    // Actual concurrent publication, queue-full retries and repeated ring wraps.
    constexpr int count = 10000;
    std::thread consumer([&] {
        for (int i = 1; i <= count; ++i) {
            const bork::sequencer_command* command;
            while (!(command = commands.front())) std::this_thread::yield();
            assert(command->type_ == bork::sequencer_command::type::update_pattern);
            audio.apply_command(*command);
            commands.pop();
            assert(audio.current_pattern().tracks_[0].steps_[0].note_ == i);
        }
    });
    for (int i = 1; i <= count; ++i) {
        edit.value.tracks_[0].steps_[0].note_ = i;
        while (!bork::try_submit_pattern_edit(commands, audio.patterns_.edits, edit))
            std::this_thread::yield();
    }
    consumer.join();
    assert(commands.empty() && audio.patterns_.edits.empty());
}
