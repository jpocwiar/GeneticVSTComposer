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

    std::pair<int, int> SpeedQualityValues[3] = { {64, 50}, {128, 100}, {256, 200} };//populations, generations (speed <--> quality)

    int counter = 1;

    juce::TextButton startGenBtn;

    //===NEW DATA===
    juce::ComboBox scaleBox1;
    juce::ComboBox scaleBox2;
    juce::ComboBox seqLenBox;
    juce::ComboBox noteDurationBox;
    juce::Slider diversitySlid;
    juce::Slider dynamicsSlid;
    juce::Slider arousalSlid;
    juce::Slider valenceSlid;
    juce::Slider jazzinessSlid;
    juce::Slider weirdnessSlid;
    juce::Slider noteRangeSlid;
    juce::Slider speedQualitySlid;

    juce::Label scaleLbl;
    juce::Label seqLenLbl;
    juce::Label noteDurationLbl;
    juce::Label diversityLbl;
    juce::Label dynamicsLbl;
    juce::Label arousalLbl;
    juce::Label valenceLbl;
    juce::Label jazzinessLbl;
    juce::Label weirdnessLbl;
    juce::Label noteRangeLbl;
    juce::Label speedLbl;
    juce::Label qualityLbl;

    //===OLD DATA===
    //---main data
    //juce::TextEditor scaleTxt;//string
    //juce::TextEditor minNoteRangeTxt;//int
    //juce::TextEditor maxNoteRangeTxt;//int
    juce::TextEditor minMeterTxt;//int
    juce::TextEditor maxMeterTxt;//int
    //juce::TextEditor noteDurationTxt;//float
    //---GA data
    //juce::TextEditor populationSizeTxt;//int
    //juce::TextEditor genNumberTxt;//int

    juce::Label debugLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GeneticVSTComposerJUCEAudioProcessorEditor)
};
