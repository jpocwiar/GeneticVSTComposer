#pragma once
#include "ComposerEngine.h"
#include <JuceHeader.h>

// Shared by the piano roll, the guide and the editor. Call sites stay identical to the
// helpers that used to live in PluginEditor.cpp.
inline juce::Font font(float size, bool bold = false) {
#if JUCE_MAC
    return juce::Font(juce::FontOptions("Avenir Next", size, bold ? juce::Font::bold : juce::Font::plain));
#else
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
#endif
}
inline juce::String noteName(int pitch) {
    return juce::MidiMessage::getMidiNoteName(pitch, true, true, 4);
}
// E4-50 is E4 a quarter tone flat.
inline juce::String noteName(const composer::Note& note, bool microtonal) {
    if (!microtonal || note.cents == 0)
        return noteName(note.pitch);
    return noteName(note.pitch) + (note.cents > 0 ? "+" : "") + juce::String(note.cents);
}
