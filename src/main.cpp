#include "transport.hpp"
#include <algorithm>
#include <grid_tui.hpp>
#include <array>
#include <fmt/base.h>
#include <ezpz_portaudio.hpp>
#include <sequencer.hpp>

bork::sequencer sequencer;
std::array<std::array<float, 8192>, 16> track_buffers{};
std::array<float, 8192> main_buffer{};
const int TRACKS_TO_PROCESS = bork::GridWidget::ROWS;

template<typename SampleType>
class audio_client : public ezpz_portaudio<SampleType> {
    using base = ezpz_portaudio<SampleType>;

public:
    audio_client(std::string_view name, bork::command_queue& commands,
                 bork::sequencer_status& status)
        : base(name), commands_(commands), status_(status) {}
    ~audio_client() override { deactivate(); }
    void deactivate() noexcept {
        base::deactivate(); // Wait for callbacks before clearing published state.
        status_.playing.store(false, std::memory_order_relaxed);
    }

private:
    bork::command_queue& commands_;
    bork::sequencer_status& status_;
    int process(unsigned long num_frames) override {
        sequencer.process_commands(commands_);
        // PortAudio may supply variable-sized blocks. Never exceed scratch capacity.
        for (size_t offset = 0; offset < num_frames;) {
            const auto count = std::min<size_t>(main_buffer.size(), num_frames - offset);
            std::fill_n(main_buffer.data(), count, 0.0f);
            for (size_t track = 0; track < TRACKS_TO_PROCESS; ++track) {
                std::fill_n(track_buffers[track].data(), count, 0.0f);
                sequencer.process_track(track, std::span(track_buffers[track].data(), count));
                for (size_t i = 0; i < count; ++i)
                    main_buffer[i] += track_buffers[track][i];
            }
            for (auto* output : base::out_buffer_ptrs_)
                std::copy_n(main_buffer.data(), count, output + offset);
            offset += count;
        }
        sequencer.publish_status(status_, TRACKS_TO_PROCESS);
        return paContinue;
    }
};


int main() try {
    fmt::print("welcome to {}!\n", "bork");
    fmt::print("max trax = {}\n", sequencer.transport_.track_transport_.size());

    // // make a simple pattern
    // for (int i=0; i<16; i=i+16) {
    //     sequencer.pattern_.tracks_[0].steps_[i+0] = {.note_ = 1, .vel_=127};
    //     sequencer.pattern_.tracks_[0].steps_[i+3] = {.note_ = 1, .vel_=127};
    //     sequencer.pattern_.tracks_[0].steps_[i+6] = {.note_ = 1, .vel_=127};
    //     sequencer.pattern_.tracks_[0].steps_[i+8] = {.note_ = 1, .vel_=127};
    // }
    // for (int i=4; i<16; i=i+8) {
    //     sequencer.pattern_.tracks_[1].steps_[i+0] = {.note_ = 1, .vel_=90};
    // }
    // for (int i=0; i<16; i=i+2) {
    //     sequencer.pattern_.tracks_[3].steps_[i+0] = {.note_ = 1, .vel_=80};
    //     sequencer.pattern_.tracks_[3].steps_[i+1] = {.note_ = 1, .vel_=40};
    // }
    // sequencer.pattern_.tracks_[4].steps_[14] = {.note_ = 1, .vel_=40};
    
    static_cast<bork::sampler*>(sequencer.machines_[0].get())->audio_file_.load("/Users/petewebbo/code/bork/samples/tapetonic/Tonic_BD_E_C2S.wav");
    static_cast<bork::sampler*>(sequencer.machines_[1].get())->audio_file_.load("/Users/petewebbo/code/bork/samples/tapetonic/Tonic_SD_D_C2A.wav");
    static_cast<bork::sampler*>(sequencer.machines_[2].get())->audio_file_.load("/Users/petewebbo/code/bork/samples/tapetonic/Tonic_Clap_D_ST_T1A.wav");
    static_cast<bork::sampler*>(sequencer.machines_[3].get())->audio_file_.load("/Users/petewebbo/code/bork/samples/tapetonic/Tonic_HH_B_T1A_V1.wav");
    static_cast<bork::sampler*>(sequencer.machines_[4].get())->audio_file_.load("/Users/petewebbo/code/bork/samples/tapetonic/Tonic_HHo_B_T1A.wav");


    // Initialise the UI's display cache before audio takes exclusive ownership.
    // Storage, exchange and queues all outlive callbacks, including on exceptions.
    bork::pattern_storage storage = sequencer.patterns_;
    bork::command_queue commands(bork::COMMAND_QUEUE_CAPACITY);
    bork::GridWidget widget(storage, commands, sequencer.exchange_);
    bork::sequencer_status status;
    audio_client<float> audio("bork", commands, status);
    audio.init(0, 2); // Default output device; no microphone/input required.
    sequencer.transport_.sample_rate_ = static_cast<uint32_t>(audio.sample_rate());
    sequencer.transport_.set_tempo(120.0f);
    audio.activate();

    bork::run_grid_tui(widget, status);
    audio.deactivate();
    
    return 0;
} catch (const std::exception& error) {
    fmt::print(stderr, "Audio error: {}\n", error.what());
    return 1;
}
