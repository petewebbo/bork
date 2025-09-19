
#pragma once

#include "bork_defines.hpp"
#include "pattern.hpp"
#include "sampler.hpp"
#include "transport.hpp"
#include <array>
#include <cstdint>
#include <span>

namespace bork {

struct sequencer {

    transport _transport;
    pattern _pattern;
    std::array<sampler<float>, MAX_NUM_TRACKS> _samplers;

    void process_track(uint8_t track_idx, std::span<float> track_buffer) {
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
