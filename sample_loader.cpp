#pragma once

#include <string_view>
#include <vector>
#include <AudioFile.h>

struct audio_loader{
    bool load_file(std::string_view file_name, AudioFile<float> audio_file) {
        return audio_file.load(file_name);
    }
};
