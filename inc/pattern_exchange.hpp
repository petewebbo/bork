#pragma once

#include "pattern.hpp"
#include "pattern_edit.hpp"
#include <rigtorp/SPSCQueue.h>
#include <array>
#include <cstdint>

namespace bork {
inline constexpr size_t NUM_STORED_PATTERNS = 8;
using pattern_storage = std::array<pattern, NUM_STORED_PATTERNS>;

enum class pattern_switch_result {
    switched,
    cancelled  
};

struct pattern_switched {
    uint8_t pattern_id{};
    uint8_t retired_slot{};
    pattern_switch_result result{};
};

// Slot 0 starts audio-owned, slot 1 UI-owned. Submission publishes ownership;
// acknowledgement returns the retired slot. Neither thread touches the other's slot.
struct pattern_exchange {
    std::array<pattern, 2> slots{};
    rigtorp::SPSCQueue<pattern_switched> completed{1};

    // Payloads are consumed only by matching update_pattern command markers.
    // Queue pop handles storage reuse; there is no edit completion channel.
    pattern_edit_queue edits{PATTERN_EDIT_QUEUE_CAPACITY};
};
} // namespace bork
