/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include "PluginProcessor.h"
#include <JuceHeader.h>

//==============================================================================
/**
 */
class GeneticVSTComposerJUCEAudioProcessorEditor
    : public juce::AudioProcessorEditor,
      public juce::Button::Listener {
public:
  GeneticVSTComposerJUCEAudioProcessorEditor(
      GeneticVSTComposerJUCEAudioProcessor &);
  ~GeneticVSTComposerJUCEAudioProcessorEditor() override;

  //==============================================================================
  void paint(juce::Graphics &) override;
  void resized() override;

  void updateDebug();

private:
  // This reference is provided as a quick way for your editor to
  // access the processor object that created it.
  GeneticVSTComposerJUCEAudioProcessor &audioProcessor;

  void buttonClicked(juce::Button *button) override;

  int modeRadioGroupID = 56789;
  std::pair<int, int> SpeedQualityValues[3] = {
      {64, 50},
      {128, 100},
      {256, 200}}; // populations, generations (speed <--> quality)
  juce::StringArray noteDurationStr = {"1/8", "1/16", "1/32"};
  float noteDurationValues[4] = {0.5, 0.25, 0.125};

  int counter = 1;

  juce::TextButton startGenBtn;

  // data GUIs
  juce::TextButton mode0Btn; // 0 - Full melody
  juce::TextButton mode1Btn; // 1 - Craft Rhythm
  juce::TextButton mode2Btn; // 2 - Build melody on rhythm
  juce::ToggleButton scaleSnapBtn;
  juce::ComboBox scaleBox1;
  juce::ComboBox scaleBox2;
  juce::ComboBox seqLenBox;
  juce::ComboBox noteDurationBox;
  juce::Slider diversitySlid;
  juce::Slider dynamicsSlid;
  juce::Slider arousalSlid;
  juce::Slider valenceSlid;
  juce::Slider pauseAmountSlid;
  juce::Slider jazzinessSlid;
  juce::Slider weirdnessSlid;
  juce::Slider noteRangeSlid;
  juce::Slider speedQualitySlid;

  // labels
  juce::Label modeLbl;
  juce::Label scaleLbl;
  juce::Label seqLenLbl;
  juce::Label noteDurationLbl;
  juce::Label diversityLbl;
  juce::Label dynamicsLbl;
  juce::Label arousalLbl;
  juce::Label valenceLbl;
  juce::Label pauseAmountLbl;
  juce::Label jazzinessLbl;
  juce::Label weirdnessLbl;
  juce::Label noteRangeLbl;
  juce::Label speedLbl;
  juce::Label qualityLbl;

  juce::Label separationLabel;

  // debug label
  juce::TextEditor debugTextBox;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
      GeneticVSTComposerJUCEAudioProcessorEditor)
};
