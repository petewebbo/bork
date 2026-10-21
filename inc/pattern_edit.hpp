#pragma once

#include "pattern.hpp"
#include <cstdint>

namespace bork {
inline constexpr int DEFAULT_NOTE_NUMBER = 60;
using remembered_notes = std::array<std::array<int, MAX_PATTERN_STEPS>, 8>;
struct pattern_edit {
    enum class kind : uint8_t { region, paste_region, paste_pattern };
    kind type{kind::region};
    size_t top{}, left{}, rows{1}, cols{1};
    pattern value{}; // Region cells use absolute destination coordinates.
};
inline remembered_notes remember_notes(const pattern& value) noexcept {
    remembered_notes notes{};
    for (size_t row = 0; row < value.tracks_.size(); ++row)
        for (size_t col = 0; col < MAX_PATTERN_STEPS; ++col) {
            const int note = value.tracks_[row].steps_[col].note_;
            notes[row][col] = note ? note : DEFAULT_NOTE_NUMBER;
        }
    return notes;
}
inline bool valid_pattern_edit(const pattern_edit& edit) noexcept {
    if (edit.type == pattern_edit::kind::paste_pattern) {
        for (const auto& track : edit.value.tracks_)
            if (track.size_ > MAX_PATTERN_STEPS) return false;
        return true;
    }
    if (edit.type != pattern_edit::kind::region && edit.type != pattern_edit::kind::paste_region)
        return false;
    return edit.top < 8 && edit.left < MAX_PATTERN_STEPS && edit.rows > 0 && edit.cols > 0 &&
        edit.rows <= 8 - edit.top && edit.cols <= MAX_PATTERN_STEPS - edit.left;
}
// Caller validates first; UI uses the same operation for its optimistic copy.
inline void apply_pattern_edit(pattern& destination, remembered_notes& notes,
                               const pattern_edit& edit) noexcept {
    if (edit.type == pattern_edit::kind::paste_pattern) {
        destination = edit.value;
        notes = remember_notes(destination);
        return;
    }
    for (size_t row = edit.top; row < edit.top + edit.rows; ++row)
        for (size_t col = edit.left; col < edit.left + edit.cols; ++col) {
            const auto value = edit.value.tracks_[row].steps_[col];
            destination.tracks_[row].steps_[col] = value;
            if (value.note_ || edit.type == pattern_edit::kind::paste_region)
                notes[row][col] = value.note_ ? value.note_ : DEFAULT_NOTE_NUMBER;
        }
}

struct store_request {
    enum class kind : uint8_t { select, edit, resync, start, stop, tempo };
    kind type{kind::select};
    uint64_t request_id{}, session{};
    uint8_t pattern_id{};
    bool deferred{true};
    float tempo{120};
    pattern_edit edit{};
};
struct store_result {
    store_request::kind type{};
    uint64_t request_id{}, session{};
    uint8_t pattern_id{};
    bool accepted{};
};
struct editing_snapshot {
    uint64_t session{}, request_id{};
    uint8_t pattern_id{};
    bool recovery{};
    pattern value{};
    remembered_notes notes{};
};
} // namespace bork
