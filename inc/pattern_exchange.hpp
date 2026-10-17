#pragma once

#include "pattern.hpp"
#include <rigtorp/SPSCQueue.h>
#include <array>
#include <cstdint>

namespace bork {
inline constexpr size_t NUM_STORED_PATTERNS = 8;
using pattern_storage = std::array<pattern, NUM_STORED_PATTERNS>;
inline constexpr size_t NUM_EDIT_SLOTS = 4;

struct pattern_switched {
    uint64_t generation{};
    uint8_t pattern_id{};
    uint8_t retired_slot{};
};

// Slot 0 starts audio-owned, slot 1 UI-owned. Submission publishes ownership;
// acknowledgement returns the retired slot. Neither thread touches the other's slot.
struct pattern_exchange {
    std::array<pattern, 2> slots{};
    rigtorp::SPSCQueue<pattern_switched> completed{1};

    // Independent UI->audio transfer pool for atomic pattern edits. UI owns a
    // free slot, publishes it once, then waits for its completion before reuse.
    std::array<pattern, NUM_EDIT_SLOTS> edits{};
    rigtorp::SPSCQueue<uint8_t> edits_completed{NUM_EDIT_SLOTS};
};
} // namespace bork
