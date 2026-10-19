#pragma once
#include <JuceHeader.h>
#include <array>
#include <cstdint>

// A compact view of emitted MIDI events and the measured audio output level.
// update() and paint() both run on the message thread; the owner supplies telemetry.
class ActivityDisplay : public juce::Component {
  public:
    ActivityDisplay();
    void update(float outputPeak, float midiEnergy, uint64_t noteOnSerial, bool playing, bool generating,
                float progress, double beat, bool animate);
    void paint(juce::Graphics&) override;

  private:
    static constexpr size_t historySize = 48;
    std::array<float, historySize> velocities{};
    std::array<bool, historySize> onsets{};
    size_t newest = 0;
    uint64_t lastSerial = 0;
    int scrollFrames = 0;
    float output = 0, midiLight = 0, beatLight = 0, searchProgress = 0;
    bool serialInitialised = false, running = false, searching = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ActivityDisplay)
};
