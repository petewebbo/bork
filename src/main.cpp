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
    using base::base;
    ~audio_client() override { base::deactivate(); }

private:
    int process(unsigned long num_frames) override {
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
            for (auto* output : base::_out_buffer_ptrs)
                std::copy_n(main_buffer.data(), count, output + offset);
            offset += count;
        }
        return paContinue;
    }
};


int main() try {
    fmt::print("welcome to {}!\n", "bork");
    fmt::print("max trax = {}\n", sequencer._transport._track_transport.size());

    // make a simple pattern
    for (int i=0; i<16; i=i+16) {
        sequencer._pattern._tracks[0]._steps[i+0] = {._note = 1, ._vel=127};
        sequencer._pattern._tracks[0]._steps[i+3] = {._note = 1, ._vel=127};
        sequencer._pattern._tracks[0]._steps[i+6] = {._note = 1, ._vel=127};
        sequencer._pattern._tracks[0]._steps[i+8] = {._note = 1, ._vel=127};
    }
    for (int i=4; i<16; i=i+8) {
        sequencer._pattern._tracks[1]._steps[i+0] = {._note = 1, ._vel=90};
    }
    for (int i=0; i<16; i=i+2) {
        sequencer._pattern._tracks[3]._steps[i+0] = {._note = 1, ._vel=80};
        sequencer._pattern._tracks[3]._steps[i+1] = {._note = 1, ._vel=40};
    }
    sequencer._pattern._tracks[4]._steps[14] = {._note = 1, ._vel=40};
    
    sequencer._samplers[0]._audio_file.load("/Users/petewebbo/code/bork/samples/tapetonic/Tonic_BD_E_C2S.wav");
    sequencer._samplers[1]._audio_file.load("/Users/petewebbo/code/bork/samples/tapetonic/Tonic_SD_D_C2A.wav");
    sequencer._samplers[2]._audio_file.load("/Users/petewebbo/code/bork/samples/tapetonic/Tonic_Clap_D_ST_T1A.wav");
    sequencer._samplers[3]._audio_file.load("/Users/petewebbo/code/bork/samples/tapetonic/Tonic_HH_B_T1A_V1.wav");
    sequencer._samplers[4]._audio_file.load("/Users/petewebbo/code/bork/samples/tapetonic/Tonic_HHo_B_T1A.wav");


    audio_client<float> audio("bork");
    audio.init(0, 2); // Default output device; no microphone/input required.
    sequencer._transport._sample_rate = static_cast<uint32_t>(audio.sample_rate());
    sequencer._transport.set_tempo(120.0f);
    audio.activate();

    // Temporary direct access: UI writes are intentionally not synchronised
    // with the audio callback yet.
    bork::run_grid_tui(sequencer._pattern);
    audio.deactivate();
    
    return 0;
} catch (const std::exception& error) {
    fmt::print(stderr, "Audio error: {}\n", error.what());
    return 1;
}
