#include <ezpz_portaudio.hpp>
#undef NDEBUG
#include <cassert>
#include <stdexcept>

// Stub the device API so lifecycle/buffer tests need no audio hardware.
namespace {
PaStreamCallback* callback;
void* context;
int initialised{}, closed{}, terminated{};
bool active{}, device_available = true, fail_open{};
PaStreamInfo stream_info{1, 0, 0, 44100};
PaDeviceInfo device_info{};
}
extern "C" {
PaError Pa_Initialize() { ++initialised; return paNoError; }
PaError Pa_Terminate() { ++terminated; return paNoError; }
const char* Pa_GetErrorText(PaError) { return "test error"; }
PaDeviceIndex Pa_GetDefaultOutputDevice() { return device_available ? 0 : paNoDevice; }
PaDeviceIndex Pa_GetDefaultInputDevice() { return paNoDevice; }
const PaDeviceInfo* Pa_GetDeviceInfo(PaDeviceIndex index) { return index == 0 ? &device_info : nullptr; }
const PaStreamInfo* Pa_GetStreamInfo(PaStream*) { return &stream_info; }
PaError Pa_OpenStream(PaStream** stream, const PaStreamParameters* input,
                      const PaStreamParameters* output, double rate, unsigned long,
                      PaStreamFlags, PaStreamCallback* cb, void* arg) {
    assert(!input && output && output->channelCount == 2);
    assert(output->sampleFormat == (paFloat32 | paNonInterleaved));
    assert(rate == 44100);
    if (fail_open) return paDeviceUnavailable;
    *stream = reinterpret_cast<PaStream*>(1);
    callback = cb;
    context = arg;
    return paNoError;
}
PaError Pa_StartStream(PaStream*) { active = true; return paNoError; }
PaError Pa_StopStream(PaStream*) { active = false; return paNoError; }
PaError Pa_IsStreamActive(PaStream*) { return active ? 1 : 0; }
PaError Pa_CloseStream(PaStream*) { ++closed; return paNoError; }
}
struct client : ezpz_portaudio<float> {
    client() : ezpz_portaudio("test") {}
    ~client() override { deactivate(); }
    bool fail{};
    int process(unsigned long frames) override {
        if (fail) throw std::runtime_error("callback failure");
        for (auto* output : out_buffer_ptrs_) std::fill_n(output, frames, 0.25f);
        return paContinue;
    }
};
void test_portaudio() {
    device_info.maxOutputChannels = 2;
    device_info.defaultSampleRate = 44100;
    {
        client audio;
        audio.init(0, 2);
        assert(audio.sample_rate() == 44100);
        audio.activate();
        float left[17]{}, right[17]{};
        float* outputs[]{left, right};
        assert(callback(nullptr, outputs, 17, nullptr, 0, context) == paContinue);
        assert(left[16] == 0.25f && right[16] == 0.25f);
        audio.fail = true;
        assert(callback(nullptr, outputs, 17, nullptr, 0, context) == paAbort);
        assert(left[16] == 0 && right[16] == 0);
        audio.reset();
        audio.reset();
        assert(closed == 1 && !active);
        fail_open = true;
        try { audio.init(0, 2); assert(false); } catch (const std::runtime_error&) {}
        fail_open = false;
        device_available = false;
        try { audio.init(0, 2); assert(false); } catch (const std::runtime_error&) {}
    }
    assert(initialised == 1 && terminated == 1 && closed == 1);
}
