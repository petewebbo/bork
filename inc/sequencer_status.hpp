#pragma once

#include <atomic>

namespace bork {

// Independent runtime telemetry, not a publication fence for other engine data.
// Audio publishes while running; the lifecycle owner clears after callbacks stop.
struct sequencer_status {
    static_assert(std::atomic<bool>::is_always_lock_free);
    std::atomic<bool> playing{false};
};

} // namespace bork
