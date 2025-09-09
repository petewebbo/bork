#pragma once

#include "bork_defines.hpp"
#include <array>
#include <cstdint>

/**
this should deal with counting the samples and woeking out what step we're on
and where to play it
*/

namespace bork {

#define STEP_OUT_OF_BLOCK ((1 << 16) - 1)
/*
    step len equal to the number of ticks in a step.
*/
struct track_transport {
    float _samples_per_tick{};
    float _samples_per_step{};
    float _samples_per_cycle{}; // @todo i feel like this needs ot be float and
                                // should be adjusted each cycle to acount for
                                // no cycle ever being an exact number of
                                // samples in length
    float _steps_per_sample{};
    uint16_t _steps_per_cycle{};
    uint16_t _ticks_per_step{};

    uint32_t _cycle_start_sample;
    uint32_t _block_start_sample{};
    uint32_t _block_end_sample{};
    uint16_t _last_step_played{};
    uint16_t _next_step{};

    track_transport(float fs = 48000.0f, float tempo = 120.0f) {
        _steps_per_cycle = 16;
        _ticks_per_step = TICKS_PER_QN / 4;
        set_tempo(fs, tempo);
        update_internal_values();
    }

    track_transport(uint32_t steps_per_cycle, uint32_t ticks_per_step,
                    float fs = 48000.0f, float tempo = 120.0f) {
        _steps_per_cycle = steps_per_cycle;
        _ticks_per_step = ticks_per_step;
        set_tempo(fs, tempo);
        update_internal_values();
    }

    void update_internal_values() {
        _samples_per_step = _samples_per_tick * _ticks_per_step;
        _samples_per_cycle = _samples_per_step * _steps_per_cycle;
        _steps_per_sample = 1 / _samples_per_step;
    }

    void set_tempo(float sample_rate, float beats_per_minute) {
        auto seconds_per_tick = 60.0f / (beats_per_minute * TICKS_PER_QN);
        _samples_per_tick = seconds_per_tick * sample_rate;
        update_internal_values();
    }

    void set_ticks_per_step(float ticks_per_step) {
        _ticks_per_step = ticks_per_step;
        update_internal_values();
    }

    void set_steps_per_cycle(uint16_t steps_per_cycle) {
        _steps_per_cycle = steps_per_cycle;
        update_internal_values();
    }

    void block_update(uint32_t num_samp) {
        _block_start_sample = _block_end_sample;
        _block_end_sample = _block_start_sample + num_samp;
        _block_end_sample =
            (static_cast<float>(_block_end_sample) > _samples_per_cycle)
                ? _block_end_sample - _samples_per_cycle
                : _block_end_sample;
        // deal with float extra bit?
    }

    void step_update() {
        _next_step++;
        if (_next_step >= _steps_per_cycle) {
            _next_step -= _steps_per_cycle;
        }
    }

    uint32_t get_next_step_sample(bool update_step = true) {
        auto sample = step_to_sample_in_block(_next_step);
        if (update_step && sample != STEP_OUT_OF_BLOCK) {
            step_update();
        }
        return sample;
    }

    // this will need to take the actual step so that it can account for
    // step delay and such
    uint32_t step_to_sample_in_block(uint16_t step_idx) {
        auto sample = static_cast<uint32_t>(step_idx * _samples_per_step) -
                      _block_start_sample;

        if (sample < (_block_end_sample-_block_start_sample)) {
            return sample;
        } else {
            return STEP_OUT_OF_BLOCK;
        }
    }

    uint16_t sample_in_block_to_step(uint32_t sample) {
        return static_cast<uint16_t>((_block_start_sample + sample) *
                                     _steps_per_sample);
    }
};

struct transport {
    uint64_t _sample_counter{};
    uint32_t _sample_rate{};
    float _tempo;
    std::array<track_transport, MAX_NUM_TRACKS> _track_transport;

    void set_tempo(float tempo) {
        for (auto &t : _track_transport) {
            t.set_tempo(_sample_rate, tempo);
        }
    }

    void set_ticks_per_step(uint8_t track_idx, uint16_t ticks_per_step) {
        _track_transport[track_idx].set_ticks_per_step(ticks_per_step);
    }

    void set_steps_per_cycle(uint8_t track_idx, uint16_t steps_per_cycle) {
        _track_transport[track_idx].set_steps_per_cycle(steps_per_cycle);
    }

    // plopcs
};

} // namespace bork
