#pragma once

#include "pattern.hpp"
#include <rigtorp/SPSCQueue.h>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace bork {

struct sequencer_command {
    enum class type : uint8_t { set_step, clear_track, set_tempo };
    type _type{};
    uint8_t _track{};
    uint8_t _step{};
    step _value{};
    float _tempo{};
};
static_assert(std::is_trivially_copyable_v<sequencer_command>);
static_assert(std::atomic<size_t>::is_always_lock_free);
using command_queue = rigtorp::SPSCQueue<sequencer_command>;
inline constexpr size_t COMMAND_QUEUE_CAPACITY = 1024;

} // namespace bork
