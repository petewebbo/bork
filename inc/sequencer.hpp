
#pragma once

#include "bork_defines.hpp"
#include "pattern.hpp"
#include "pattern_exchange.hpp"
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
    pattern_exchange _patterns;
    uint8_t _current_slot{};
    uint8_t _pattern_id{};
    uint64_t _generation{};
    pattern& current_pattern() { return _patterns.slots[_current_slot]; }
    const pattern& current_pattern() const { return _patterns.slots[_current_slot]; }
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
        if ((command._type == sequencer_command::type::set_step ||
             command._type == sequencer_command::type::clear_track) &&
            (command._generation != _generation || command._pattern_id != _pattern_id)) return;
        auto& pattern = current_pattern();
        switch (command._type) {
        case sequencer_command::type::set_step:
            if (command._track < pattern._tracks.size() && command._step < MAX_PATTERN_STEPS)
                pattern._tracks[command._track]._steps[command._step] = command._value;
            break;
        case sequencer_command::type::clear_track:
            if (command._track < pattern._tracks.size())
                for (auto& step : pattern._tracks[command._track]._steps) step._note = 0;
            break;
        case sequencer_command::type::set_tempo:
            if (std::isfinite(command._tempo) && command._tempo >= 20 && command._tempo <= 300)
                _transport.set_tempo(command._tempo);
            break;
        case sequencer_command::type::toggle_play:
            _is_playing = !_is_playing;
            break;
        case sequencer_command::type::activate_pattern: {
            if (command._slot >= _patterns.slots.size() || command._slot == _current_slot ||
                command._pattern_id >= NUM_STORED_PATTERNS || command._generation <= _generation) break;
            const pattern_switched ack{command._generation, command._pattern_id, _current_slot};
            // Only this thread produces acknowledgements; the UI can only
            // free space. A valid single outstanding request always has room.
            if (_patterns.completed.size() == _patterns.completed.capacity()) break;
            _current_slot = command._slot;
            _pattern_id = command._pattern_id;
            _generation = command._generation;
            const bool published = _patterns.completed.try_push(ack);
            (void)published; // Guaranteed by the ownership/producer contract.
            break;
        }
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
                auto& step = current_pattern()._tracks[track_idx]._steps[next_step];
                if (step._note) {
                    _samplers[track_idx].trigger(step);
                }
            }
        }
    }
};

} // namespace bork
