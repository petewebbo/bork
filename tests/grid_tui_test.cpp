#include <grid_tui.hpp>
#include <midi_notes.hpp>
#include <sequencer.hpp>
#include <string_view>
#undef NDEBUG
#include <cassert>
#include <limits>
#include <thread>

namespace {
struct fixture {
    bork::pattern_store store;
    bork::sequencer audio;
    bork::GridWidget ui;
    explicit fixture(const bork::pattern_storage& initial = {})
        : store(initial), audio(store.exchange_, store.initial_snapshot()), ui(store) { flush(); }
    void flush() {
        for (int pass = 0; pass < 6; ++pass) {
            store.pump();
            audio.process_commands();
            audio.publish_status(store.audio_status_, bork::GridWidget::ROWS);
            store.pump();
            ui.poll_notifications();
        }
    }
};
}

void test_grid_tui() {
    {
        bork::pattern_storage values{};
        values[0].tracks_[0].steps_[0] = {.note_ = 67, .vel_ = 81, .len_ = 12};
        values[0].tracks_[0].steps_[1] = {.note_ = 0, .vel_ = 42, .len_ = 6};
        fixture display(values);
        assert(display.ui.input_mode_key('e'));
        assert(display.ui.input_mode_key('v') && !display.ui.awaiting_edit_);
        assert(display.ui.displayed_value(0, 0) == 81);
        assert(display.ui.displayed_value(0, 1) == 42);
        assert(display.ui.input_mode_key('e') && display.ui.input_mode_key('l'));
        assert(display.ui.displayed_value(0, 0) == 12);
        assert(display.ui.input_mode_key('e'));
        assert(display.ui.input_mode_key('x') && display.ui.input_mode_key('q'));
        assert(display.ui.input_mode_key(' ') && display.ui.awaiting_edit_);
        assert(display.ui.input_mode_key(27) && !display.ui.awaiting_edit_);
        assert(display.ui.displayed_value(0, 0) == 12);
        assert(display.ui.input_mode_key('e') && display.ui.input_mode_key('n'));
        assert(display.ui.displayed_value(0, 0) == 67 && display.ui.displayed_value(0, 1) == 0);
        assert(display.ui.note_number(0, 0) == 67);
        assert(display.store.outstanding_requests() == 0);
        assert(display.ui.input_mode_key('c') && display.ui.input_mode_key('e'));
        assert(display.ui.awaiting_copy_ && !display.ui.awaiting_edit_);
        assert(display.ui.input_mode_key(27));
    }
    bork::sequencer_status status;
    const auto& published = status;
    assert(!published.playing.load(std::memory_order_relaxed));
    std::thread publisher([&] { status.playing.store(true, std::memory_order_relaxed); });
    publisher.join();
    assert(published.playing.load(std::memory_order_relaxed));
    status.playing.store(false, std::memory_order_relaxed);
    assert(std::string_view(bork::lookup_midi_note(0)->name.data()) == "C-1");
    assert(std::string_view(bork::lookup_midi_note(60)->name.data()) == "C4");
    assert(std::string_view(bork::lookup_midi_note(61)->name.data()) == "C#4");
    assert(std::string_view(bork::lookup_midi_note(127)->name.data()) == "G9");
    assert(std::abs(bork::lookup_midi_note(69)->frequency - 440.0) < 0.000001);
    assert(!bork::lookup_midi_note(-1) && !bork::lookup_midi_note(128));

    bork::pattern_storage initial{};
    initial[0].tracks_[0].steps_[3] = {.note_ = 61, .vel_ = 80, .len_ = 12};
    initial[0].tracks_[4].steps_[14] = {.note_ = 74, .vel_ = 40};
    fixture f(initial);
    assert(!f.ui.loading() && f.ui.loaded_pattern_ == 0);
    assert(!f.audio.is_playing_);
    assert(f.ui.toggleplay() && f.ui.toggleplay());
    f.flush();
    assert(!f.audio.is_playing_); // Explicit start/stop preserve rapid toggle intent.
    assert(f.ui.toggleplay());
    f.flush();
    assert(f.audio.is_playing_);
    assert(f.ui.toggleplay());
    f.flush();
    assert(!f.audio.is_playing_);
    assert(f.ui.grid_state_[0][3] && f.ui.grid_state_[4][14]);
    assert(!f.ui.grid_state_[2][0]);
    f.ui.set_cursor(0, 3);
    assert(f.ui.togglecell());
    assert(f.ui.note_number(0, 3) == 0);
    assert(f.store.original(0).tracks_[0].steps_[3].note_ == 61);
    assert(f.audio.current_pattern().tracks_[0].steps_[3].note_ == 61);
    f.store.pump();
    assert(f.store.original(0).tracks_[0].steps_[3].note_ == 0);
    assert(f.audio.current_pattern().tracks_[0].steps_[3].note_ == 61);
    f.flush();
    assert(f.audio.current_pattern().tracks_[0].steps_[3].note_ == 0);
    assert(f.ui.togglecell());
    assert(f.ui.note_number(0, 3) == 61);
    f.flush();
    assert(f.audio.current_pattern().tracks_[0].steps_[3].vel_ == 80);
    assert(f.audio.current_pattern().tracks_[0].steps_[3].len_ == 12);
    assert(f.ui.clearrow() && f.ui.togglecell());
    f.flush();
    assert(f.store.original(0).tracks_[0].steps_[3].note_ == 61);
    f.ui.set_cursor(2, 0);
    assert(f.ui.togglecell() && f.ui.clearrow() && f.ui.togglecell());
    f.flush();
    assert(f.store.original(0).tracks_[2].steps_[0].note_ == 60);
    assert(f.store.original(0).tracks_[2].steps_[0].vel_ == 100);
    assert(f.ui.clearrow());
    f.flush();
    for (const auto& step : f.store.original(0).tracks_[2].steps_) assert(step.note_ == 0);
    assert(f.store.original(0).tracks_[4].steps_[14].note_ == 74);

    // Results reserve credits until consumed, not merely until store processing.
    for (size_t i = 0; i < 64; ++i) assert(f.ui.togglecell());
    const int before = f.ui.note_number(2, 0);
    assert(!f.ui.togglecell() && f.ui.queue_full_);
    assert(!f.ui.clearrow() && f.ui.note_number(2, 0) == before);
    f.store.pump();
    assert(!f.ui.togglecell()); // Request queue is drained, reliable results still occupy credits.
    f.ui.poll_notifications();
    assert(f.ui.togglecell() && !f.ui.queue_full_);
    f.flush();

    // Editing selection is independent of paused/deferred audio activation.
    f.ui.copy_pattern();
    assert(f.ui.select_pattern(1) && f.ui.loading());
    assert(f.ui.loaded_pattern_ == 0 && f.ui.selected_pattern_ == 1);
    assert(!f.ui.togglecell() && !f.ui.clearrow() && !f.ui.toggle_selection());
    assert(!f.ui.clear_selection() && !f.ui.paste());
    const auto row = f.ui.current_row_, col = f.ui.current_col_;
    assert(!f.ui.click_cell(4, 15));
    assert(f.ui.current_row_ == row && f.ui.current_col_ == col);
    f.ui.copy_pattern(); // Loading must not overwrite the deliberate clipboard.
    f.store.pump();
    bork::store_result result{};
    while (f.store.try_result(result)) {} // A terminal selection result alone is not a copy.
    assert(f.ui.loading());
    f.ui.poll_notifications();
    assert(!f.ui.loading() && f.ui.loaded_pattern_ == 1);
    assert(f.audio.pattern_id() == 0);
    assert(f.ui.paste());
    f.flush();
    assert(f.store.original(1).tracks_[4].steps_[14].note_ == 74);
    assert(f.audio.pattern_id() == 0 && f.audio.pending_ && f.audio.pending_->pattern_id == 1);

    // A stale queued copy cannot complete a newer selection, even on revisiting an ID.
    assert(f.ui.select_pattern(2));
    f.store.pump();
    assert(f.ui.select_pattern(3) && f.ui.select_pattern(2));
    f.store.pump();
    f.ui.poll_notifications();
    assert(f.ui.loading());
    f.store.pump();
    f.ui.poll_notifications();
    assert(!f.ui.loading() && f.ui.loaded_pattern_ == 2);
    f.flush();

    f.audio.transport_.sample_rate_ = 48000;
    f.audio.apply_command({.type_ = bork::sequencer_command::type::set_tempo, .tempo_ = 150});
    const auto samples_per_tick = f.audio.transport_.track_transport_[0].samples_per_tick_;
    assert(std::abs(samples_per_tick - 100.0f) < 0.001f);
    f.audio.apply_command({.type_ = bork::sequencer_command::type::set_tempo,
                           .tempo_ = std::numeric_limits<float>::quiet_NaN()});
    assert(f.audio.transport_.track_transport_[0].samples_per_tick_ == samples_per_tick);

    // Exercise real SPSC publication and repeated ring wraparound.
    bork::command_queue concurrent(8);
    std::thread producer([&] {
        for (int i = 1; i <= 10000; ++i) {
            const bork::sequencer_command command{
                .type_ = bork::sequencer_command::type::set_tempo, .tempo_ = static_cast<float>(i)};
            while (!concurrent.try_push(command)) std::this_thread::yield();
        }
    });
    for (int i = 1; i <= 10000; ++i) {
        const bork::sequencer_command* command;
        while (!(command = concurrent.front())) std::this_thread::yield();
        assert(command->tempo_ == i);
        concurrent.pop();
    }
    producer.join();
}
