
#pragma once

#include "bork_defines.hpp"
#include "pattern.hpp"
#include "pattern_exchange.hpp"
#include "sampler.hpp"
#include "sequencer_command.hpp"
#include "sequencer_status.hpp"
#include <cmath>
#include "transport.hpp"
#include <array>
#include <cstdint>
#include <span>

namespace bork {

struct sequencer {
    bool is_playing_ = false;
    transport transport_;
    pattern_exchange patterns_;
    uint8_t current_slot_{};
    uint8_t pattern_id_{};
    pattern& current_pattern() { return patterns_.slots[current_slot_]; }
    const pattern& current_pattern() const { return patterns_.slots[current_slot_]; }
    std::array<std::unique_ptr<machine>, MAX_NUM_TRACKS> machines_;

    sequencer() {
        for (auto& m : machines_) {
            m = std::make_unique<sampler>();
        }
    }

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
        if ((command.type_ == sequencer_command::type::set_step ||
             command.type_ == sequencer_command::type::clear_track) &&
            command.pattern_id_ != pattern_id_) return;
        auto& pattern = current_pattern();
        switch (command.type_) {
        case sequencer_command::type::set_step:
            if (command.track_ < pattern.tracks_.size() && command.step_ < MAX_PATTERN_STEPS)
                pattern.tracks_[command.track_].steps_[command.step_] = command.value_;
            break;
        case sequencer_command::type::clear_track:
            if (command.track_ < pattern.tracks_.size())
                for (auto& step : pattern.tracks_[command.track_].steps_) step.note_ = 0;
            break;
        case sequencer_command::type::set_tempo:
            if (std::isfinite(command.tempo_) && command.tempo_ >= 20 && command.tempo_ <= 300)
                transport_.set_tempo(command.tempo_);
            break;
        case sequencer_command::type::toggle_play:
            is_playing_ = !is_playing_;
            break;
        case sequencer_command::type::update_pattern: {
            const auto* edit = patterns_.edits.front();
            assert(edit); // Every marker must have one previously published payload.
            if (!edit) break;
            if (edit->pattern_id == pattern_id_)
                pattern = edit->value;
            // Payloads for other patterns must also be popped to preserve alignment.
            patterns_.edits.pop();
            break;
        }
        case sequencer_command::type::activate_pattern: {
            if (command.slot_ >= patterns_.slots.size() || command.slot_ == current_slot_ ||
                command.pattern_id_ >= NUM_STORED_PATTERNS) break;
            const pattern_switched ack{command.pattern_id_, current_slot_};
            // Only this thread produces acknowledgements; the UI can only
            // free space. A valid single outstanding request always has room.
            if (patterns_.completed.size() == patterns_.completed.capacity()) break;
            current_slot_ = command.slot_;
            pattern_id_ = command.pattern_id_;
            const bool published = patterns_.completed.try_push(ack);
            (void)published; // Guaranteed by the ownership/producer contract.
            break;
        }
        }
    }

    // Call once after rendering the entire device block, not each scratch chunk.
    void publish_status(sequencer_status& status, size_t processed_tracks) const noexcept {
        for (size_t track = 0; track < MAX_NUM_TRACKS; ++track) {
            const auto step = track < processed_tracks
                ? transport_.track_transport_[track].last_step_played_ : NO_CURRENT_STEP;
            status.publish_position(track, pattern_id_, step);
        }
        status.playing.store(is_playing_, std::memory_order_relaxed);
    }

    void process_track(uint8_t track_idx, std::span<float> track_buffer) {
        if (!is_playing_) {
            return;
        }
        auto num_samples = track_buffer.size();
        transport_.track_transport_[track_idx].block_update(num_samples);

        uint32_t last_sample{};
        uint32_t samples_processed{};

        while (last_sample < num_samples) {
            auto next_step = transport_.track_transport_[track_idx].next_step_;
            auto next_event_sample =
                transport_.track_transport_[track_idx].get_next_step_sample(num_samples);
            last_sample = next_event_sample;

            if (next_event_sample == STEP_OUT_OF_BLOCK) {
                last_sample = num_samples;
            }

            // now actually process samples up to next_event_sample
            auto samples_to_process = last_sample - samples_processed;

            if (samples_to_process > 0) {
                machines_[track_idx]->process_chunk(
                    track_buffer.subspan(samples_processed, samples_to_process));
                samples_processed += samples_to_process;
            }

            // if we are at an event and not the end of the block
            // check the pattern step and trigger an event if required
            if (samples_processed == next_event_sample) {
                auto& step = current_pattern().tracks_[track_idx].steps_[next_step];
                if (step.note_) {
                    machines_[track_idx]->trigger(step);
                }
            }
        }
    }
};

} // namespace bork
