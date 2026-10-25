#pragma once

#include "model/pattern.hpp"
#include "messages/sequencer_command.hpp"
#include <rigtorp/SPSCQueue.h>
#include <cassert>
#include <cstdint>
#include <optional>

namespace bork {
inline constexpr size_t PLAYBACK_BUFFER_COUNT = 5;
inline constexpr size_t SNAPSHOT_QUEUE_CAPACITY = 3;
struct snapshot_ref {
    uint8_t buffer_id{};
    uint8_t pattern_id{};
    const pattern* value{};
};
struct snapshot_message {
    snapshot_ref snapshot{};
    bool select{};
    bool deferred{};
};
struct pattern_exchange {
    command_queue events{COMMAND_QUEUE_CAPACITY};
    rigtorp::SPSCQueue<snapshot_message> snapshots{SNAPSHOT_QUEUE_CAPACITY};
    rigtorp::SPSCQueue<uint8_t> released{PLAYBACK_BUFFER_COUNT};
};

// Audio/preview owns only these two borrowed references, never pattern storage.
struct snapshot_consumer {
    pattern_exchange& exchange_;
    snapshot_ref current_;
    std::optional<snapshot_ref> pending_;

    snapshot_consumer(pattern_exchange& exchange, snapshot_ref initial) noexcept
        : exchange_(exchange), current_(initial) { assert(initial.value); }
    snapshot_consumer(const snapshot_consumer&) = delete;
    snapshot_consumer& operator=(const snapshot_consumer&) = delete;
    ~snapshot_consumer() { release_all(); }

    void release(snapshot_ref reference) noexcept {
        assert(reference.value);
        const bool returned = exchange_.released.try_push(reference.buffer_id);
        assert(returned); // Five entries cover every allocation, each returned exactly once.
        (void)returned;
    }
    void apply_snapshot(const snapshot_message& message) noexcept {
        const auto incoming = message.snapshot;
        if (message.select) {
            if (pending_) { release(*pending_); pending_.reset(); }
            if (!message.deferred || incoming.pattern_id == current_.pattern_id) {
                release(current_);
                current_ = incoming;
            } else pending_ = incoming;
        } else if (incoming.pattern_id == current_.pattern_id) {
            release(current_);
            current_ = incoming;
        } else if (pending_ && incoming.pattern_id == pending_->pattern_id) {
            release(*pending_);
            pending_ = incoming;
        } else release(incoming);
    }
    size_t process_snapshots() noexcept {
        const size_t count = exchange_.snapshots.size();
        for (size_t i = 0; i < count; ++i) {
            apply_snapshot(*exchange_.snapshots.front());
            exchange_.snapshots.pop();
        }
        return count;
    }
    void switch_pending() noexcept {
        if (!pending_) return;
        release(current_);
        current_ = *pending_;
        pending_.reset();
    }
    // Only after callbacks stop; includes references queued for a stopped consumer.
    void release_all() noexcept {
        if (current_.value) { release(current_); current_.value = nullptr; }
        if (pending_) { release(*pending_); pending_.reset(); }
        const size_t count = exchange_.snapshots.size();
        for (size_t i = 0; i < count; ++i) {
            release(exchange_.snapshots.front()->snapshot);
            exchange_.snapshots.pop();
        }
    }
};
} // namespace bork
