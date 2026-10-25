#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include <fmt/format.h>

#include <AudioFile.h>

#include "model/pattern.hpp"
#include "audio/machine.hpp"

namespace bork {

template <typename T> using buffer = std::vector<std::vector<T>>;

struct sampler : public machine {

    using T = float;

    sampler()
    : machine(machine::MachineType::Sampler)
    , samples_(audio_file_.samples) {
        audio_file_.setNumChannels(1);
        audio_file_.setNumSamplesPerChannel(1);
        samples_[0][0] = 1;
    }

    void trigger(const step& trig) noexcept final{
        if (trig.note_) {
            play_counter_ = 0;
            playing_ = true;
            amp_ = static_cast<float>(trig.vel_)/127;;
        }
    }

    void process_chunk(std::span<T> buffer) noexcept final {

        if (!playing_) {
            return;
        }
        
        auto samples_to_end = samples_[0].size() - play_counter_;
        auto samples_to_play = std::min(samples_to_end, buffer.size());

        for (size_t i=0; i<samples_to_play; ++i) {
            buffer[i] = samples_[0][play_counter_++] * amp_;
        }
        
        if (play_counter_ >= samples_[0].size()) {
            playing_ = false;
            play_counter_ = 0;
        }
    }

    AudioFile<T> audio_file_;
    buffer<T>& samples_;
    uint32_t play_counter_{};
    bool playing_{};
    float amp_{};
};

} // namespace bork
