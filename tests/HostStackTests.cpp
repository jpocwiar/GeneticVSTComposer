#include "PluginProcessor.h"

#include <cmath>
#include <exception>
#include <iostream>
#include <memory>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif

namespace {
using Processor = GeneticVSTComposerJUCEAudioProcessor;
constexpr size_t hostStackBytes = 512 * 1024;

struct RestoreCall {
    Processor& processor;
    const juce::MemoryBlock& state;
    bool completed = false;
    bool matched = false;
};

void restoreOnHostThread(RestoreCall& call) {
    try {
        call.processor.setStateInformation(call.state.getData(), int(call.state.getSize()));
        call.matched = std::abs(call.processor.parameters.getRawParameterValue("density")->load() - 0.73f) < 1e-5f;
        juce::MemoryBlock savedAgain;
        call.processor.getStateInformation(savedAgain);
        call.completed = savedAgain.getSize() > 0;
    } catch (...) {
        call.completed = false;
    }
}

#if JUCE_WINDOWS
unsigned __stdcall restoreEntry(void* context) {
    restoreOnHostThread(*static_cast<RestoreCall*>(context));
    return 0;
}
#else
void* restoreEntry(void* context) {
    restoreOnHostThread(*static_cast<RestoreCall*>(context));
    return nullptr;
}
#endif

bool runOnSmallStack(RestoreCall& call) {
#if JUCE_WINDOWS
    const auto thread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, unsigned(hostStackBytes), restoreEntry, &call,
                                                               STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr));
    if (thread == nullptr)
        return false;
    const auto result = WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return result == WAIT_OBJECT_0;
#else
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0)
        return false;
    const int sized = pthread_attr_setstacksize(&attributes, hostStackBytes);
    pthread_t thread{};
    const int created = sized == 0 ? pthread_create(&thread, &attributes, restoreEntry, &call) : sized;
    pthread_attr_destroy(&attributes);
    return created == 0 && pthread_join(thread, nullptr) == 0;
#endif
}
} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    auto processor = std::make_unique<Processor>();
    auto* density = processor->parameters.getParameter("density");
    if (density == nullptr)
        return 1;
    density->setValueNotifyingHost(density->convertTo0to1(0.73f));
    juce::MemoryBlock saved;
    processor->getStateInformation(saved);

    // Exercise the full workspace and a pre-workspace session. Both must fit a host's
    // worker stack, rather than depending on the larger stack of the GUI/main thread.
    auto legacyXml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), int(saved.getSize()));
    if (legacyXml == nullptr)
        return 1;
    auto legacy = juce::ValueTree::fromXml(*legacyXml);
    legacy.setProperty("version", 6, nullptr);
    legacy.removeChild(legacy.getChildWithName("MicrotonalWorkspace"), nullptr);
    legacy.removeChild(legacy.getChildWithName("CompositionWorkspace"), nullptr);
    juce::MemoryBlock beforeWorkspaces;
    juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(), beforeWorkspaces);

    for (const auto* state : {&saved, &beforeWorkspaces}) {
        density->setValueNotifyingHost(density->convertTo0to1(0.12f));
        RestoreCall call{*processor, *state};
        if (!runOnSmallStack(call) || !call.completed || !call.matched) {
            std::cerr << "State round trip failed on a 512 KiB host thread\n";
            return 1;
        }
    }
    std::cout << "State save/restore on a 512 KiB host thread: PASS (full and legacy workspaces)\n";
}
