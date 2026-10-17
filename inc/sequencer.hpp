
#pragma once

#include "bork_defines.hpp"
#include "pattern.hpp"
#include "sampler.hpp"
#include "sequencer_command.hpp"
#include <cmath>
#include "transport.hpp"
#include <array>
#include <cstdint>
#include <span>

namespace bork {

struct sequencer {
    bool _is_playing = false;
    transport _transport;
    pattern _pattern;
    std::array<sampler<float>, MAX_NUM_TRACKS> _samplers;

    // Audio-thread only. No allocation, waiting, logging or unbounded draining.
    size_t process_commands(command_queue& commands) {
        // Only this thread consumes. Capture once so later arrivals wait until
        // the next callback; work is bounded by the queue's fixed capacity.
        const size_t count = commands.size();
        for (size_t i = 0; i < count; ++i) {
            apply_command(*commands.front());
            commands.pop();
        }
        return count;
    }

    void apply_command(const sequencer_command& command) {
        switch (command._type) {
        case sequencer_command::type::set_step:
            if (command._track < _pattern._tracks.size() && command._step < MAX_PATTERN_STEPS)
                _pattern._tracks[command._track]._steps[command._step] = command._value;
            break;
        case sequencer_command::type::clear_track:
            if (command._track < _pattern._tracks.size())
                for (auto& step : _pattern._tracks[command._track]._steps) step._note = 0;
            break;
        case sequencer_command::type::set_tempo:
            if (std::isfinite(command._tempo) && command._tempo >= 20 && command._tempo <= 300)
                _transport.set_tempo(command._tempo);
            break;
        case sequencer_command::type::toggle_play:
            _is_playing = !_is_playing;
            break;
        }
    }

    void process_track(uint8_t track_idx, std::span<float> track_buffer) {
        if (!_is_playing) {
            return;
        }
        auto num_samples = track_buffer.size();
        _transport._track_transport[track_idx].block_update(num_samples);

        uint32_t last_sample{};
        uint32_t samples_processed{};

        while (last_sample < num_samples) {
            auto next_step = _transport._track_transport[track_idx]._next_step;
            auto next_event_sample =
                _transport._track_transport[track_idx].get_next_step_sample(num_samples);
            last_sample = next_event_sample;

            if (next_event_sample == STEP_OUT_OF_BLOCK) {
                last_sample = num_samples;
            }

            // now actually process samples up to next_event_sample
            auto samples_to_process = last_sample - samples_processed;

            if (samples_to_process > 0) {
                _samplers[track_idx].process_chunk(
                    track_buffer.subspan(samples_processed, samples_to_process));
                samples_processed += samples_to_process;
            }

            // if we are at an event and not the end of the block
            // check the pattern step and trigger an event if required
            if (samples_processed == next_event_sample) {
                auto& step = _pattern._tracks[track_idx]._steps[next_step];
                if (step._note) {
                    _samplers[track_idx].trigger(step);
                }
            }
        }
    }
};

} // namespace bork
