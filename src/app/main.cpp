#include <algorithm>
#include <array>
#include "ui/grid_tui.hpp"
#include <fmt/base.h>
#include "audio/ezpz_portaudio.hpp"
#include "audio/sequencer.hpp"
#include "store/pattern_store.hpp"
#include <string_view>
#ifdef BORK_USE_JACK
#include <pa_jack.h>
#endif

namespace {
constexpr size_t TRACKS_TO_PROCESS = bork::GridWidget::ROWS;

class audio_client : public ezpz_portaudio<float> {
public:
    audio_client(bork::sequencer& sequencer, bork::sequencer_status& status)
        : ezpz_portaudio("bork"), sequencer_(sequencer), status_(status) {}
    ~audio_client() override { deactivate(); }
    void deactivate() noexcept {
        ezpz_portaudio::deactivate();
        status_.playing.store(false, std::memory_order_relaxed);
    }

private:
    bork::sequencer& sequencer_;
    bork::sequencer_status& status_;
    std::array<std::array<float, 8192>, TRACKS_TO_PROCESS> track_buffers_{};
    std::array<float, 8192> main_buffer_{};

    int process(unsigned long num_frames) override {
        const auto chunk_frames = bork::sequencer::max_chunk_frames(sample_rate(), main_buffer_.size());
        for (size_t offset = 0; offset < num_frames;) {
            sequencer_.process_commands();
            const auto count = std::min<size_t>(chunk_frames, num_frames - offset);
            std::fill_n(main_buffer_.data(), count, 0.0f);
            for (size_t track = 0; track < TRACKS_TO_PROCESS; ++track) {
                std::fill_n(track_buffers_[track].data(), count, 0.0f);
                sequencer_.process_track(track, std::span(track_buffers_[track].data(), count));
                for (size_t i = 0; i < count; ++i)
                    main_buffer_[i] += track_buffers_[track][i];
            }
            for (auto* output : out_buffer_ptrs_)
                std::copy_n(main_buffer_.data(), count, output + offset);
            sequencer_.publish_status(status_, TRACKS_TO_PROCESS);
            offset += count;
        }
        return paContinue;
    }
};
} // namespace

int main(int argc, char** argv) try {
    bool use_jack = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--jack") use_jack = true;
        else if (argument == "--help") {
            fmt::print("Usage: bork [--jack]\n");
            return 0;
        } else {
            fmt::print(stderr, "Unknown argument: {}\nUsage: bork [--jack]\n", argument);
            return 2;
        }
    }
#ifdef BORK_USE_JACK
    if (use_jack) {
        const auto error = PaJack_SetClientName("bork"); // Must precede Pa_Initialize.
        if (error != paNoError)
            throw std::runtime_error(std::string("Naming JACK client: ") + Pa_GetErrorText(error));
    }
#else
    if (use_jack) {
        fmt::print(stderr, "JACK support is not enabled. Rebuild with ./build.sh USE_JACK\n");
        return 1;
    }
#endif
    fmt::print("welcome to {}!\n", "bork");
    // Reverse destruction order stops callbacks before store-owned storage dies.
    bork::pattern_store store;
    bork::sequencer sequencer(store.exchange_, store.initial_snapshot());
    static_cast<bork::sampler*>(sequencer.machines_[0].get())->audio_file_.load("/Users/petewebbo/code/bork/samples/tapetonic/Tonic_BD_E_C2S.wav");
    static_cast<bork::sampler*>(sequencer.machines_[1].get())->audio_file_.load("/Users/petewebbo/code/bork/samples/tapetonic/Tonic_SD_D_C2A.wav");
    static_cast<bork::sampler*>(sequencer.machines_[2].get())->audio_file_.load("/Users/petewebbo/code/bork/samples/tapetonic/Tonic_Clap_D_ST_T1A.wav");
    static_cast<bork::sampler*>(sequencer.machines_[3].get())->audio_file_.load("/Users/petewebbo/code/bork/samples/tapetonic/Tonic_HH_B_T1A_V1.wav");
    static_cast<bork::sampler*>(sequencer.machines_[4].get())->audio_file_.load("/Users/petewebbo/code/bork/samples/tapetonic/Tonic_HHo_B_T1A.wav");

    bork::GridWidget widget(store);
    audio_client audio(sequencer, store.audio_status_);
    audio.init(0, 2, use_jack);
    sequencer.transport_.sample_rate_ = static_cast<uint32_t>(audio.sample_rate());
    sequencer.transport_.set_tempo(120.0f);
    store.start();
    audio.activate();
    bork::run_grid_tui(widget, store.ui_status_);
    store.stop_admission();
    audio.deactivate();
    sequencer.release_all();
    store.stop();
    return 0;
} catch (const std::exception& error) {
    fmt::print(stderr, "Audio error: {}\n", error.what());
    return 1;
}
