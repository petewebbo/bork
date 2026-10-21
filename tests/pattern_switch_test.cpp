#include <grid_tui.hpp>
#include <sequencer.hpp>
#undef NDEBUG
#include <cassert>

namespace {
struct fixture {
    bork::sequencer audio;
    bork::pattern_storage cache = [] {
        bork::pattern_storage bank{};
        bank[0].tracks_[0].steps_[0] = {.note_ = 60, .vel_ = 90};
        bank[1].tracks_[0].steps_[0] = {.note_ = 72, .vel_ = 80};
        bank[2].tracks_[0].steps_[0] = {.note_ = 74, .vel_ = 80};
        bank[2].tracks_[0].steps_[1] = {.note_ = 76, .vel_ = 80};
        return bank;
    }();
    bork::command_queue commands{16};
    bork::GridWidget ui{cache, commands, audio.exchange_};
    fixture() { audio.patterns_ = cache; }
    void boundary() {
        auto& track = audio.transport_.track_transport_[0];
        track.next_step_ = 0;
        track.block_end_sample_ = static_cast<uint32_t>(track.samples_per_cycle_) - 1;
        audio.is_playing_ = true;
        std::array<float, 2> buffer{};
        audio.process_track(0, buffer);
    }
};
} // namespace

void test_pattern_switching() {
    {
        fixture f;
        assert(f.ui.select_pattern(0) && f.commands.empty());
        assert(f.ui.pattern_input('P') && f.ui.pattern_input('2'));
        assert(f.ui.selected_pattern_ == 1 && f.ui.playing_pattern_ == 0 && f.ui.switch_pending_);
        assert(f.ui.togglecell());
        f.audio.process_commands(f.commands);
        assert(f.audio.pattern_id_ == 0 && f.audio.deferred_next_pattern_->pattern_id == 1);
        assert(f.audio.patterns_[1].tracks_[0].steps_[0].note_ == 0); // Edit pending B.
        assert(f.audio.current_pattern().tracks_[0].steps_[0].note_ == 60);
        assert(f.audio.exchange_.completed.empty());

        assert(f.ui.select_pattern(2)); // C replaces B; no slot or pattern transfer.
        assert(f.ui.clearrow());
        f.audio.process_commands(f.commands);
        assert(f.audio.deferred_next_pattern_->pattern_id == 2);
        assert(f.audio.patterns_[2].tracks_[0].steps_[0].note_ == 0);
        assert(f.audio.patterns_[2].tracks_[0].steps_[1].note_ == 0);
        assert(f.audio.patterns_[1].tracks_[0].steps_[0].note_ == 0);

        f.ui.move_cursor(0, 1, true);
        assert(f.ui.toggle_selection()); // Bulk edit pending C.
        f.ui.copy_selection();
        f.ui.set_cursor(1, 0);
        assert(f.ui.paste());
        f.audio.process_commands(f.commands);
        assert(f.audio.patterns_[2].tracks_[0].steps_[0].note_ == 74);
        assert(f.audio.patterns_[2].tracks_[1].steps_[1].note_ == 76);
        f.boundary();
        assert(f.audio.pattern_id_ == 2 && !f.audio.deferred_next_pattern_);
        assert(f.ui.switch_pending_);
        f.ui.poll_notifications();
        assert(!f.ui.switch_pending_ && f.ui.playing_pattern_ == 2);
        assert(f.audio.current_pattern().tracks_[1].steps_[1].note_ == 76);

        // A valid edit to another stored pattern no longer gets discarded.
        f.audio.apply_command({.type_ = bork::sequencer_command::type::set_step,
                               .value_ = {.note_ = 67}, .pattern_id_ = 7});
        assert(f.audio.patterns_[7].tracks_[0].steps_[0].note_ == 67);
        f.audio.apply_command({.type_ = bork::sequencer_command::type::clear_track,
                               .pattern_id_ = 7});
        assert(f.audio.patterns_[7].tracks_[0].steps_[0].note_ == 0);
        f.audio.apply_command({.type_ = bork::sequencer_command::type::set_step,
                               .value_ = {.note_ = 99}, .pattern_id_ = 255});
        assert(f.audio.current_pattern().tracks_[0].steps_[0].note_ == 74);
    }
    {
        fixture f;
        assert(f.ui.select_pattern(1));
        assert(f.ui.select_pattern(2));
        assert(f.ui.select_pattern(0)); // Selecting playing A cancels the latest pending request.
        assert(f.audio.process_commands(f.commands) == 3);
        assert(!f.audio.deferred_next_pattern_ && f.audio.pattern_id_ == 0);
        f.ui.poll_notifications();
        assert(!f.ui.switch_pending_ && f.ui.playing_pattern_ == 0);
        assert(f.audio.patterns_[1].tracks_[0].steps_[0].note_ == 72);
        assert(f.audio.patterns_[2].tracks_[0].steps_[0].note_ == 74);

        assert(f.ui.select_pattern(1)); // Switching still works after cancellation.
        f.audio.process_commands(f.commands);
        f.boundary();
        f.ui.poll_notifications();
        assert(!f.ui.switch_pending_ && f.ui.playing_pattern_ == 1);
        assert(f.ui.select_pattern(2));
        assert(f.ui.clearrow());
        assert(f.ui.select_pattern(1)); // Cancel again, now with B playing.
        f.audio.process_commands(f.commands);
        f.ui.poll_notifications();
        assert(!f.ui.switch_pending_ && f.audio.pattern_id_ == 1);
        assert(f.audio.patterns_[2].tracks_[0].steps_[0].note_ == 0); // Cancel preserves edits.
    }
    {
        fixture f;
        assert(f.ui.select_pattern(1));
        f.audio.process_commands(f.commands);
        f.boundary(); // B activates, but its acknowledgement has not reached the UI.
        assert(f.ui.select_pattern(2));
        assert(f.ui.select_pattern(1)); // B -> C -> B: matching pattern IDs are not enough.
        assert(f.audio.process_commands(f.commands) == 0); // Retain commands while response queue is full.
        f.ui.poll_notifications();
        assert(f.ui.playing_pattern_ == 1 && f.ui.switch_pending_);
        assert(f.audio.process_commands(f.commands) == 2);
        assert(!f.audio.deferred_next_pattern_);
        f.ui.poll_notifications();
        assert(!f.ui.switch_pending_ && f.ui.selected_pattern_ == 1);
    }
    {
        fixture f;
        assert(f.ui.select_pattern(1));
        assert(f.ui.select_pattern(0));
        f.audio.process_commands(f.commands); // Cancellation outcome fills the response queue.
        assert(f.ui.select_pattern(2));
        assert(f.audio.process_commands(f.commands) == 0 && !f.commands.empty());
        f.ui.poll_notifications(); // Older cancellation must not clear pending C.
        assert(f.ui.switch_pending_ && f.ui.selected_pattern_ == 2);
        f.audio.process_commands(f.commands);
        f.boundary();
        f.ui.poll_notifications();
        assert(!f.ui.switch_pending_ && f.ui.playing_pattern_ == 2);
    }
    {
        bork::sequencer audio;
        bork::pattern_storage cache{};
        bork::command_queue full(1);
        bork::GridWidget ui(cache, full, audio.exchange_);
        assert(full.try_push({.type_ = bork::sequencer_command::type::toggle_play}));
        assert(!ui.select_pattern(1));
        assert(ui.selected_pattern_ == 0 && !ui.switch_pending_);
        audio.process_commands(full);
        assert(ui.select_pattern(1));
        assert(full.front()->request_id_ == 1); // Failed submission did not advance the request.
    }
}
