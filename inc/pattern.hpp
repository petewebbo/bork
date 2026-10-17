#pragma once

#include <array>
#include <bork_defines.hpp>

namespace bork {


struct step {
    int note_{};
    int vel_{};
    int len_{};
};

struct track {
    std::array<step, MAX_PATTERN_STEPS> steps_;
    unsigned int size_{};
};

struct pattern {
    std::array<track, 8> tracks_;
};

}
