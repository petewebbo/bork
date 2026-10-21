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

    explicit ezpz_portaudio(std::string_view name) : client_name_(name) {
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

    void init(size_t num_in_chans, size_t num_out_chans, bool use_jack = false) {
        reset();
        const PaHostApiInfo* host = nullptr;
        if (use_jack) {
            const auto index = Pa_HostApiTypeIdToHostApiIndex(paJACK);
            if (index < 0 || !(host = Pa_GetHostApiInfo(index)))
                throw std::runtime_error("JACK is unavailable; check that the JACK server is running");
        }
        PaStreamParameters input{}, output{};
        double sample_rate = 0;
        if (num_out_chans) {
            output.device = host ? host->defaultOutputDevice : Pa_GetDefaultOutputDevice();
            const auto* info = Pa_GetDeviceInfo(output.device);
            if (!info) throw std::runtime_error(use_jack
                ? "No JACK output device available; check the JACK server and its ports"
                : "No default audio output device available");
            // Use mono if the default device cannot provide stereo.
            num_out_chans = std::min(num_out_chans, static_cast<size_t>(info->maxOutputChannels));
            if (!num_out_chans) throw std::runtime_error("Default device has no output channels");
            output.channelCount = static_cast<int>(num_out_chans);
            output.sampleFormat = paFloat32 | paNonInterleaved;
            output.suggestedLatency = info->defaultLowOutputLatency;
            sample_rate = info->defaultSampleRate;
        }
        if (num_in_chans) {
            input.device = host ? host->defaultInputDevice : Pa_GetDefaultInputDevice();
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

        in_buffer_ptrs_.resize(num_in_chans);
        out_buffer_ptrs_.resize(num_out_chans);
        check(Pa_OpenStream(&stream_, num_in_chans ? &input : nullptr,
                            num_out_chans ? &output : nullptr, sample_rate,
                            paFramesPerBufferUnspecified, paNoFlag,
                            &ezpz_portaudio::audio_callback, this), "Opening audio stream");
        sample_rate_ = Pa_GetStreamInfo(stream_)->sampleRate;
    }
    void activate() {
        if (!stream_) throw std::runtime_error("Audio stream has not been initialised");
        check(Pa_StartStream(stream_), "Starting audio stream");
    }
    void deactivate() noexcept {
        if (stream_ && Pa_IsStreamActive(stream_) == 1) Pa_StopStream(stream_);
    }
    void reset() noexcept {
        if (stream_) {
            deactivate();
            Pa_CloseStream(stream_);
            stream_ = nullptr;
        }
    }
    double sample_rate() const noexcept { return sample_rate_; }

protected:
    double sample_rate_{};
    std::vector<const SampleType*> in_buffer_ptrs_;
    buffer_ptrs out_buffer_ptrs_;
    // Return paContinue, paComplete or paAbort. Do not throw or allocate here.
    virtual int process(unsigned long num_frames) = 0;

private:
    std::string client_name_;
    PaStream* stream_{};
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
        for (size_t ch = 0; ch < self.in_buffer_ptrs_.size(); ++ch)
            self.in_buffer_ptrs_[ch] = inputs ? inputs[ch] : nullptr;
        for (size_t ch = 0; ch < self.out_buffer_ptrs_.size(); ++ch) {
            self.out_buffer_ptrs_[ch] = outputs[ch];
            std::fill_n(outputs[ch], num_frames, SampleType{});
        }
        try {
            return self.process(num_frames);
        } catch (...) {
            for (auto* channel : self.out_buffer_ptrs_)
                std::fill_n(channel, num_frames, SampleType{});
            return paAbort;
        }
    }
};
