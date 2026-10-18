#pragma once

#include "pattern.hpp"
#include "pattern_edit.hpp"
#include <rigtorp/SPSCQueue.h>
#include <cstdint>

namespace bork {
enum class pattern_switch_result {
    switched,
    cancelled  
};
struct pattern_switch {
    uint8_t pattern_id{};
    uint64_t request_id{};
};

struct pattern_switch_ack {
    uint8_t pattern_id{};
    pattern_switch_result result{};
    uint64_t request_id{};
};

// Cross-thread messages only. The pattern bank belongs exclusively to audio.
struct pattern_exchange {
    rigtorp::SPSCQueue<pattern_switch_ack> completed{1};

    // Payloads are consumed only by matching update_pattern command markers.
    // Queue pop handles storage reuse; there is no edit completion channel.
    pattern_edit_queue edits{PATTERN_EDIT_QUEUE_CAPACITY};
};
} // namespace bork
