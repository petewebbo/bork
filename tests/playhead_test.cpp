#include <sequencer.hpp>
#undef NDEBUG
#include <cassert>

int main() {
    bork::sequencer audio;
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
    render(32); // Empty steps still advance, with independent track timings.
    assert(status.current_step(0, 0, true) == 1);
    assert(status.current_step(1, 0, true) == 0);
    render(160); // Several boundaries in a block: report the last one reached.
    assert(status.current_step(0, 0, true) == 4);
    assert(status.current_step(1, 0, true) == 2);
    render(8); // No boundary: retain current step.
    assert(status.current_step(0, 0, true) == 4);
    audio.is_playing_ = false;
    render(32);
    assert(!status.playing.load());
    assert(status.current_step(0, 0, false) == bork::NO_CURRENT_STEP);
    audio.is_playing_ = true;
    render(32);
    assert(status.current_step(0, 0, true) == 5); // Resume, not restart.
    audio.apply_command({.type_ = bork::sequencer_command::type::activate_pattern,
                         .pattern_id_ = 1, .slot_ = 1});
    audio.publish_status(status, 2);
    assert(status.current_step(0, 0, true) == bork::NO_CURRENT_STEP);
    assert(status.current_step(0, 1, true) == 5);
    assert(status.current_step(2, 1, true) == bork::NO_CURRENT_STEP);

    // Short cycle: a block reaching every step must terminate, not revisit step 0.
    bork::sequencer wrapping;
    wrapping.is_playing_ = true;
    wrapping.transport_.sample_rate_ = 192;
    wrapping.transport_.set_tempo(60);
    wrapping.transport_.set_steps_per_cycle(0, 4);
    wrapping.process_track(0, buffer);
    wrapping.publish_status(status, 1);
    assert(status.current_step(0, 0, true) == 3);
    wrapping.process_track(0, std::span(buffer).first(32)); // Ends exactly at wrap.
    wrapping.publish_status(status, 1);
    assert(status.current_step(0, 0, true) == 3);
    wrapping.process_track(0, std::span(buffer).first(1));
    wrapping.publish_status(status, 1);
    assert(status.current_step(0, 0, true) == 0);
    std::array<float, 400> long_block{};
    wrapping.process_track(0, long_block); // More than two cycles in one block.
    wrapping.publish_status(status, 1);
    assert(status.current_step(0, 0, true) == 0);
}
