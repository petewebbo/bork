#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace bork {

// MIDI equal temperament, A4 = 440 Hz; middle C = C4 (60).
// Reference: https://inspiredacoustics.com/en/MIDI_note_numbers_and_center_frequencies
struct midi_note {
    int number;
    std::array<char, 5> name{};
    double frequency;
};

inline constexpr auto MIDI_NOTES = [] {
    constexpr std::array<std::string_view, 12> names{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    std::array<midi_note, 128> notes{};
    double frequency = 8.175798915643707; // MIDI 0, C-1
    constexpr double semitone_ratio = 1.0594630943592953;
    for (size_t number = 0; number < notes.size(); ++number) {
        auto& note = notes[number];
        note.number = static_cast<int>(number);
        note.frequency = frequency;
        const auto pitch = names[number % 12];
        size_t pos = 0;
        for (char ch : pitch) note.name[pos++] = ch;
        const int octave = static_cast<int>(number / 12) - 1;
        if (octave < 0) note.name[pos++] = '-';
        note.name[pos] = static_cast<char>('0' + (octave < 0 ? -octave : octave));
        frequency *= semitone_ratio;
    }
    return notes;
}();

inline constexpr const midi_note* lookup_midi_note(int number) noexcept {
    return number >= 0 && number < static_cast<int>(MIDI_NOTES.size())
        ? &MIDI_NOTES[static_cast<size_t>(number)] : nullptr;
}

} // namespace bork
