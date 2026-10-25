#pragma once

#include "model/bork_defines.hpp"
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
    float samples_per_tick_{};
    float samples_per_step_{};
    float samples_per_cycle_{}; // @todo i feel like this needs ot be float and
                                // should be adjusted each cycle to acount for
                                // no cycle ever being an exact number of
                                // samples in length
    float steps_per_sample_{};
    uint16_t steps_per_cycle_{};
    uint16_t ticks_per_step_{};

    // uint32_t cycle_start_sample_;
    uint32_t block_start_sample_{};
    uint32_t block_end_sample_{};
    uint16_t last_step_played_{NO_CURRENT_STEP};
    uint16_t next_step_{};

    track_transport(float fs = 48000.0f, float tempo = 120.0f) {
        steps_per_cycle_ = 16;
        ticks_per_step_ = TICKS_PER_QN / 4;
        set_tempo(fs, tempo);
        update_internal_values();
    }

    track_transport(uint32_t steps_per_cycle, uint32_t ticks_per_step,
                    float fs = 48000.0f, float tempo = 120.0f) {
        steps_per_cycle_ = steps_per_cycle;
        ticks_per_step_ = ticks_per_step;
        set_tempo(fs, tempo);
        update_internal_values();
    }

    void update_internal_values() {
        auto old_samples_per_step = samples_per_step_;
        auto old_samples_per_cycle = samples_per_cycle_;
        samples_per_step_ = samples_per_tick_ * ticks_per_step_;
        samples_per_cycle_ = samples_per_step_ * steps_per_cycle_;
        steps_per_sample_ = 1 / samples_per_step_;
        block_start_sample_ = block_start_sample_ / old_samples_per_cycle * samples_per_cycle_;        
        block_end_sample_ = block_end_sample_ / old_samples_per_cycle * samples_per_cycle_;        
        
    }

    void set_tempo(float sample_rate, float beats_per_minute) {
        auto seconds_per_tick = 60.0f / (beats_per_minute * TICKS_PER_QN);
        samples_per_tick_ = seconds_per_tick * sample_rate;
        update_internal_values();
    }

    void set_ticks_per_step(float ticks_per_step) {
        ticks_per_step_ = ticks_per_step;
        update_internal_values();
    }

    void set_steps_per_cycle(uint16_t steps_per_cycle) {
        steps_per_cycle_ = steps_per_cycle;
        update_internal_values();
    }

    void block_update(uint32_t num_samp) {
        block_start_sample_ = block_end_sample_;
        block_end_sample_ = block_start_sample_ + num_samp;
        block_end_sample_ =
            (static_cast<float>(block_end_sample_) > samples_per_cycle_)
                ? block_end_sample_ - samples_per_cycle_
                : block_end_sample_;
        // deal with float extra bit?
    }

    void step_update() {
        next_step_++;
        if (next_step_ >= steps_per_cycle_) {
            next_step_ -= steps_per_cycle_;
        }
    }

    uint32_t get_next_step_sample(uint32_t num_samples, bool update_step = true) {
        auto sample = step_to_sample_in_block(next_step_, num_samples);
        if (update_step && sample != STEP_OUT_OF_BLOCK) {
            last_step_played_ = next_step_; // Includes steps with no note.
            step_update();
        }
        return sample;
    }

    // this will need to take the actual step so that it can account for
    // step delay and such
    uint32_t step_to_sample_in_block(uint16_t step_idx, uint32_t num_samples) {
        auto sample_in_cycle = static_cast<uint32_t>(step_idx * samples_per_step_);
        uint32_t sample_in_block;

        // deal with wraparound to start of cycle
        if (sample_in_cycle < block_start_sample_) {
            sample_in_block = samples_per_cycle_ - block_start_sample_ + sample_in_cycle;
        } else {
            sample_in_block = sample_in_cycle - block_start_sample_;
        }

        if (sample_in_block < num_samples) {
            return sample_in_block;
        } else {
            return STEP_OUT_OF_BLOCK;
        }
    }

    uint16_t sample_in_block_to_step(uint32_t sample) {
        return static_cast<uint16_t>((block_start_sample_ + sample) *
                                     steps_per_sample_);
    }
};

struct transport {
    uint64_t sample_counter_{};
    uint32_t sample_rate_{};
    float tempo_;
    std::array<track_transport, MAX_NUM_TRACKS> track_transport_;

    void set_tempo(float tempo) {
        for (auto &t : track_transport_) {
            t.set_tempo(sample_rate_, tempo);
        }
    }

    void set_ticks_per_step(uint8_t track_idx, uint16_t ticks_per_step) {
        track_transport_[track_idx].set_ticks_per_step(ticks_per_step);
    }

    void set_steps_per_cycle(uint8_t track_idx, uint16_t steps_per_cycle) {
        track_transport_[track_idx].set_steps_per_cycle(steps_per_cycle);
    }

    // plopcs
};

} // namespace bork
