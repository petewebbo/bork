#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include <fmt/format.h>

#include <AudioFile.h>

#include <pattern.hpp>

namespace bork {

template <typename T> using buffer = std::vector<std::vector<T>>;

template <typename T> struct sampler {

    sampler() :
        _samples(_audio_file.samples) {
        _audio_file.setNumChannels(1);
        _audio_file.setNumSamplesPerChannel(1);
        _samples[0][0] = 1;
    }

    void trigger(step& trig) {
        if (trig._note) {
            _play_counter = 0;
            _playing = true;
            _amp = static_cast<float>(trig._vel)/127;;
        }
    }

    void process_chunk(std::span<T> buffer) {

        if (!_playing) {
            return;
        }
        
        auto samples_to_end = _samples[0].size() - _play_counter;
        auto samples_to_play = std::min(samples_to_end, buffer.size());

        for (size_t i=0; i<samples_to_play; ++i) {
            buffer[i] = _samples[0][_play_counter++] * _amp;
        }
        
        if (_play_counter >= _samples[0].size()) {
            _playing = false;
            _play_counter = 0;
        }
    }

    AudioFile<T> _audio_file;
    buffer<T>& _samples;
    uint32_t _play_counter{};
    bool _playing{};
    float _amp{};
};

} // namespace bork
