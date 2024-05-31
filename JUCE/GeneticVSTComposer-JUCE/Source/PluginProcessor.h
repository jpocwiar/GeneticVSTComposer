/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "genetic.hpp"
#include "notes_generator.hpp"

//==============================================================================
/**
*/
class GeneticVSTComposerJUCEAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    GeneticVSTComposerJUCEAudioProcessor();
    ~GeneticVSTComposerJUCEAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //          CUSTOM
    //Method to generate melody and save it in the processor using Genetic Algorithms
    void GenerateMelody(std::string scale, std::pair<int, int> noteRange,
        double noteDuration, int populationSize, int numGenerations);
    std::vector<int> originalMelody;
    std::vector<int> melody;
    std::vector<int> melodyTemplate;
    std::vector<std::vector<int>> melodies;
    std::string debugInfo;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GeneticVSTComposerJUCEAudioProcessor)
    juce::AudioPlayHead::CurrentPositionInfo lastPosInfo;
    std::vector<std::pair<int, int>> noteOffTimes; // To track note offs outside the current block
    std::array<int, 16> notes {60, 61, 62, -2, -2, -2, 65, -1, 66, 68, -1, 61, 62,63,70, 80};
    int currentNoteIndex = 0;
    int nextNoteTime = 0;
    std::vector<int> g_scale_notes;
    std::pair<int, int> meter{ 4, 4 };
    int samplesBetweenNotes = 48000;
    bool isSequencePlaying = false;
    int activeNotesCount = 0; // Track how many keys are pressed
    int lastNote = -1;
    int selectedMelodyIndex = -1;
    int transposition = 0;
    int initialVelocity;
    int composeMode = 0;

    void adjustMelodyForMeter()
    {
        if (originalMelody.empty())
            return;

        // Calculate the number of notes in the melody for one measure
        int currentMeasureLength = meter.first * 4 / meter.second;
        int newMeasureLength = meter.first * 4 / meter.second;

        // Adjust melody length
        std::vector<int> newMelody;
        if (newMeasureLength < currentMeasureLength) {
            // Cut the melody to fit the new meter
            for (int i = 0; i < newMeasureLength; ++i) {
                newMelody.push_back(originalMelody[i % originalMelody.size()]);
            }
        }
        else {
            // Extend the melody to fit the new meter
            newMelody = originalMelody;
            for (int i = originalMelody.size(); i < newMeasureLength; ++i) {
                newMelody.push_back(originalMelody[i % originalMelody.size()]);
            }
        }
        melody = newMelody;
    }

};
