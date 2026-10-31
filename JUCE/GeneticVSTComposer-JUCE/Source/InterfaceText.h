#pragma once
#include <juce_core/juce_core.h>

// Editor-local interface language. 0 is English, 1 is Polish. Unknown text stays English.
juce::String interfaceText(const juce::String& english, int language);
