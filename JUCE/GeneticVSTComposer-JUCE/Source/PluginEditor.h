/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class GeneticVSTComposerJUCEAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                                    public juce::Button::Listener
{
public:
    GeneticVSTComposerJUCEAudioProcessorEditor (GeneticVSTComposerJUCEAudioProcessor&);
    ~GeneticVSTComposerJUCEAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void updateDebug();

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    GeneticVSTComposerJUCEAudioProcessor& audioProcessor;

    void buttonClicked(juce::Button* button) override;

    int counter = 1;

    juce::TextButton startGenBtn;
    //main data
    juce::TextEditor scaleTxt;//string
    juce::TextEditor minNoteRangeTxt;//int
    juce::TextEditor maxNoteRangeTxt;//int
    juce::TextEditor minMeterTxt;//int
    juce::TextEditor maxMeterTxt;//int
    juce::TextEditor noteDurationTxt;//float
    //GA data
    juce::TextEditor populationSizeTxt;//int
    juce::TextEditor genNumberTxt;//int

    juce::Label debugLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GeneticVSTComposerJUCEAudioProcessorEditor)
};
