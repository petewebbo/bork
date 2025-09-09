#pragma once

#include <array>
#include <bork_defines.hpp>

namespace bork {


struct step {
    int note{};
    int vel{};
    int len{};  
};

struct track {
    std::array<step, MAX_PATTERN_STEPS> _steps;
    unsigned int _size{};  
};

struct pattern {
    std::array<track, 8> _tracks; 
};

}
