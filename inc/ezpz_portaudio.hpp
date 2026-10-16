#pragma once

#include <portaudio.h>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// Non-interleaved buffers keep the processing interface independent of the backend.
// Derived classes must stop the stream before destroying callback-owned state.
template <typename SampleType>
class ezpz_portaudio {
public:
    using buffer_ptrs = std::vector<SampleType*>;
    static_assert(std::is_same_v<SampleType, float>, "PortAudio wrapper uses float32 samples");

    explicit ezpz_portaudio(std::string_view name) : _client_name(name) {
        check(Pa_Initialize(), "Initialising PortAudio");
    }
    virtual ~ezpz_portaudio() {
        reset();
        Pa_Terminate();
    }
    ezpz_portaudio(const ezpz_portaudio&) = delete;
    ezpz_portaudio& operator=(const ezpz_portaudio&) = delete;
    ezpz_portaudio(ezpz_portaudio&&) = delete;
    ezpz_portaudio& operator=(ezpz_portaudio&&) = delete;

    void init(size_t num_in_chans, size_t num_out_chans) {
        reset();
        PaStreamParameters input{}, output{};
        double sample_rate = 0;
        if (num_out_chans) {
            output.device = Pa_GetDefaultOutputDevice();
            const auto* info = Pa_GetDeviceInfo(output.device);
            if (!info) throw std::runtime_error("No default audio output device available");
            // Use mono if the default device cannot provide stereo.
            num_out_chans = std::min(num_out_chans, static_cast<size_t>(info->maxOutputChannels));
            if (!num_out_chans) throw std::runtime_error("Default device has no output channels");
            output.channelCount = static_cast<int>(num_out_chans);
            output.sampleFormat = paFloat32 | paNonInterleaved;
            output.suggestedLatency = info->defaultLowOutputLatency;
            sample_rate = info->defaultSampleRate;
        }
        if (num_in_chans) {
            input.device = Pa_GetDefaultInputDevice();
            const auto* info = Pa_GetDeviceInfo(input.device);
            if (!info || num_in_chans > static_cast<size_t>(info->maxInputChannels))
                throw std::runtime_error("Default audio input device cannot provide requested channels");
            input.channelCount = static_cast<int>(num_in_chans);
            input.sampleFormat = paFloat32 | paNonInterleaved;
            input.suggestedLatency = info->defaultLowInputLatency;
            if (!sample_rate) sample_rate = info->defaultSampleRate;
        }
        if (!num_in_chans && !num_out_chans)
            throw std::runtime_error("At least one audio channel is required");

        _in_buffer_ptrs.resize(num_in_chans);
        _out_buffer_ptrs.resize(num_out_chans);
        check(Pa_OpenStream(&_stream, num_in_chans ? &input : nullptr,
                            num_out_chans ? &output : nullptr, sample_rate,
                            paFramesPerBufferUnspecified, paNoFlag,
                            &ezpz_portaudio::audio_callback, this), "Opening audio stream");
        _sample_rate = Pa_GetStreamInfo(_stream)->sampleRate;
    }
    void activate() {
        if (!_stream) throw std::runtime_error("Audio stream has not been initialised");
        check(Pa_StartStream(_stream), "Starting audio stream");
    }
    void deactivate() noexcept {
        if (_stream && Pa_IsStreamActive(_stream) == 1) Pa_StopStream(_stream);
    }
    void reset() noexcept {
        if (_stream) {
            deactivate();
            Pa_CloseStream(_stream);
            _stream = nullptr;
        }
    }
    double sample_rate() const noexcept { return _sample_rate; }

protected:
    double _sample_rate{};
    std::vector<const SampleType*> _in_buffer_ptrs;
    buffer_ptrs _out_buffer_ptrs;
    // Return paContinue, paComplete or paAbort. Do not throw or allocate here.
    virtual int process(unsigned long num_frames) = 0;

private:
    std::string _client_name;
    PaStream* _stream{};
    static void check(PaError error, const char* operation) {
        if (error != paNoError)
            throw std::runtime_error(std::string(operation) + ": " + Pa_GetErrorText(error));
    }
    static int audio_callback(const void* input, void* output, unsigned long num_frames,
                              const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags,
                              void* arg) noexcept {
        auto& self = *static_cast<ezpz_portaudio*>(arg);
        auto inputs = static_cast<const SampleType* const*>(input);
        auto outputs = static_cast<SampleType**>(output);
        for (size_t ch = 0; ch < self._in_buffer_ptrs.size(); ++ch)
            self._in_buffer_ptrs[ch] = inputs ? inputs[ch] : nullptr;
        for (size_t ch = 0; ch < self._out_buffer_ptrs.size(); ++ch) {
            self._out_buffer_ptrs[ch] = outputs[ch];
            std::fill_n(outputs[ch], num_frames, SampleType{});
        }
        try {
            return self.process(num_frames);
        } catch (...) {
            for (auto* channel : self._out_buffer_ptrs)
                std::fill_n(channel, num_frames, SampleType{});
            return paAbort;
        }
    }
};
