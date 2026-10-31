#pragma once
#include "ComposerEngine.h"
#include <JuceHeader.h>
#include <cmath>

// Shared by audition playback and MIDI export. Internal linkage per translation unit.
namespace {
double swingTime(double time, double grid, double amount) {
    const double pair = std::floor(time / (2.0 * grid)) * (2.0 * grid);
    const double offset = time - pair;
    return pair + (offset <= grid ? offset * (1.0 + amount)
                                  : grid * (1.0 + amount) + (offset - grid) * (1.0 - amount));
}
bool samePlayback(const composer::Clip& a, const composer::Clip& b) {
    if (a.length != b.length || a.grid != b.grid || a.count != b.count)
        return false;
    for (int i = 0; i < a.count; ++i) {
        const auto& x = a.notes[i];
        const auto& y = b.notes[i];
        if (x.start != y.start || x.duration != y.duration || x.pitch != y.pitch ||
            x.velocity != y.velocity || x.cents != y.cents)
            return false;
    }
    return true;
}
constexpr int mpeBendRange = 48;
// 14-bit pitch-wheel position for an offset in cents at a bend range in semitones.
int bendPosition(int cents, int range) {
    return juce::jlimit(0, 16383, 8192 + int(std::lround(cents * 8192.0 / (range * 100.0))));
}
template <typename Add> void addRpn(Add&& add, int channel, int number, int value) {
    add(juce::MidiMessage::controllerEvent(channel, 101, number >> 7));
    add(juce::MidiMessage::controllerEvent(channel, 100, number & 127));
    add(juce::MidiMessage::controllerEvent(channel, 6, value));
}
// Sent before microtonal notes: channel 1's bend range, or for MPE a lower zone of 15 member
// channels at the standard per-note range, with ±2 on the master channel.
template <typename Add> void microtonalSetup(int output, int range, Add&& add) {
    if (output == 1)
        addRpn(add, 1, 0, range);
    if (output == 2) {
        addRpn(add, 1, 6, 15);
        addRpn(add, 2, 0, mpeBendRange);
        addRpn(add, 1, 0, 2);
    }
}
} // namespace
