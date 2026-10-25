#pragma once

#include "model/bork_defines.hpp"
#include "messages/pattern_exchange.hpp"
#include "audio/sampler.hpp"
#include "messages/sequencer_status.hpp"
#include "audio/transport.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <cstdint>
#include <span>

namespace bork {
struct sequencer : snapshot_consumer {
    bool is_playing_ = false;
    transport transport_;
    std::array<std::unique_ptr<machine>, MAX_NUM_TRACKS> machines_;

    sequencer(pattern_exchange& exchange, snapshot_ref initial)
        : snapshot_consumer(exchange, initial) {
        for (auto& machine : machines_) machine = std::make_unique<sampler>();
    }
    const pattern& current_pattern() const noexcept { return *current_.value; }
    uint8_t pattern_id() const noexcept { return current_.pattern_id; }
    static size_t max_chunk_frames(double sample_rate, size_t scratch_capacity) noexcept {
        const size_t capacity = std::max<size_t>(1, scratch_capacity);
        if (!std::isfinite(sample_rate) || sample_rate < 200) return 1;
        return static_cast<size_t>(std::min<double>(capacity, std::floor(sample_rate / 200)));
    }
    // Audio-thread only: capture each depth once, events before snapshot messages.
    size_t process_commands() noexcept {
        const size_t count = exchange_.events.size();
        for (size_t i = 0; i < count; ++i) {
            apply_command(*exchange_.events.front());
            exchange_.events.pop();
        }
        return count + process_snapshots();
    }
    void apply_command(const sequencer_command& command) noexcept {
        switch (command.type_) {
        case sequencer_command::type::start: is_playing_ = true; break;
        case sequencer_command::type::stop: is_playing_ = false; break;
        case sequencer_command::type::set_tempo:
            if (valid_tempo(command.tempo_)) transport_.set_tempo(command.tempo_);
            break;
        }
    }
    // Publish after each rendered chunk, not just at the end of a device callback.
    void publish_status(sequencer_status& status, size_t processed_tracks) const noexcept {
        for (size_t track = 0; track < MAX_NUM_TRACKS; ++track) {
            const auto step = track < processed_tracks
                ? transport_.track_transport_[track].last_step_played_ : NO_CURRENT_STEP;
            status.publish_position(track, pattern_id(), step);
        }
        status.publish_selection(pattern_id(), pending_ ? std::optional<uint8_t>(pending_->pattern_id) : std::nullopt);
        status.playing.store(is_playing_, std::memory_order_relaxed);
    }
    void process_track(uint8_t track_idx, std::span<float> track_buffer) {
        if (!is_playing_) {
            machines_[track_idx]->process_chunk(track_buffer); // Pause sequencing, not sample tails.
            return;
        }
        auto num_samples = track_buffer.size();
        transport_.track_transport_[track_idx].block_update(num_samples);
        uint32_t last_sample{};
        uint32_t samples_processed{};
        while (last_sample < num_samples) {
            auto next_step = transport_.track_transport_[track_idx].next_step_;
            // Preserve the existing track-zero switch boundary and scheduling.
            if (track_idx == 0 && next_step == 0 && pending_) switch_pending();
            auto next_event_sample =
                transport_.track_transport_[track_idx].get_next_step_sample(num_samples);
            last_sample = next_event_sample;
            if (next_event_sample == STEP_OUT_OF_BLOCK) last_sample = num_samples;
            auto samples_to_process = last_sample - samples_processed;
            if (samples_to_process > 0) {
                machines_[track_idx]->process_chunk(
                    track_buffer.subspan(samples_processed, samples_to_process));
                samples_processed += samples_to_process;
            }
            if (samples_processed == next_event_sample) {
                const auto& step = current_pattern().tracks_[track_idx].steps_[next_step];
                if (step.note_) machines_[track_idx]->trigger(step);
            }
        }
    }
};
} // namespace bork
