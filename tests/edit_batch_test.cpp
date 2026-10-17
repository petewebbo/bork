#include <sequencer.hpp>
#undef NDEBUG
#include <cassert>
#include <chrono>
#include <cstdio>
#include <type_traits>

int main() {
    static_assert(std::is_trivially_copyable_v<bork::pattern>);
    bork::sequencer audio;
    bork::command_queue commands(8);
    for (size_t i = 0; i < bork::NUM_EDIT_SLOTS; ++i) {
        auto& edit = audio._patterns.edits[i];
        edit._tracks[7]._steps[15] = {._note = static_cast<int>(60 + i), ._vel = 90, ._len = 12};
        edit._tracks[7]._size = 16;
        assert(commands.try_push({._type = bork::sequencer_command::type::replace_pattern,
                                  ._slot = static_cast<uint8_t>(i)}));
    }
    assert(audio.current_pattern()._tracks[7]._steps[15]._note == 0);
    assert(audio.process_commands(commands) == bork::NUM_EDIT_SLOTS);
    assert(audio.current_pattern()._tracks[7]._steps[15]._note == 63);
    assert(audio.current_pattern()._tracks[7]._size == 16);
    for (size_t i = 0; i < bork::NUM_EDIT_SLOTS; ++i) {
        assert(*audio._patterns.edits_completed.front() == i);
        audio._patterns.edits_completed.pop();
    }
    // Stale targets return their slot without changing current state.
    audio.apply_command({._type = bork::sequencer_command::type::replace_pattern,
                         ._generation = 999, ._slot = 0});
    assert(audio.current_pattern()._tracks[7]._steps[15]._note == 63);
    assert(*audio._patterns.edits_completed.front() == 0);
    audio._patterns.edits_completed.pop();
    audio.apply_command({._type = bork::sequencer_command::type::replace_pattern, ._slot = 255});
    assert(!audio._patterns.edits_completed.front());

    // Informational timing, not a flaky deadline assertion or real-time guarantee.
    constexpr int iterations = 10000;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        audio.apply_command({._type = bork::sequencer_command::type::replace_pattern, ._slot = 0});
        audio._patterns.edits_completed.pop();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::printf("Pattern batch: %zu bytes; copy + completion average %lld ns (local build)\n",
                sizeof(bork::pattern), static_cast<long long>(elapsed / iterations));
}
