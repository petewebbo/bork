#include "ui/editor_state.hpp"
#include "ui/run_tui.hpp"
#include "store/pattern_store.hpp"
#include <chrono>
#include <thread>

// Non-rendering consumer exercises the same snapshot ownership as audio.
int main() {
    bork::pattern_store store;
    bork::snapshot_consumer audio(store.exchange_, store.initial_snapshot());
    bork::EditorState editor(store);
    store.start();
    std::atomic<bool> stop{false};
    std::thread consumer([&] {
            bool playing = false;
            while (!stop.load(std::memory_order_relaxed)) {
                const auto count = store.exchange_.events.size();
                for (size_t i = 0; i < count; ++i) {
                    const auto& event = *store.exchange_.events.front();
                    if (event.type_ == bork::sequencer_command::type::start) playing = true;
                    if (event.type_ == bork::sequencer_command::type::stop) playing = false;
                    store.exchange_.events.pop();
                }
                audio.process_snapshots();
                // Preview has no musical clock: pending selections activate when playing.
                if (playing) audio.switch_pending();
                store.audio_status_.publish_selection(audio.current_.pattern_id,
                    audio.pending_ ? std::optional<uint8_t>(audio.pending_->pattern_id) : std::nullopt);
                store.audio_status_.playing.store(playing, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
    });
    try {
        bork::run_tui(editor, store.ui_status_);
    } catch (...) {
        store.stop_admission();
        stop.store(true, std::memory_order_relaxed);
        consumer.join();
        throw;
    }
    store.stop_admission();
    stop.store(true, std::memory_order_relaxed);
    consumer.join();
    audio.release_all();
    store.stop();
}
