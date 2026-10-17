#include <grid_tui.hpp>
#include <sequencer.hpp>
#undef NDEBUG
#include <cassert>
#include <chrono>
#include <thread>

int main() {
    bork::sequencer audio{};
    bork::pattern_storage storage{};
    storage[0].tracks_[0].steps_[0] = {.note_ = 60, .vel_ = 90};
    storage[1].tracks_[0].steps_[0] = {.note_ = 72, .vel_ = 80};
    audio.current_pattern() = storage[0];
    bork::command_queue commands(16);
    bork::GridWidget ui(storage, commands, audio.patterns_);
    audio.transport_.track_transport_[0].next_step_ = 9;
    auto& sampler = static_cast<bork::sampler&>(*audio.machines_[0].get());
    sampler.playing_ = true;
    sampler.play_counter_ = 3;

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
    assert(audio.current_pattern().tracks_[0].steps_[0].note_ == 60);
    assert(ui.pattern_input('p') && ui.pattern_input('3'));
    assert(!ui.awaiting_pattern_ && ui.message_); // Reject, leave number-entry mode.
    assert(ui.selected_pattern_ == 1); // Cannot overwrite submitted slot.
    assert(ui.togglecell()); // Edit new stored pattern before audio adopts it.
    assert(storage[1].tracks_[0].steps_[0].note_ == 0);
    assert(storage[0].tracks_[0].steps_[0].note_ == 60);
    assert(audio.process_commands(commands) == 2);
    assert(!audio.is_playing_); // Switching works while stopped.
    assert(audio.current_pattern().tracks_[0].steps_[0].note_ == 0);
    assert(audio.transport_.track_transport_[0].next_step_ == 9);
    assert(sampler.playing_ && sampler.play_counter_ == 3);
    assert(ui.switch_pending_); // No UI state changes until it polls completion.
    ui.poll_notifications();
    assert(!ui.switch_pending_ && ui.playing_pattern_ == 1);
    assert(ui.select_pattern(0));
    audio.process_commands(commands);
    ui.poll_notifications();
    assert(ui.note_number(0, 0) == 60);
    assert(audio.current_pattern().tracks_[0].steps_[0].note_ == 60);
    // Edits targeting another stored pattern must not affect current.
    audio.apply_command({.type_ = bork::sequencer_command::type::clear_track, .pattern_id_ = 1});
    assert(audio.current_pattern().tracks_[0].steps_[0].note_ == 60);

    // Edits before/after activation stay ordered when revisiting the same IDs.
    assert(ui.togglecell()); // Pattern 1 off, before switching away.
    assert(ui.select_pattern(1));
    assert(ui.togglecell()); // Pattern 2 back on, after its activation command.
    assert(audio.process_commands(commands) == 3);
    assert(audio.current_pattern().tracks_[0].steps_[0].note_ == 72);
    ui.poll_notifications();
    assert(ui.select_pattern(0)); // Latest stored pattern 1 is empty.
    assert(ui.note_number(0, 0) == 0);
    assert(ui.togglecell());
    assert(audio.process_commands(commands) == 2);
    assert(audio.current_pattern().tracks_[0].steps_[0].note_ == 60);
    ui.poll_notifications();

    // Submission failure leaves displayed pattern/storage unchanged and spare reusable.
    bork::command_queue full(1);
    bork::sequencer other{};
    bork::GridWidget blocked(storage, full, other.patterns_);
    assert(full.try_push({.type_ = bork::sequencer_command::type::toggle_play}));
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
    assert(audio.pattern_id_ == ui.selected_pattern_);
    assert(audio.current_pattern().tracks_[0].steps_[0].note_ == ui.note_number(0, 0));
}
