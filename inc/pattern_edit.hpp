#pragma once

#include "sequencer_command.hpp"
#include <cassert>

namespace bork {

struct pattern_edit {
    uint64_t generation{};
    uint8_t pattern_id{};
    pattern value{};
};
static_assert(std::is_trivially_copyable_v<pattern_edit>);
inline constexpr size_t PATTERN_EDIT_QUEUE_CAPACITY = 4;
using pattern_edit_queue = rigtorp::SPSCQueue<pattern_edit>;

// Sole producer only, without reentrant submissions. Consumer can only free
// space between these checks and publication, so both pushes must succeed.
// Publish the payload first; audio accesses it only upon consuming its marker.
inline bool try_submit_pattern_edit(command_queue& commands, pattern_edit_queue& edits,
                                    const pattern_edit& edit) noexcept {
    if (commands.size() >= commands.capacity() || edits.size() >= edits.capacity())
        return false;
    const bool payload_pushed = edits.try_push(edit);
    assert(payload_pushed);
    const bool marker_pushed = commands.try_push(
        sequencer_command{._type = sequencer_command::type::update_pattern});
    assert(marker_pushed);
    (void)payload_pushed;
    (void)marker_pushed;
    return true;
}

} // namespace bork
