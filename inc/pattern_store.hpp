#pragma once

#include "pattern_edit.hpp"
#include "pattern_exchange.hpp"
#include "sequencer_status.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace bork {
inline constexpr size_t UI_REQUEST_CAPACITY = 64;
class pattern_store {
public:
    pattern_exchange exchange_;
    sequencer_status audio_status_;
    sequencer_status ui_status_;

    explicit pattern_store(pattern_storage originals = {}) : originals_(originals) {
        for (size_t id = 0; id < originals_.size(); ++id) notes_[id] = remember_notes(originals_[id]);
        buffers_[0] = originals_[0];
        used_[0] = true;
        buffer_patterns_[0] = 0;
    }
    ~pattern_store() { stop(); }
    pattern_store(const pattern_store&) = delete;
    pattern_store& operator=(const pattern_store&) = delete;
    snapshot_ref initial_snapshot() const noexcept { return {0, 0, &buffers_[0]}; }
    const sequencer_status& ui_status() const noexcept { return ui_status_; }

    // UI is sole producer and owns result credits, including stale results.
    bool try_submit(const store_request& request) {
        std::lock_guard lock(wake_mutex_);
        if (!admitting_ || outstanding_ == UI_REQUEST_CAPACITY || !requests_.try_push(request))
            return false;
        ++outstanding_;
        wake_ = true;
        wake_condition_.notify_one();
        return true;
    }
    bool try_result(store_result& result) noexcept {
        const auto* front = results_.front();
        if (!front) return false;
        result = *front;
        results_.pop();
        assert(outstanding_ > 0);
        --outstanding_;
        return true;
    }
    bool try_snapshot(editing_snapshot& snapshot) noexcept {
        const auto* front = editing_snapshots_.front();
        if (!front) return false;
        snapshot = *front;
        editing_snapshots_.pop();
        return true;
    }
    size_t outstanding_requests() const noexcept { return outstanding_; }
    void start() {
        if (worker_.joinable()) return;
        stopping_.store(false, std::memory_order_relaxed);
        worker_ = std::thread([this] { run(); });
    }
    void stop_admission() {
        std::lock_guard lock(wake_mutex_);
        admitting_ = false;
        wake_ = true;
        wake_condition_.notify_one();
    }
    void stop() {
        stop_admission();
        {
            std::lock_guard lock(wake_mutex_);
            stopping_.store(true, std::memory_order_relaxed);
            wake_ = true;
            wake_condition_.notify_one();
        }
        if (worker_.joinable()) worker_.join();
    }

    // Deterministic store pass; tests must not call this while the worker runs.
    void pump() {
        const size_t releases = exchange_.released.size();
        for (size_t i = 0; i < releases; ++i) {
            const auto id = *exchange_.released.front();
            assert(id < used_.size() && used_[id]);
            used_[id] = false;
            exchange_.released.pop();
        }
        ui_status_.copy_from(audio_status_);
        const size_t count = requests_.size();
        for (size_t i = 0; i < count; ++i) {
            const auto& request = *requests_.front();
            if (!handle(request)) break; // Event capacity retains the request in FIFO order.
            requests_.pop();
        }
        if (editing_pending_ && editing_snapshots_.try_push(*editing_pending_)) editing_pending_.reset();
        publish_audio();
    }
    const pattern& original(size_t id) const { return originals_.at(id); }
    const remembered_notes& notes(size_t id) const { return notes_.at(id); }
    size_t free_buffers() const noexcept {
        size_t count = 0;
        for (bool used : used_) count += !used;
        return count;
    }

private:
    pattern_storage originals_{};
    std::array<remembered_notes, NUM_STORED_PATTERNS> notes_{};
    std::array<pattern, PLAYBACK_BUFFER_COUNT> buffers_{};
    std::array<bool, PLAYBACK_BUFFER_COUNT> used_{};
    std::array<uint8_t, PLAYBACK_BUFFER_COUNT> buffer_patterns_{};
    std::array<bool, NUM_STORED_PATTERNS> dirty_{};
    struct selection { uint8_t pattern_id; bool deferred; };
    std::optional<selection> selection_pending_;
    std::optional<selection> published_selection_;
    bool desired_playing_{};
    std::optional<editing_snapshot> editing_pending_;
    rigtorp::SPSCQueue<store_request> requests_{UI_REQUEST_CAPACITY};
    rigtorp::SPSCQueue<store_result> results_{UI_REQUEST_CAPACITY};
    rigtorp::SPSCQueue<editing_snapshot> editing_snapshots_{1};
    size_t outstanding_{}; // UI-thread only.
    std::mutex wake_mutex_;
    std::condition_variable wake_condition_;
    bool wake_{};
    bool admitting_{true}; // Protected by wake_mutex_.
    std::atomic<bool> stopping_{false};
    std::thread worker_;

    bool handle(const store_request& request) {
        bool accepted = true;
        switch (request.type) {
        case store_request::kind::select:
        case store_request::kind::resync:
            accepted = request.pattern_id < originals_.size();
            if (accepted) {
                editing_pending_ = editing_snapshot{request.session, request.request_id,
                    request.pattern_id, request.type == store_request::kind::resync,
                    originals_[request.pattern_id], notes_[request.pattern_id]};
                if (request.type == store_request::kind::select)
                    selection_pending_ = selection{request.pattern_id, request.deferred};
            }
            break;
        case store_request::kind::edit:
            accepted = request.pattern_id < originals_.size() && valid_pattern_edit(request.edit);
            if (accepted) {
                apply_pattern_edit(originals_[request.pattern_id], notes_[request.pattern_id], request.edit);
                dirty_[request.pattern_id] = true;
            }
            break;
        case store_request::kind::tempo:
            accepted = valid_tempo(request.tempo);
            if (!accepted) break;
            [[fallthrough]];
        case store_request::kind::start:
        case store_request::kind::stop: {
            const auto type = request.type == store_request::kind::start ? sequencer_command::type::start
                : request.type == store_request::kind::stop ? sequencer_command::type::stop
                : sequencer_command::type::set_tempo;
            if (!exchange_.events.try_push({type, request.tempo})) return false;
            if (request.type == store_request::kind::start) desired_playing_ = true;
            if (request.type == store_request::kind::stop) desired_playing_ = false;
            break;
        }
        default:
            accepted = false;
            break;
        }
        const bool pushed = results_.try_push({request.type, request.request_id, request.session,
                                               request.pattern_id, accepted});
        assert(pushed); // Every admission reserved one of the 64 terminal-result slots.
        (void)pushed;
        return true;
    }
    bool publish(uint8_t pattern_id, bool select, bool deferred) {
        if (exchange_.snapshots.size() == exchange_.snapshots.capacity()) return false;
        size_t id = 0;
        while (id < used_.size() && used_[id]) ++id;
        if (id == used_.size()) return false;
        buffers_[id] = originals_[pattern_id];
        const bool pushed = exchange_.snapshots.try_push({
            {static_cast<uint8_t>(id), pattern_id, &buffers_[id]}, select, deferred});
        if (!pushed) return false;
        used_[id] = true;
        buffer_patterns_[id] = pattern_id;
        dirty_[pattern_id] = false;
        return true;
    }
    bool relevant(size_t pattern_id) const noexcept {
        // Outstanding references may be current, pending or in flight; telemetry can lag.
        for (size_t id = 0; id < used_.size(); ++id)
            if (used_[id] && buffer_patterns_[id] == pattern_id) return true;
        return false;
    }
    void publish_audio() {
        if (selection_pending_) {
            if (!publish(selection_pending_->pattern_id, true, selection_pending_->deferred)) return;
            published_selection_ = selection_pending_;
            selection_pending_.reset();
        }
        for (size_t id = 0; id < dirty_.size(); ++id) {
            if (!dirty_[id]) continue;
            if (!relevant(id)) { dirty_[id] = false; continue; }
            if (!publish(static_cast<uint8_t>(id), false, false)) return;
        }
    }
    bool needs_refresh() const noexcept {
        if (!exchange_.events.empty() || desired_playing_ != audio_status_.playing.load(std::memory_order_relaxed))
            return true;
        if (published_selection_) {
            const int current = audio_status_.current_pattern.load(std::memory_order_relaxed);
            const int pending = audio_status_.pending_pattern.load(std::memory_order_relaxed);
            if (current != published_selection_->pattern_id && pending != published_selection_->pattern_id)
                return true;
        }
        if (!requests_.empty() || editing_pending_ || selection_pending_ ||
            audio_status_.playing.load(std::memory_order_relaxed) ||
            audio_status_.pending_pattern.load(std::memory_order_relaxed) >= 0 ||
            free_buffers() < PLAYBACK_BUFFER_COUNT - 1) return true;
        for (bool dirty : dirty_) if (dirty) return true;
        return false;
    }
    void run() {
        while (!stopping_.load(std::memory_order_relaxed)) {
            pump();
            std::unique_lock lock(wake_mutex_);
            const auto predicate = [this] { return wake_ || stopping_.load(std::memory_order_relaxed); };
            if (needs_refresh()) wake_condition_.wait_for(lock, std::chrono::milliseconds(1), predicate);
            else wake_condition_.wait(lock, predicate);
            wake_ = false;
        }
    }
};
} // namespace bork
