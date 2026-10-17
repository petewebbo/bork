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

    // Playhead wraps correctly with ordinary blocks smaller than a step.
    bork::sequencer wrapping;
    wrapping.is_playing_ = true;
    wrapping.transport_.sample_rate_ = 192;
    wrapping.transport_.set_tempo(60);
    wrapping.transport_.set_steps_per_cycle(0, 4);
    for (int block = 0; block < 6; ++block) {
        wrapping.process_track(0, std::span(buffer).first(32));
        wrapping.publish_status(status, 1);
        assert(status.current_step(0, 0, true) == static_cast<uint16_t>(((block + 1) * 32 - 1) / 48));
    } // Ends exactly at wrap; step 0 belongs to the following block.
    wrapping.process_track(0, std::span(buffer).first(1));
    wrapping.publish_status(status, 1);
    assert(status.current_step(0, 0, true) == 0);
}
