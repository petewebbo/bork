#include <grid_tui.hpp>
#include <sequencer.hpp>
#undef NDEBUG
#include <cassert>
#include <chrono>
#include <thread>

int main() {
    bork::sequencer audio{};
    bork::pattern_storage storage{};
    storage[0]._tracks[0]._steps[0] = {._note = 60, ._vel = 90};
    storage[1]._tracks[0]._steps[0] = {._note = 72, ._vel = 80};
    audio.current_pattern() = storage[0];
    bork::command_queue commands(16);
    bork::GridWidget ui(storage, commands, audio._patterns);
    audio._transport._track_transport[0]._next_step = 9;
    auto& sampler = static_cast<bork::sampler&>(*audio._machines[0].get());
    sampler._playing = true;
    sampler._play_counter = 3;

    assert(ui.select_pattern(0)); // Same selection is a no-op.
    assert(commands.empty());
    assert(ui.pattern_input('p'));
    assert(ui.awaiting_pattern_);
    assert(ui.pattern_input('x') && ui.pattern_input(' ') && ui.pattern_input('q'));
    assert(ui.pattern_input('9') && ui.awaiting_pattern_ && commands.empty());
    assert(ui.pattern_input(27));
    assert(!ui.awaiting_pattern_ && commands.empty());
    assert(ui.pattern_input('P'));
    assert(ui.pattern_input('2'));
    assert(!ui.awaiting_pattern_);
    assert(ui.selected_pattern_ == 1 && ui.playing_pattern_ == 0 && ui.switch_pending_);
    assert(ui.note_number(0, 0) == 72);
    assert(audio.current_pattern()._tracks[0]._steps[0]._note == 60);
    assert(ui.pattern_input('p') && ui.pattern_input('3'));
    assert(!ui.awaiting_pattern_ && ui.message_); // Reject, leave number-entry mode.
    assert(ui.selected_pattern_ == 1); // Cannot overwrite submitted slot.
    assert(ui.togglecell()); // Edit new stored pattern before audio adopts it.
    assert(storage[1]._tracks[0]._steps[0]._note == 0);
    assert(storage[0]._tracks[0]._steps[0]._note == 60);
    assert(audio.process_commands(commands) == 2);
    assert(!audio._is_playing); // Switching works while stopped.
    assert(audio.current_pattern()._tracks[0]._steps[0]._note == 0);
    assert(audio._transport._track_transport[0]._next_step == 9);
    assert(sampler._playing && sampler._play_counter == 3);
    assert(ui.switch_pending_); // No UI state changes until it polls completion.
    ui.poll_notifications();
    assert(!ui.switch_pending_ && ui.playing_pattern_ == 1);
    assert(ui.select_pattern(0));
    audio.process_commands(commands);
    ui.poll_notifications();
    assert(ui.note_number(0, 0) == 60);
    assert(audio.current_pattern()._tracks[0]._steps[0]._note == 60);
    // Same ID, older generation must not affect a later activation.
    audio.apply_command({._type = bork::sequencer_command::type::clear_track});
    assert(audio.current_pattern()._tracks[0]._steps[0]._note == 60);

    // Submission failure leaves displayed pattern/storage unchanged and spare reusable.
    bork::command_queue full(1);
    bork::sequencer other{};
    bork::GridWidget blocked(storage, full, other._patterns);
    assert(full.try_push({._type = bork::sequencer_command::type::toggle_play}));
    assert(!blocked.select_pattern(1));
    assert(blocked.selected_pattern_ == 0 && !blocked.switch_pending_);
    other.process_commands(full);
    assert(blocked.select_pattern(1));
    other.process_commands(full);
    blocked.poll_notifications();
    assert(blocked.playing_pattern_ == 1);

    // Actual cross-thread slot publication/reclamation across repeated swaps.
    std::atomic<bool> stop{false};
    std::thread consumer([&] {
        while (!stop.load()) {
            audio.process_commands(commands);
            std::this_thread::yield();
        }
    });
    for (size_t n = 0; n < 1000; ++n) {
        const size_t id = (n + 1) % 8;
        assert(ui.select_pattern(id));
        assert(ui.togglecell());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (ui.switch_pending_) {
            ui.poll_notifications();
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
    }
    // Stop/join before inspecting audio state. Flush any final edit on this thread.
    stop.store(true);
    consumer.join();
    audio.process_commands(commands);
    assert(audio._pattern_id == ui.selected_pattern_);
    assert(audio.current_pattern()._tracks[0]._steps[0]._note == ui.note_number(0, 0));
}
