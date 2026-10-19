#include "ActivityDisplay.h"
#include "ComposerTheme.h"
#include <algorithm>
#include <cmath>

namespace {
float unit(float value) {
    return std::isfinite(value) ? juce::jlimit(0.0f, 1.0f, value) : 0.0f;
}
float releaseTowards(float previous, float current, float release) {
    const auto value = std::max(current, previous * release);
    return value < 0.00001f ? 0.0f : value;
}
juce::Font displayFont(float size, bool bold = false) {
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace

ActivityDisplay::ActivityDisplay() {
    setInterceptsMouseClicks(false, false);
    setTitle("MIDI activity and output level");
    setDescription("The MIDI trace shows emitted note events. OUT measures the internal audio output; "
                   "MIDI remains visible when Internal sound is off.");
}

void ActivityDisplay::update(float outputPeak, float midiEnergy, uint64_t noteOnSerial, bool playing,
                             bool generating, float progress, double beat, bool animate) {
    const auto previousOutput = output, previousMidi = midiLight, previousBeat = beatLight;
    const bool statusChanged = running != playing || searching != generating ||
                               int(searchProgress * 100) != int(unit(progress) * 100);
    running = playing;
    searching = generating;
    searchProgress = unit(progress);

    // A reset serial is preparation, not a newly generated note. The first update
    // must not turn the processor's historical count into an invented onset.
    const bool onset = serialInitialised && noteOnSerial > lastSerial;
    lastSerial = noteOnSerial;
    serialInitialised = true;
    const auto energy = unit(midiEnergy);
    output = releaseTowards(output, unit(outputPeak), 0.9f);
    midiLight = releaseTowards(midiLight, onset ? std::max(0.55f, energy) : energy, 0.82f);
    const double fraction = std::isfinite(beat) ? beat - std::floor(beat) : 0.0;
    beatLight = playing ? (animate ? float(std::exp(-fraction * 6.0)) : 0.3f) : 0.0f;

    bool historyChanged = false;
    const bool hasHistory = std::any_of(onsets.begin(), onsets.end(), [](bool active) { return active; });
    if (animate && (playing || onset || hasHistory)) {
        // 20 columns per second at the owner's 60 Hz refresh: 2.4 seconds of MIDI
        // events, bounded storage, and no scrolling once the last event has left.
        if (++scrollFrames >= 3) {
            scrollFrames = 0;
            newest = (newest + 1) % historySize;
            velocities[newest] = 0;
            onsets[newest] = false;
            historyChanged = hasHistory;
        }
    } else {
        scrollFrames = 0;
    }
    if (onset) {
        onsets[newest] = true;
        velocities[newest] = std::max(velocities[newest], energy);
        historyChanged = true;
    }
    if (statusChanged || historyChanged || std::abs(previousOutput - output) > 0.0005f ||
        std::abs(previousMidi - midiLight) > 0.005f || std::abs(previousBeat - beatLight) > 0.005f)
        repaint();
}

void ActivityDisplay::paint(juce::Graphics& g) {
    using namespace palette;
    auto shell = getLocalBounds().toFloat().reduced(0.5f);
    if (shell.getWidth() < 90 || shell.getHeight() < 26)
        return;

    g.setGradientFill(juce::ColourGradient(panel.brighter(0.025f), shell.getTopLeft(),
                                           background.withAlpha(0.85f), shell.getBottomLeft(), false));
    g.fillRoundedRectangle(shell, 8);
    g.setColour(line.withAlpha(0.8f));
    g.drawRoundedRectangle(shell, 8, 1);
    auto inner = shell.reduced(10, 7);
    auto outputArea = inner.removeFromRight(26);
    inner.removeFromRight(10);
    auto topRow = inner.removeFromTop(11);
    inner.removeFromTop(3);

    const auto statusColour = searching ? violet : running ? accent : muted;
    const auto statusDot = juce::Rectangle<float>(topRow.getX(), topRow.getCentreY() - 2, 4, 4);
    if (running && beatLight > 0.1f) {
        g.setColour(accent.withAlpha(beatLight * 0.12f));
        g.fillEllipse(statusDot.expanded(3));
    }
    g.setColour(statusColour.withAlpha(running ? 0.65f + beatLight * 0.35f : 1.0f));
    g.fillEllipse(statusDot);
    g.setFont(displayFont(9.5f, true));
    g.setColour(statusColour);
    const juce::String status = searching ? "SEARCH " + juce::String(int(searchProgress * 100)) + "%"
                                : running ? "PLAYING"
                                          : "READY";
    auto statusBounds = topRow.withTrimmedLeft(9);
    statusBounds.removeFromRight(35);
    g.drawText(status, statusBounds, juce::Justification::centredLeft, false);

    auto midiLabel = topRow.removeFromRight(23);
    g.setFont(displayFont(8.0f, true));
    g.setColour(muted.withAlpha(0.75f));
    g.drawText("MIDI", midiLabel, juce::Justification::centredRight, false);
    g.setColour(teal.withAlpha(0.15f + midiLight * 0.85f));
    g.fillEllipse(midiLabel.getX() - 7, midiLabel.getCentreY() - 1.5f, 3, 3);

    const float base = inner.getBottom() - 1;
    const float height = std::max(1.0f, inner.getHeight() - 1);
    const float spacing = inner.getWidth() / float(historySize);
    g.setColour(line.withAlpha(0.8f));
    g.drawHorizontalLine(int(base), inner.getX(), inner.getRight());
    for (size_t i = 0; i < historySize; ++i) {
        const auto index = (newest + 1 + i) % historySize;
        if (!onsets[index])
            continue;
        const float age = float(i + 1) / float(historySize);
        const float x = inner.getX() + (float(i) + 0.5f) * spacing;
        // If the note began and ended within one audio block, its serial still
        // records the onset but held-note velocity may be zero. Draw a small
        // event tick, rather than claiming an unknown velocity measurement.
        const float impulse = std::max(1.5f, velocities[index] * height);
        g.setColour(teal.withAlpha(0.2f + age * 0.65f));
        g.drawLine(x, base, x, base - impulse, std::max(1.0f, std::min(2.0f, spacing - 0.8f)));
        if (age > 0.85f && velocities[index] > 0.15f) {
            g.setColour(teal.withAlpha(0.07f));
            g.drawLine(x, base, x, base - impulse, 4);
        }
    }
    if (searching) {
        g.setColour(violet.withAlpha(0.25f));
        g.fillRoundedRectangle(inner.getX(), base - 1, inner.getWidth(), 2, 1);
        g.setColour(violet.withAlpha(0.85f));
        g.fillRoundedRectangle(inner.getX(), base - 1, inner.getWidth() * searchProgress, 2, 1);
    }

    g.setColour(line.withAlpha(0.6f));
    g.drawVerticalLine(int(outputArea.getX() - 5), shell.getY() + 9, shell.getBottom() - 9);
    g.setFont(displayFont(8.0f, true));
    g.setColour(muted.withAlpha(0.75f));
    g.drawText("OUT", outputArea.removeFromTop(11), juce::Justification::centred, false);
    outputArea.removeFromTop(4);
    const float level = output > 0.00001f ? unit((20.0f * std::log10(output) + 48.0f) / 48.0f) : 0;
    const float segmentWidth = outputArea.getWidth() / 8;
    for (int i = 0; i < 8; ++i) {
        auto segment =
            juce::Rectangle<float>(outputArea.getX() + i * segmentWidth, outputArea.getCentreY() - 3,
                                   std::max(1.0f, segmentWidth - 1), 6);
        g.setColour(level * 8 > i ? (i >= 6 ? gold : teal).withAlpha(0.85f) : line.withAlpha(0.8f));
        g.fillRoundedRectangle(segment, 0.8f);
    }
}
