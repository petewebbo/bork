#include "store/pattern_store.hpp"
#include "audio/sequencer.hpp"
#undef NDEBUG
#include <cassert>

namespace {
struct fixture {
    bork::pattern_store store;
    bork::sequencer audio{store.exchange_, store.initial_snapshot()};
    uint64_t request_id{};
    void submit(bork::store_request request) {
        request.request_id = ++request_id;
        assert(store.try_submit(request));
        store.pump();
        bork::store_result result;
        assert(store.try_result(result) && result.accepted);
        bork::editing_snapshot snapshot;
        while (store.try_snapshot(snapshot)) {}
    }
    void select(uint8_t id, bool deferred = true) {
        submit({.type = bork::store_request::kind::select, .pattern_id = id, .deferred = deferred});
    }
    void edit(uint8_t id, int note) {
        bork::store_request request{.type = bork::store_request::kind::edit, .pattern_id = id};
        request.edit.value.tracks_[0].steps_[0].note_ = note;
        submit(request);
    }
    void drain() {
        audio.process_commands();
        audio.publish_status(store.audio_status_, 1);
        store.pump();
        accounting();
    }
    void boundary() {
        auto& transport = audio.transport_.track_transport_[0];
        transport.next_step_ = 0;
        transport.block_start_sample_ = 0;
        std::array<float, 1> buffer{};
        audio.process_track(0, buffer);
        audio.publish_status(store.audio_status_, 1);
        store.pump();
        accounting();
    }
    void accounting() {
        const size_t borrowed = (audio.current_.value ? 1 : 0) + (audio.pending_ ? 1 : 0);
        assert(store.free_buffers() + store.exchange_.snapshots.size() +
            store.exchange_.released.size() + borrowed == bork::PLAYBACK_BUFFER_COUNT);
    }
};
}

void test_pattern_switching() {
    {
        fixture f;
        f.accounting();
        f.edit(0, 67);
        const auto* queued_a = f.store.exchange_.snapshots.front()->snapshot.value;
        f.select(1);
        assert(queued_a->tracks_[0].steps_[0].note_ == 67);
        f.drain(); // A-edit/B-select: update A, then install B as pending.
        assert(f.audio.pattern_id() == 0 && f.audio.current_pattern().tracks_[0].steps_[0].note_ == 67);
        assert(f.audio.pending_ && f.audio.pending_->pattern_id == 1);
        f.edit(1, 72);
        f.drain();
        assert(f.audio.pending_->value->tracks_[0].steps_[0].note_ == 72);
        f.boundary(); // Stopped transport keeps the pending selection.
        assert(f.audio.pattern_id() == 0 && f.audio.pending_);
        f.audio.apply_command({bork::sequencer_command::type::start});
        f.boundary();
        assert(f.audio.pattern_id() == 1 && !f.audio.pending_);
        assert(f.audio.current_pattern().tracks_[0].steps_[0].note_ == 72);
        assert(f.store.ui_status_.current_pattern.load() == 1);
        assert(f.store.ui_status_.pending_pattern.load() == -1);
        f.edit(1, 73); f.drain(); // A late B update refreshes current without waiting another cycle.
        assert(f.audio.current_pattern().tracks_[0].steps_[0].note_ == 73);
    }
    {
        fixture f;
        f.select(1); f.drain();
        f.select(2); f.drain(); // Pending replacement returns B's buffer.
        assert(f.audio.pending_->pattern_id == 2);
        f.select(0); f.drain(); // Selecting actual current clears pending and refreshes A.
        assert(f.audio.pattern_id() == 0 && !f.audio.pending_);
        assert(f.store.free_buffers() == 4);
        f.select(1); f.drain();
        f.select(2, false); f.drain(); // Immediate selection clears old pending and current.
        assert(f.audio.pattern_id() == 2 && !f.audio.pending_);
        assert(f.store.free_buffers() == 4);
        f.edit(2, 76); f.drain();
        assert(f.audio.current_pattern().tracks_[0].steps_[0].note_ == 76);
        f.select(2); f.drain(); // Same-current deferred selection is still an immediate refresh.
        assert(f.audio.pattern_id() == 2 && !f.audio.pending_);
    }
    {
        fixture f;
        f.select(1); // In-flight target has an edit snapshot queued behind it.
        f.edit(1, 74);
        f.select(2, false);
        assert(f.store.exchange_.snapshots.size() == 3);
        f.drain();
        assert(f.audio.pattern_id() == 2 && !f.audio.pending_);
        assert(f.store.original(1).tracks_[0].steps_[0].note_ == 74);
        // Arrange an update that is made irrelevant by a queued immediate selection.
        f.edit(2, 78);
        f.select(0, false);
        f.edit(2, 79);
        assert(f.store.exchange_.snapshots.size() == 3);
        f.drain();
        assert(f.audio.pattern_id() == 0);
        assert(f.store.original(2).tracks_[0].steps_[0].note_ == 79);
        assert(f.store.free_buffers() == 4); // Unmatched update returned, never installed.
    }
    {
        fixture f;
        f.select(1); f.drain(); // Current + pending occupy two buffers.
        f.edit(0, 61); f.edit(0, 62); f.edit(0, 63); // All remaining buffers queued.
        assert(f.store.free_buffers() == 0 && f.store.exchange_.snapshots.size() == 3);
        const auto* frozen = f.store.exchange_.snapshots.front()->snapshot.value;
        assert(frozen->tracks_[0].steps_[0].note_ == 61);
        f.edit(0, 64); // Originals remain editable without a sixth buffer.
        f.select(2);
        f.edit(2, 82);
        f.select(3); // Latest unsent selection supersedes 2, never the original edits.
        f.edit(3, 83);
        assert(f.store.original(0).tracks_[0].steps_[0].note_ == 64);
        assert(f.store.original(2).tracks_[0].steps_[0].note_ == 82);
        assert(frozen->tracks_[0].steps_[0].note_ == 61); // No reuse before release.
        f.accounting();
        f.audio.process_commands(); // Release three old A snapshots without store reclaim yet.
        assert(f.store.free_buffers() == 0 && f.store.exchange_.released.size() == 3);
        f.accounting();
        f.store.pump();
        assert(f.store.exchange_.snapshots.front()->select);
        assert(f.store.exchange_.snapshots.front()->snapshot.pattern_id == 3); // Selection priority.
        assert(f.store.exchange_.snapshots.front()->snapshot.value->tracks_[0].steps_[0].note_ == 83);
        f.drain();
        assert(f.audio.pending_->pattern_id == 3);
        assert(f.audio.current_pattern().tracks_[0].steps_[0].note_ == 64);
        f.audio.release_all(); // Stopped-consumer reclamation includes current, pending and queued.
        f.store.pump();
        assert(f.store.free_buffers() == 5);
    }
}
