#pragma once

#include <fmt/format.h>
#include <jack/jack.h>

#include <string>
#include <string_view>
#include <vector>

template <typename SampleType>
class ezpz_jack {
public:
    using buffer_ptrs = std::vector<SampleType*>;

    ezpz_jack(std::string_view name);
    virtual ~ezpz_jack();

    void init(size_t num_in_chans, size_t num_out_chans);
    void activate();
    void deactivate();
    // int GetNumOfPorts(const char* clientName, JackPortFlags portType);

    void reset();

private:
    std::string _client_name;
    std::vector<jack_port_t*> _out_ports;
    std::vector<jack_port_t*> _in_ports;
    size_t _num_in_chans;
    size_t _num_out_chans;
    jack_client_t* _client;
    bool _initialised;

    static int jack_callback(jack_nframes_t num_frames, void* arg);
    std::string create_port_name(std::string client_name, bool is_output, int port_number);

protected:
    unsigned int _sample_rate;
    buffer_ptrs _in_buffer_ptrs;
    buffer_ptrs _out_buffer_ptrs;

    virtual int process(jack_nframes_t num_frames) = 0;
};

template <typename SampleType>
ezpz_jack<SampleType>::ezpz_jack(std::string_view name)
    : _client_name { name }
    , _initialised { false }
{
    // create the jack client
    jack_options_t options = JackNullOption;
    jack_status_t status;
    _client = jack_client_open(_client_name.data(), options, &status);

    if (!_client) {
        throw std::runtime_error(fmt::format("Could not create jack client '{}', reason:{}", _client_name, (int)status));
    }
    if (status & JackServerStarted) {
        fmt::print(stderr, "jack_client_open started jack server, is there a bug?");
    }
    if (status & JackNameNotUnique) {
        _client_name = jack_get_client_name(_client);
        fmt::print(stderr, "Name not unique.");
    }

    jack_set_process_callback(_client, ezpz_jack::jack_callback, this);
}

template <typename SampleType>
void ezpz_jack<SampleType>::init(size_t num_in_chans, size_t num_out_chans)
{
    _num_in_chans = num_in_chans;
    _num_out_chans = num_out_chans;

    // Setup buffers for processing JACK inputs with RNBO
    _in_buffer_ptrs.resize(_num_in_chans);
    _out_buffer_ptrs.resize(_num_out_chans);

    for (size_t i = 0; i < _num_in_chans; ++i) {
        _in_ports.push_back(jack_port_register(_client, fmt::format("in_{}", i + 1).c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0));
    }

    for (size_t i = 0; i < _num_out_chans; ++i) {
        _out_ports.push_back(jack_port_register(_client, fmt::format("out_{}", i + 1).c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0));
    }

    _sample_rate = jack_get_sample_rate(_client);
    _initialised = true;
}



template <typename SampleType>
ezpz_jack<SampleType>::~ezpz_jack()
{
    reset();
}

template <typename SampleType>
void ezpz_jack<SampleType>::reset()
{

    deactivate();
    jack_set_process_callback(_client, nullptr, nullptr);

    for (auto port : _in_ports) {
        jack_port_unregister(_client, port);
    }

    for (auto port : _out_ports) {
        jack_port_unregister(_client, port);
    }

    jack_client_close(_client);
}

template <typename SampleType>
void ezpz_jack<SampleType>::activate()
{
    fmt::print("Activating {} JACK client\n", _client_name);

    if (!_initialised) {
        throw std::runtime_error("Attempt to activate Jack Client before initialisation.");
    }
    auto res = jack_activate(_client);

    if (res) {
        throw std::runtime_error(fmt::format("Activating client '{}' failed with {}", _client_name, res));
    }
}

template <typename SampleType>
void ezpz_jack<SampleType>::deactivate()
{
    jack_deactivate(_client);
}

template <typename SampleType>
int ezpz_jack<SampleType>::jack_callback(jack_nframes_t num_frames, void* arg)
{

    ezpz_jack* self = static_cast<ezpz_jack*>(arg);

    for (size_t ch = 0; ch < self->_num_in_chans; ++ch) {
        self->_in_buffer_ptrs[ch] = static_cast<SampleType*>(jack_port_get_buffer(self->_in_ports[ch], num_frames));
    }

    for (size_t ch = 0; ch < self->_num_out_chans; ++ch) {
        self->_out_buffer_ptrs[ch] = static_cast<SampleType*>(jack_port_get_buffer(self->_out_ports[ch], num_frames));
    }

    // call virtual callback function
    self->process(num_frames);

    return 0;
}
