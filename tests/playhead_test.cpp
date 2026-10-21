#include <pattern_store.hpp>
#include <sequencer.hpp>
#undef NDEBUG
#include <cassert>
#include <limits>

namespace {
struct tail_machine : bork::machine {
    size_t frames{};
    size_t triggers{};
    tail_machine() : machine(MachineType::Sampler) {}
    void trigger(const bork::step&) noexcept override { ++triggers; }
    void process_chunk(std::span<float> output) noexcept override { frames += output.size(); }
};
}
void test_playheads() {
    bork::pattern_store store;
    bork::sequencer audio(store.exchange_, store.initial_snapshot());
    bork::sequencer_status status;
    std::array<float, 160> buffer{};
    assert(status.current_step(0, 0, true) == bork::NO_CURRENT_STEP);
    audio.transport_.sample_rate_ = 192;
    audio.transport_.set_tempo(60); // 48 samples per step.
    audio.transport_.set_ticks_per_step(1, 96);
    audio.is_playing_ = true;
    auto render = [&](size_t frames) {
        for (uint8_t track = 0; track < 2; ++track)
            audio.process_track(track, std::span(buffer).first(frames));
        audio.publish_status(status, 2);
    };
    render(32);
    assert(status.current_step(0, 0, true) == 0);
    assert(status.current_step(1, 0, true) == 0);
    render(32);
    assert(status.current_step(0, 0, true) == 1);
    assert(status.current_step(1, 0, true) == 0);
    render(160);
    assert(status.current_step(0, 0, true) == 4);
    assert(status.current_step(1, 0, true) == 2);
    render(8);
    assert(status.current_step(0, 0, true) == 4);
    audio.apply_command({bork::sequencer_command::type::stop});
    render(32);
    assert(!status.playing.load());
    assert(status.current_step(0, 0, false) == bork::NO_CURRENT_STEP);
    audio.apply_command({bork::sequencer_command::type::start});
    render(32);
    assert(status.current_step(0, 0, true) == 5); // Resume, not restart.
    assert(store.try_submit({.type = bork::store_request::kind::select,
        .pattern_id = 1, .deferred = false}));
    store.pump();
    audio.process_commands();
    audio.publish_status(status, 2);
    assert(status.current_step(0, 0, true) == bork::NO_CURRENT_STEP);
    assert(status.current_step(0, 1, true) == 5);
    assert(status.current_step(2, 1, true) == bork::NO_CURRENT_STEP);
    assert(status.current_pattern.load() == 1 && status.pending_pattern.load() == -1);
    store.audio_status_.copy_from(status);
    store.pump();
    assert(store.ui_status_.current_step(0, 1, true) == 5);

    bork::pattern_store wrapping_store;
    bork::sequencer wrapping(wrapping_store.exchange_, wrapping_store.initial_snapshot());
    wrapping.is_playing_ = true;
    wrapping.transport_.sample_rate_ = 192;
    wrapping.transport_.set_tempo(60);
    wrapping.transport_.set_steps_per_cycle(0, 4);
    for (int block = 0; block < 6; ++block) {
        wrapping.process_track(0, std::span(buffer).first(32));
        wrapping.publish_status(status, 1);
        assert(status.current_step(0, 0, true) == static_cast<uint16_t>(((block + 1) * 32 - 1) / 48));
    }
    wrapping.process_track(0, std::span(buffer).first(1));
    wrapping.publish_status(status, 1);
    assert(status.current_step(0, 0, true) == 0);

    assert(bork::sequencer::max_chunk_frames(48000, 8192) == 240);
    assert(bork::sequencer::max_chunk_frames(44100, 8192) == 220);
    assert(bork::sequencer::max_chunk_frames(96000, 128) == 128);
    assert(bork::sequencer::max_chunk_frames(192, 8192) == 1);
    assert(bork::sequencer::max_chunk_frames(0, 8192) == 1);
    assert(bork::sequencer::max_chunk_frames(std::numeric_limits<double>::infinity(), 8192) == 1);

    // Simulate a large callback with the exact production chunk sizing path.
    auto machine = std::make_unique<tail_machine>();
    auto* rendered = machine.get();
    wrapping.machines_[0] = std::move(machine);
    const auto original_end = wrapping.transport_.track_transport_[0].block_end_sample_;
    size_t chunks = 0;
    const auto chunk = bork::sequencer::max_chunk_frames(48000, buffer.size());
    for (size_t offset = 0; offset < 1000;) {
        const auto frames = std::min(chunk, 1000 - offset);
        // Commands arriving after an earlier check are handled before the next chunk.
        if (chunks == 1) assert(wrapping_store.exchange_.events.try_push({bork::sequencer_command::type::stop}));
        wrapping.process_commands();
        wrapping.process_track(0, std::span(buffer).first(frames));
        wrapping.publish_status(status, 1);
        if (chunks >= 1) assert(!status.playing.load());
        offset += frames;
        ++chunks;
    }
    assert(chunks == 7 && rendered->frames == 1000); // Stop keeps rendering tails.
    assert(wrapping.transport_.track_transport_[0].block_start_sample_ == original_end);
    assert(wrapping_store.exchange_.events.try_push({bork::sequencer_command::type::start}));
    assert(wrapping_store.exchange_.events.try_push({bork::sequencer_command::type::start}));
    assert(wrapping.process_commands() == 2 && wrapping.is_playing_); // Explicit desired states, not toggles.
    assert(wrapping_store.exchange_.events.try_push({bork::sequencer_command::type::stop}));
    assert(wrapping_store.exchange_.events.try_push({bork::sequencer_command::type::stop}));
    assert(wrapping.process_commands() == 2 && !wrapping.is_playing_);
    const float old_tick = wrapping.transport_.track_transport_[0].samples_per_tick_;
    wrapping.apply_command({bork::sequencer_command::type::set_tempo, 0});
    wrapping.apply_command({bork::sequencer_command::type::set_tempo, std::numeric_limits<float>::quiet_NaN()});
    assert(wrapping.transport_.track_transport_[0].samples_per_tick_ == old_tick);
}
