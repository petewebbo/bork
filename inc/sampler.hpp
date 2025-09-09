#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace bork {

template <typename T> using sample = std::vector<T>;

template <typename T> struct sampler {

    sampler() {}

    void trigger() {
        _play_counter = 0;
        _playing = true;
    }

    void process_chunk(std::span<T> buffer) {
        auto samples_to_end = _sample_data.size() - _play_counter;
        auto samples_to_play = std::min(samples_to_end, buffer.size());
        std::copy(_sample_data.data() + _play_counter,
                  _sample_data.data() + _play_counter + samples_to_play,
                  buffer.data());
    }

    sample<T> _sample_data{1};
    uint32_t _play_counter{};
    bool _playing{};
};

} // namespace bork
