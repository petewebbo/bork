#include "store/pattern_store.hpp"
#undef NDEBUG
#include <cassert>
#include <limits>
#include <thread>

namespace {
bork::store_request edit_request(uint8_t id, int note, uint64_t request_id = 1) {
    bork::store_request request{.type = bork::store_request::kind::edit,
        .request_id = request_id, .session = 10, .pattern_id = id};
    request.edit.value.tracks_[0].steps_[0] = {.note_ = note, .vel_ = 90, .len_ = 12};
    return request;
}
void consume_results(bork::pattern_store& store, size_t count, bool accepted = true) {
    bork::store_result result;
    for (size_t i = 0; i < count; ++i) {
        assert(store.try_result(result));
        assert(result.accepted == accepted);
    }
    assert(!store.try_result(result));
}
}

void test_edit_batches() {
    {
        bork::pattern_store store;
        bork::snapshot_consumer audio(store.exchange_, store.initial_snapshot());
        for (int i = 0; i < 64; ++i) assert(store.try_submit(edit_request(0, 60 + i, i)));
        assert(!store.try_submit(edit_request(0, 1)));
        store.pump();
        assert(store.original(0).tracks_[0].steps_[0].note_ == 123);
        assert(store.exchange_.snapshots.size() == 1); // Queued batch coalesces without a timer.
        assert(audio.current_.value->tracks_[0].steps_[0].note_ == 0);
        assert(!store.try_submit(edit_request(0, 1))); // Results retain credits until consumed.
        consume_results(store, 64);
        assert(store.outstanding_requests() == 0);
        assert(audio.process_snapshots() == 1);
        assert(audio.current_.value->tracks_[0].steps_[0].note_ == 123);
        store.pump();
        assert(store.free_buffers() == 4);

        assert(store.try_submit(edit_request(7, 72)));
        store.pump();
        consume_results(store, 1);
        assert(store.original(7).tracks_[0].steps_[0].note_ == 72);
        assert(store.exchange_.snapshots.empty()); // Do not publish the entire bank.
    }
    {
        bork::pattern_store store;
        auto request = edit_request(0, 69);
        assert(store.try_submit(request));
        request.edit.value.tracks_[0].steps_[0].note_ = 0;
        assert(store.try_submit(request));
        store.pump();
        consume_results(store, 2);
        assert(store.notes(0)[0][0] == 69); // Clear retains remembered pitch.
        request.edit.type = bork::pattern_edit::kind::paste_region;
        assert(store.try_submit(request));
        store.pump();
        consume_results(store, 1);
        assert(store.notes(0)[0][0] == 60); // Empty paste replaces metadata only here.
        assert(store.notes(0)[1][0] == 60);
        request.edit.type = bork::pattern_edit::kind::paste_pattern;
        request.edit.value.tracks_[7].steps_[15].note_ = 81;
        request.edit.value.tracks_[7].size_ = 13;
        assert(store.try_submit(request));
        store.pump();
        consume_results(store, 1);
        assert(store.original(0).tracks_[7].size_ == 13);
        assert(store.notes(0)[7][15] == 81);
        request.edit.value.tracks_[7].size_ = 17;
        assert(store.try_submit(request));
        store.pump();
        consume_results(store, 1, false);
        assert(store.original(0).tracks_[7].size_ == 13);
        request.edit.type = bork::pattern_edit::kind::region;
        request.edit.rows = std::numeric_limits<size_t>::max();
        assert(store.try_submit(request));
        store.pump();
        consume_results(store, 1, false);
        assert(store.original(0).tracks_[7].size_ == 13); // Invalid edit is atomic.
    }
    {
        bork::pattern_store store;
        assert(store.try_submit({.type = bork::store_request::kind::select,
            .request_id = 1, .session = 1, .pattern_id = 1}));
        store.pump(); // Keep this editing snapshot queued.
        assert(store.try_submit(edit_request(1, 73, 2)));
        assert(store.try_submit({.type = bork::store_request::kind::resync,
            .request_id = 3, .session = 2, .pattern_id = 1}));
        assert(store.try_submit(edit_request(1, 75, 4)));
        store.pump();
        consume_results(store, 4);
        bork::editing_snapshot snapshot;
        assert(store.try_snapshot(snapshot) && snapshot.session == 1);
        assert(!store.try_snapshot(snapshot));
        store.pump(); // Retries without another UI request and captures exact barrier contents.
        assert(store.try_snapshot(snapshot));
        assert(snapshot.recovery && snapshot.session == 2 && snapshot.request_id == 3);
        assert(snapshot.value.tracks_[0].steps_[0].note_ == 73);
        assert(snapshot.notes[0][0] == 73);
        assert(store.original(1).tracks_[0].steps_[0].note_ == 75);
    }
    {
        bork::pattern_store store;
        for (size_t i = 0; i < 64; ++i)
            assert(store.exchange_.events.try_push({bork::sequencer_command::type::start}));
        assert(store.try_submit({.type = bork::store_request::kind::tempo, .tempo = 145}));
        assert(store.try_submit(edit_request(0, 77)));
        store.pump();
        bork::store_result result;
        assert(!store.try_result(result)); // No accepted event is lost when audio stalls.
        store.exchange_.events.pop();
        store.pump();
        consume_results(store, 2);
        assert(store.original(0).tracks_[0].steps_[0].note_ == 77);
        for (size_t i = 0; i < 63; ++i) store.exchange_.events.pop();
        assert(store.exchange_.events.front()->tempo_ == 145);
    }
    {
        bork::pattern_store store;
        store.start();
        assert(store.try_submit(edit_request(0, 68)));
        bork::store_result result;
        while (!store.try_result(result)) std::this_thread::yield();
        assert(result.accepted);
        store.stop(); // No audio consumer: shutdown must not wait for pending publication.
        assert(store.original(0).tracks_[0].steps_[0].note_ == 68);
        assert(!store.try_submit(edit_request(0, 1)));
    }
}
