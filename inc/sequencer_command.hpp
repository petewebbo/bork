#pragma once

#include <rigtorp/SPSCQueue.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace bork {
struct sequencer_command {
    enum class type : uint8_t { start, stop, set_tempo };
    type type_{};
    float tempo_{};
};
inline bool valid_tempo(float tempo) noexcept {
    return std::isfinite(tempo) && tempo >= 20 && tempo <= 300;
}
static_assert(std::is_trivially_copyable_v<sequencer_command>);
static_assert(std::atomic<size_t>::is_always_lock_free);
inline constexpr size_t COMMAND_QUEUE_CAPACITY = 64;
using command_queue = rigtorp::SPSCQueue<sequencer_command>;
} // namespace bork
