#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

// Optional local inference. Call generate() on a worker, never in processBlock().
namespace composer::ai {
struct Request {
    juce::File seedMidi, outputMidi;
    juce::String pythonExecutable;
    // Empty paths use the script embedded in the plugin and the interpreter's runtime directory.
    juce::File bridgeScript, modelDirectory, vendorDirectory;
    int root = 0, role = 0, bars = 4, numerator = 4, denominator = 4;
    uint32_t seed = 1;
    int maxEvents = 192, timeoutMs = 180000;
};
juce::File defaultPython();
juce::String availability(const juce::String& pythonExecutable = {});
juce::Result generate(const Request&, const std::atomic<bool>& cancelled);
} // namespace composer::ai
