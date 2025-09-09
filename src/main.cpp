#include "transport.hpp"
#include <iostream>
#include <array>
#include <fmt/base.h>
#include <ezpz_jack.hpp>
#include <sequencer.hpp>
#
bork::sequencer sequencer;
std::array<float, 8192> track_buffer{};
std::array<float, 8192> main_buffer{};

template<typename SampleType>
class jack_client : public ezpz_jack<SampleType> {

    using ezpz_jack<SampleType>::ezpz_jack;
    using base = ezpz_jack<SampleType>;
    
    int process(jack_nframes_t num_frames) override {

        sequencer.process_track(0, std::span(track_buffer.data(), num_frames));
        
        for (auto& out_ch : base::_out_buffer_ptrs){
            // std::fill(out_ch, out_ch+num_frames, 0.0f);
            std::copy(track_buffer.data(), track_buffer.data()+num_frames, out_ch);
        }

        return 0;
    }  
};

// using namespace bork;

int main() {
    fmt::print("welcome to {}!\n", "bork");
    fmt::print("max trax = {}\n", sequencer._transport._track_transport.size());

    // make a simple pattern
    sequencer._pattern._tracks[0]._steps[0].note = 1;
    sequencer._pattern._tracks[0]._steps[4].note = 1;
    sequencer._pattern._tracks[0]._steps[8].note = 1;
    sequencer._pattern._tracks[0]._steps[12].note = 1;

    // for (size_t i=0;i<16;++i) {
    //     sequencer._transport._track_transport[0].block_update(5000);
    //     auto step = sequencer._transport._track_transport[0]._next_step;
    //     auto sample = sequencer._transport._track_transport[0].get_next_step_sample();
    //     fmt::print("end sample {}\n", sequencer._transport._track_transport[0]._block_end_sample);
    //     fmt::print("step {} to sample {}\n", step, sample);
    // }

    jack_client<float> jc("bork");
    jc.init(2, 2);
    jc.activate();

    std::cout << "Press Enter to continue...";
    std::cin.get();
    
    return 0;
}
