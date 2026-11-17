#include "AiMidiProvider.h"
#include "BinaryData.h"
#include <cmath>

namespace composer::ai {
namespace {
juce::File runtimeDirectory(const juce::File& python) {
    return python.getParentDirectory().getParentDirectory();
}
juce::File executableIn(const juce::File& directory) {
#if JUCE_WINDOWS
    return directory.getChildFile("Scripts/python.exe");
#else
    return directory.getChildFile("bin/python");
#endif
}
bool modelFilesExist(const juce::File& model, const juce::File& vendor) {
    return model.getChildFile("config.json").existsAsFile() &&
           model.getChildFile("model_base.onnx").getSize() > 1000000 &&
           model.getChildFile("model_token.onnx").getSize() > 1000000 &&
           vendor.getChildFile("midi_tokenizer.py").existsAsFile();
}
struct TemporaryDirectory {
    juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("Genetic-Composer-AI", "", false);
    ~TemporaryDirectory() { file.deleteRecursively(); }
};
juce::Result validMidi(const juce::File& file) {
    if (!file.existsAsFile() || file.getSize() <= 14 || file.getSize() > 2 * 1024 * 1024)
        return juce::Result::fail("Local AI returned an empty or oversized MIDI file.");
    juce::FileInputStream input(file);
    juce::MidiFile midi;
    if (!input.openedOk() || !midi.readFrom(input) || midi.getTimeFormat() <= 0)
        return juce::Result::fail("Local AI returned invalid musical-time MIDI.");
    int notes = 0;
    for (int track = 0; track < midi.getNumTracks(); ++track)
        for (int event = 0; event < midi.getTrack(track)->getNumEvents(); ++event) {
            const auto& message = midi.getTrack(track)->getEventPointer(event)->message;
            if (!std::isfinite(message.getTimeStamp()) || message.getTimeStamp() < 0)
                return juce::Result::fail("Local AI returned invalid MIDI timing.");
            notes += message.isNoteOn() ? 1 : 0;
        }
    return notes > 0 && notes <= 512 ? juce::Result::ok()
                                     : juce::Result::fail("Local AI returned no usable phrase.");
}
} // namespace

juce::File defaultPython() {
    const auto configured = juce::SystemStats::getEnvironmentVariable("GENETIC_COMPOSER_AI_PYTHON", {});
    if (configured.isNotEmpty() && juce::File::isAbsolutePath(configured)) {
        const juce::File python(configured);
        if (python.existsAsFile())
            return python;
    }
    // A trusted development executable may discover its sibling runtime. Never search
    // the working/project directory: a transferred project must not choose executable code.
    auto directory = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    for (int level = 0; level < 8; ++level) {
        const auto python = executableIn(directory.getChildFile(".deps/ai-midi"));
        if (python.existsAsFile())
            return python;
        const auto parent = directory.getParentDirectory();
        if (parent == directory)
            break;
        directory = parent;
    }
    return {};
}

juce::String availability(const juce::String& pythonExecutable) {
    if (pythonExecutable.isNotEmpty() && !juce::File::isAbsolutePath(pythonExecutable))
        return "Local AI: choose an absolute Python interpreter path.";
    const auto python = pythonExecutable.isEmpty() ? defaultPython() : juce::File(pythonExecutable);
    if (!python.existsAsFile())
        return "Local AI: choose the Python interpreter created by setup-ai-midi.sh.";
    const auto runtime = runtimeDirectory(python);
    return modelFilesExist(runtime.getChildFile("models"), runtime.getChildFile("vendor"))
               ? "Local AI ready. Inference runs offline."
               : "Local AI: model files are missing. Run setup-ai-midi.sh first.";
}

juce::Result generate(const Request& request, const std::atomic<bool>& cancelled) {
    if (cancelled.load())
        return juce::Result::fail("Local AI generation cancelled.");
    if (request.pythonExecutable.isNotEmpty() && !juce::File::isAbsolutePath(request.pythonExecutable))
        return juce::Result::fail("Choose an absolute Python interpreter path.");
    if (request.outputMidi == juce::File() || request.outputMidi == request.seedMidi || request.bars < 1 ||
        request.bars > 16 || request.numerator < 1 || request.numerator > 32 || request.denominator < 1 ||
        request.denominator > 32 || (request.denominator & (request.denominator - 1)) != 0 ||
        request.bars * request.numerator * 4.0 / request.denominator > 128.0 || request.root < 0 ||
        request.root > 11 || request.role < 0 || request.role > 3)
        return juce::Result::fail("Invalid local AI request.");
    const auto python =
        request.pythonExecutable.isEmpty() ? defaultPython() : juce::File(request.pythonExecutable);
    if (!python.existsAsFile())
        return juce::Result::fail(availability(request.pythonExecutable));
    const auto runtime = runtimeDirectory(python);
    const auto models =
        request.modelDirectory == juce::File() ? runtime.getChildFile("models") : request.modelDirectory;
    const auto vendor =
        request.vendorDirectory == juce::File() ? runtime.getChildFile("vendor") : request.vendorDirectory;
    if (!modelFilesExist(models, vendor))
        return juce::Result::fail("Local AI model files are missing. Run setup-ai-midi.sh first.");
    TemporaryDirectory temporary;
    if (const auto result = temporary.file.createDirectory(); result.failed())
        return result;
    auto script = request.bridgeScript;
    if (script == juce::File()) {
        script = temporary.file.getChildFile("ai-midi-bridge.py");
        if (!script.replaceWithData(BinaryData::aimidibridge_py, BinaryData::aimidibridge_pySize))
            return juce::Result::fail("Could not prepare the local AI worker.");
    }
    if (!script.existsAsFile())
        return juce::Result::fail("The local AI bridge script is missing.");
    const auto output = temporary.file.getChildFile("proposal.mid");
    juce::StringArray arguments{python.getFullPathName(),
                                "-B",
                                script.getFullPathName(),
                                "--output",
                                output.getFullPathName(),
                                "--model-dir",
                                models.getFullPathName(),
                                "--vendor-dir",
                                vendor.getFullPathName(),
                                "--root",
                                juce::String(request.root),
                                "--role",
                                juce::String(request.role),
                                "--bars",
                                juce::String(request.bars),
                                "--numerator",
                                juce::String(request.numerator),
                                "--denominator",
                                juce::String(request.denominator),
                                "--seed",
                                juce::String(request.seed),
                                "--max-events",
                                juce::String(juce::jlimit(16, 512, request.maxEvents))};
    if (request.seedMidi.existsAsFile()) {
        arguments.add("--input");
        arguments.add(request.seedMidi.getFullPathName());
    }
    juce::ChildProcess child;
    if (!child.start(arguments))
        return juce::Result::fail("Could not start the local AI interpreter.");
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const int timeout = juce::jlimit(1000, 600000, request.timeoutMs);
    while (child.isRunning()) {
        if (cancelled.load() || juce::Time::getMillisecondCounterHiRes() - start >= timeout) {
            child.kill();
            child.waitForProcessToFinish(2000);
            return juce::Result::fail(cancelled.load() ? "Local AI generation cancelled."
                                                       : "Local AI generation timed out.");
        }
        juce::Thread::sleep(20);
    }
    const auto response = child.readAllProcessOutput().trim().substring(0, 8192);
    if (child.getExitCode() != 0) {
        juce::StringArray lines;
        lines.addLines(response);
        const auto status = juce::JSON::parse(lines.isEmpty() ? response : lines[lines.size() - 1]);
        auto error = status.getProperty("error", {}).toString().substring(0, 500);
        return juce::Result::fail(error.isNotEmpty()
                                      ? "Local AI: " + error
                                      : "The local AI worker failed. Check its runtime installation.");
    }
    if (const auto result = validMidi(output); result.failed())
        return result;
    if (cancelled.load())
        return juce::Result::fail("Local AI generation cancelled.");
    if (!output.copyFileTo(request.outputMidi))
        return juce::Result::fail("Could not save the local AI proposal.");
    return juce::Result::ok();
}
} // namespace composer::ai
