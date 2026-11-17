#include "AiMidiProvider.h"
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Workspace {
    juce::File directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getNonexistentChildFile("genetic-ai-provider-test", "", false);
    Workspace() { check(directory.createDirectory().wasOk(), "AI test temporary directory"); }
    ~Workspace() { directory.deleteRecursively(); }
};
juce::File findPython() {
    if (const auto selected = composer::ai::defaultPython(); selected.existsAsFile())
        return selected;
#if JUCE_WINDOWS
    const char* separator = ";";
#else
    const char* separator = ":";
#endif
    const auto paths =
        juce::StringArray::fromTokens(juce::SystemStats::getEnvironmentVariable("PATH", {}), separator, "");
    for (const auto& path : paths)
        for (const char* name : {"python3", "python", "python.exe"}) {
            if (!juce::File::isAbsolutePath(path))
                continue;
            const auto executable = juce::File(path).getChildFile(name);
            if (executable.existsAsFile())
                return executable;
        }
    return {};
}
void sparseModel(const juce::File& file) {
    juce::FileOutputStream output(file);
    check(output.openedOk() && output.setPosition(1000001) && output.writeByte(0),
          "AI test sparse model fixture");
}
composer::ai::Request fakeRequest(Workspace& workspace, const juce::File& python) {
    composer::ai::Request request;
    request.pythonExecutable = python.getFullPathName();
    request.modelDirectory = workspace.directory.getChildFile("models");
    request.vendorDirectory = workspace.directory.getChildFile("vendor");
    request.outputMidi = workspace.directory.getChildFile("result.mid");
    request.bridgeScript = workspace.directory.getChildFile("bridge.py");
    check(request.modelDirectory.createDirectory().wasOk() &&
              request.vendorDirectory.createDirectory().wasOk(),
          "AI test runtime directories");
    check(request.modelDirectory.getChildFile("config.json").replaceWithText("{}") &&
              request.vendorDirectory.getChildFile("midi_tokenizer.py").replaceWithText("# test fixture\n"),
          "AI test runtime metadata");
    sparseModel(request.modelDirectory.getChildFile("model_base.onnx"));
    sparseModel(request.modelDirectory.getChildFile("model_token.onnx"));
    return request;
}
void protocolAndLifecycle() {
    Workspace workspace;
    composer::ai::Request request;
    request.outputMidi = workspace.directory.getChildFile("preserved.mid");
    check(request.outputMidi.replaceWithText("preserved"), "AI test sentinel fixture");
    std::atomic<bool> cancelled{true};
    check(composer::ai::generate(request, cancelled).failed(), "AI ignored prelaunch cancellation");
    check(request.outputMidi.loadFileAsString() == "preserved", "cancelled AI touched an existing output");
    cancelled.store(false);
    request.role = 99;
    check(composer::ai::generate(request, cancelled).failed(), "AI accepted an invalid instrument role");
    request.role = 0;
    request.denominator = 3;
    check(composer::ai::generate(request, cancelled).failed(), "AI accepted an invalid denominator");
    request.denominator = 4;
    request.numerator = 32;
    request.bars = 16;
    check(composer::ai::generate(request, cancelled).failed(), "AI accepted a phrase above engine capacity");
    check(request.outputMidi.loadFileAsString() == "preserved",
          "invalid AI request touched an existing output");
    request.pythonExecutable = "python3";
    check(composer::ai::generate(request, cancelled).failed() &&
              composer::ai::availability("python3").contains("absolute"),
          "AI accepted an ambiguous relative interpreter path");

    // These process tests use a tiny trusted script, never the ONNX weights. CI runners
    // without Python still exercise request validation and prelaunch cancellation above.
    const auto python = findPython();
    if (!python.existsAsFile())
        return;
    auto fake = fakeRequest(workspace, python);
    check(fake.outputMidi.replaceWithText("previous result"), "AI output preservation fixture");
    check(fake.bridgeScript.replaceWithText("import json\nprint(json.dumps({'ok': False, 'error': "
                                            "'controlled worker failure'}))\nraise SystemExit(7)\n"),
          "AI failing worker fixture");
    const auto failure = composer::ai::generate(fake, cancelled);
    check(failure.failed() && failure.getErrorMessage().contains("controlled worker failure"),
          "AI did not report the worker failure");
    check(fake.outputMidi.loadFileAsString() == "previous result", "failed AI overwrote the previous output");

    check(fake.bridgeScript.replaceWithText(
              "import sys\nfrom pathlib import "
              "Path\nPath(sys.argv[sys.argv.index('--output')+1]).write_bytes(b'not MIDI')\n"),
          "AI malformed output fixture");
    check(composer::ai::generate(fake, cancelled).failed(), "AI accepted malformed MIDI from a worker");
    check(fake.outputMidi.loadFileAsString() == "previous result",
          "invalid AI output overwrote previous material");

    check(fake.bridgeScript.replaceWithText("import time\ntime.sleep(10)\n"), "AI timeout fixture");
    fake.timeoutMs = 1000;
    auto began = std::chrono::steady_clock::now();
    const auto timeout = composer::ai::generate(fake, cancelled);
    check(timeout.failed() && timeout.getErrorMessage().contains("timed out"),
          "AI worker timeout was not enforced");
    check(std::chrono::steady_clock::now() - began < std::chrono::seconds(5),
          "AI worker timeout was unbounded");

    fake.timeoutMs = 180000;
    juce::Result result = juce::Result::ok();
    began = std::chrono::steady_clock::now();
    std::thread worker([&] { result = composer::ai::generate(fake, cancelled); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    cancelled.store(true);
    worker.join();
    check(result.failed() && result.getErrorMessage().contains("cancelled"),
          "AI did not cancel a running child");
    check(std::chrono::steady_clock::now() - began < std::chrono::seconds(5),
          "AI child cancellation was unbounded");
    check(fake.outputMidi.loadFileAsString() == "previous result",
          "cancelled worker overwrote the previous output");
}
void realInference() {
    Workspace workspace;
    const auto python = composer::ai::defaultPython();
    check(python.existsAsFile(), "Run setup-ai-midi.sh before the optional native AI smoke test");
    composer::ai::Request request;
    request.pythonExecutable = python.getFullPathName();
    request.outputMidi = workspace.directory.getChildFile("proposal.mid");
    request.bars = 2;
    request.seed = 41;
    request.maxEvents = 64;
    std::atomic<bool> cancelled{false};
    const auto result = composer::ai::generate(request, cancelled);
    if (result.failed())
        throw std::runtime_error(result.getErrorMessage().toStdString());
    juce::FileInputStream input(request.outputMidi);
    juce::MidiFile midi;
    check(midi.readFrom(input) && midi.getTimeFormat() > 0, "native AI did not return tick-based MIDI");
    int notes = 0;
    for (int track = 0; track < midi.getNumTracks(); ++track) {
        auto sequence = *midi.getTrack(track);
        sequence.updateMatchedPairs();
        for (int event = 0; event < sequence.getNumEvents(); ++event) {
            const auto* note = sequence.getEventPointer(event);
            if (note->message.isNoteOn()) {
                ++notes;
                check(note->noteOffObject != nullptr, "native AI left an unmatched note-on");
            }
        }
    }
    check(notes > 0 && notes <= 512 && midi.getLastTimestamp() / midi.getTimeFormat() == 8.0,
          "native AI proposal has invalid note count or phrase length");
}
} // namespace

void runAiMidiProviderTests(bool realInferenceEnabled) {
    if (realInferenceEnabled)
        realInference();
    else
        protocolAndLifecycle();
}
