#include <sequencer.hpp>
#undef NDEBUG
#include <cassert>
#include <thread>

int main() {
    bork::sequencer audio;
    bork::command_queue commands(16);
    for (size_t i = 0; i < bork::PATTERN_EDIT_QUEUE_CAPACITY; ++i) {
        bork::pattern_edit edit{};
        edit.value._tracks[7]._steps[15] = {._note = static_cast<int>(60 + i), ._vel = 90, ._len = 12};
        edit.value._tracks[7]._size = 16;
        assert(bork::try_submit_pattern_edit(commands, audio._patterns.edits, edit));
    }
    // Payload-full rejects without publishing an orphan command marker.
    assert(!bork::try_submit_pattern_edit(commands, audio._patterns.edits, {}));
    assert(commands.size() == bork::PATTERN_EDIT_QUEUE_CAPACITY);
    assert(audio.current_pattern()._tracks[7]._steps[15]._note == 0);
    assert(audio.process_commands(commands) == bork::PATTERN_EDIT_QUEUE_CAPACITY);
    assert(audio.current_pattern()._tracks[7]._steps[15]._note == 63);
    assert(audio.current_pattern()._tracks[7]._size == 16);
    assert(audio._patterns.edits.empty());

    // Command-full rejects without publishing an orphan payload.
    bork::command_queue full(1);
    assert(full.try_push({._type = bork::sequencer_command::type::toggle_play}));
    assert(!bork::try_submit_pattern_edit(full, audio._patterns.edits, {}));
    assert(audio._patterns.edits.empty() && full.size() == 1);
    audio.process_commands(full);
    assert(bork::try_submit_pattern_edit(full, audio._patterns.edits, {}));
    audio.process_commands(full); // Space reusable immediately, no completion polling.
    assert(audio._patterns.edits.empty());

    // A payload for another pattern is consumed; the next payload stays aligned.
    bork::pattern_edit edit{};
    edit.pattern_id = 7;
    edit.value._tracks[0]._steps[0]._note = 1;
    assert(bork::try_submit_pattern_edit(commands, audio._patterns.edits, edit));
    edit.pattern_id = 0;
    edit.value._tracks[0]._steps[0]._note = 67;
    assert(bork::try_submit_pattern_edit(commands, audio._patterns.edits, edit));
    audio.process_commands(commands);
    assert(audio.current_pattern()._tracks[0]._steps[0]._note == 67);
    assert(audio._patterns.edits.empty());

    // Activation, replacement and ordinary edits obey one common FIFO order.
    assert(commands.try_push({._type = bork::sequencer_command::type::activate_pattern,
                              ._pattern_id = 1, ._slot = 1}));
    edit.pattern_id = 1;
    edit.value._tracks[0]._steps[0]._note = 72;
    assert(bork::try_submit_pattern_edit(commands, audio._patterns.edits, edit));
    assert(commands.try_push({._type = bork::sequencer_command::type::set_step,
                              ._step = 1, ._value = {._note = 80},
                              ._pattern_id = 1}));
    audio.process_commands(commands);
    assert(audio.current_pattern()._tracks[0]._steps[0]._note == 72);
    assert(audio.current_pattern()._tracks[0]._steps[1]._note == 80);
    assert(audio._patterns.edits.empty());
    assert(audio._patterns.completed.front());
    audio._patterns.completed.pop();

    // Actual concurrent publication, queue-full retries and repeated ring wraps.
    constexpr int count = 10000;
    std::thread consumer([&] {
        for (int i = 1; i <= count; ++i) {
            const bork::sequencer_command* command;
            while (!(command = commands.front())) std::this_thread::yield();
            assert(command->_type == bork::sequencer_command::type::update_pattern);
            audio.apply_command(*command);
            commands.pop();
            assert(audio.current_pattern()._tracks[0]._steps[0]._note == i);
        }
    });
    for (int i = 1; i <= count; ++i) {
        edit.value._tracks[0]._steps[0]._note = i;
        while (!bork::try_submit_pattern_edit(commands, audio._patterns.edits, edit))
            std::this_thread::yield();
    }
    consumer.join();
    assert(commands.empty() && audio._patterns.edits.empty());
}
