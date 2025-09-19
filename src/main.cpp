#include "transport.hpp"
#include <iostream>
#include <array>
#include <fmt/base.h>
#include <ezpz_jack.hpp>
#include <sequencer.hpp>
#
bork::sequencer sequencer;
std::array<std::array<float, 8192>, 16> track_buffers{};
std::array<float, 8192> main_buffer{};
const int TRACKS_TO_PROCESS = 3;

template<typename SampleType>
class jack_client : public ezpz_jack<SampleType> {

    using ezpz_jack<SampleType>::ezpz_jack;
    using base = ezpz_jack<SampleType>;
    
    int process(jack_nframes_t num_frames) override {

        for (size_t track=0; track<TRACKS_TO_PROCESS; ++track) {
            sequencer.process_track(track, std::span(track_buffers[track].data(), num_frames));
        }
        
        std::copy(track_buffers[0].data(), track_buffers[0].data()+num_frames, base::_out_buffer_ptrs[0]);
        
        
        for (size_t track=1; track<TRACKS_TO_PROCESS; ++track) {
            for(size_t i=0; i<num_frames; ++i) {
                base::_out_buffer_ptrs[0][i] += track_buffers[track][i];
            }
        }

        std::copy(base::_out_buffer_ptrs[0], base::_out_buffer_ptrs[0]+num_frames, base::_out_buffer_ptrs[0]);
        std::fill(track_buffers[0].data(), track_buffers[TRACKS_TO_PROCESS-1].end(), 0.0f);

        return 0;
    }  
};


int main() {
    fmt::print("welcome to {}!\n", "bork");
    fmt::print("max trax = {}\n", sequencer._transport._track_transport.size());

    // make a simple pattern
    for (int i=0; i<16; i=i+16) {
        sequencer._pattern._tracks[0]._steps[i+0] = {._note = 1, ._vel=127};
        sequencer._pattern._tracks[0]._steps[i+3] = {._note = 1, ._vel=127};
        sequencer._pattern._tracks[0]._steps[i+6] = {._note = 1, ._vel=127};
        sequencer._pattern._tracks[0]._steps[i+8] = {._note = 1, ._vel=127};
    }
    for (int i=0; i<16; i=i+2) {
        sequencer._pattern._tracks[1]._steps[i+0] = {._note = 1, ._vel=80};
        sequencer._pattern._tracks[1]._steps[i+1] = {._note = 1, ._vel=40};
    }
    for (int i=4; i<16; i=i+8) {
        sequencer._pattern._tracks[2]._steps[i+0] = {._note = 1, ._vel=90};
    }

    sequencer._samplers[0]._audio_file.load("/home/petewebbo/audio/samples/TapeTonicSamples/Samples/Tonic_BD_E_C2S.wav");
    sequencer._samplers[1]._audio_file.load("/home/petewebbo/audio/samples/TapeTonicSamples/Samples/Tonic_HH_B_T1A_V1.wav");
    sequencer._samplers[2]._audio_file.load("/home/petewebbo/audio/samples/TapeTonicSamples/Samples/Tonic_Clap_D_ST_T1A.wav");


    jack_client<float> jc("bork");
    jc.init(2, 2);
    jc.activate();

    std::cout << "Press Enter to continue...";
    std::cin.get();
    
    return 0;
}
