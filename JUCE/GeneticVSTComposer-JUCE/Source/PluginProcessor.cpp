/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "GA/genetic.hpp"

//==============================================================================
GeneticVSTComposerJUCEAudioProcessor::GeneticVSTComposerJUCEAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

GeneticVSTComposerJUCEAudioProcessor::~GeneticVSTComposerJUCEAudioProcessor()
{
}

//==============================================================================
const juce::String GeneticVSTComposerJUCEAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool GeneticVSTComposerJUCEAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool GeneticVSTComposerJUCEAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool GeneticVSTComposerJUCEAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double GeneticVSTComposerJUCEAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int GeneticVSTComposerJUCEAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int GeneticVSTComposerJUCEAudioProcessor::getCurrentProgram()
{
    return 0;
}

void GeneticVSTComposerJUCEAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String GeneticVSTComposerJUCEAudioProcessor::getProgramName (int index)
{
    return {};
}

void GeneticVSTComposerJUCEAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void GeneticVSTComposerJUCEAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
}

void GeneticVSTComposerJUCEAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool GeneticVSTComposerJUCEAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void GeneticVSTComposerJUCEAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear(); // Clear the audio buffer to prepare it for new audio data.

    const int sampleRate = getSampleRate();
    const double noteLengthSeconds = 0.5; // Example note length: 0.5 seconds for each note or pause
    const int noteLengthSamples = static_cast<int>(noteLengthSeconds * sampleRate);

    // Prepare to iterate over incoming MIDI messages
    juce::MidiBuffer processedMidi;
    juce::MidiMessage m;
    int time;

    // Variables to handle note off timing
    int noteOffTime = -1;
    int lastNoteValue = -1;

    for (const auto metadata : midiMessages) {
        m = metadata.getMessage();
        time = metadata.samplePosition;

        if (m.isNoteOn() && m.getNoteNumber() == 60 && !melodyReadyToPlay) {
            // Initialize your GA here and generate the melody
            melodyReadyToPlay = true;
            currentNoteIndex = 0;
            sampleCount = 0; // Reset sample count for the new melody playback
        }
    }

    if (melodyReadyToPlay && currentNoteIndex < generatedMelody.size()) {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            if (sampleCount <= 0) {
                if (currentNoteIndex < generatedMelody.size()) {
                    int noteValue = generatedMelody[currentNoteIndex++];

                    if (noteValue >= 0) { // Start a new note
                        processedMidi.addEvent(juce::MidiMessage::noteOn(1, noteValue, juce::uint8(127)), sample);
                        noteOffTime = sample + noteLengthSamples;
                        lastNoteValue = noteValue;
                    }
                    else if (noteValue == -2 && lastNoteValue != -1) { // Prolong the previous note
                        noteOffTime += noteLengthSamples; // Extend the note off time
                    } // -1 for pause is implicitly handled by just advancing the time

                    sampleCount = noteLengthSamples; // Reset the sample counter after each note or pause
                }
            }
            else {
                sampleCount--;
            }

            // Turn off the last note if its time has come
            if (sample == noteOffTime && lastNoteValue != -1) {
                processedMidi.addEvent(juce::MidiMessage::noteOff(1, lastNoteValue), sample);
                lastNoteValue = -1; // Reset the last note value
            }
        }
    }

    // Swap the midiMessages with processedMidi to include our generated notes and pauses
    midiMessages.swapWith(processedMidi);

    // If the melody is finished, reset the flag to allow new melody generation
    if (currentNoteIndex >= generatedMelody.size()) {
        melodyReadyToPlay = false;
    }
}


//==============================================================================
bool GeneticVSTComposerJUCEAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* GeneticVSTComposerJUCEAudioProcessor::createEditor()
{
    return new GeneticVSTComposerJUCEAudioProcessorEditor (*this);
}

//==============================================================================
void GeneticVSTComposerJUCEAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void GeneticVSTComposerJUCEAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GeneticVSTComposerJUCEAudioProcessor();
}
