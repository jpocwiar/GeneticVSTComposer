/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
GeneticVSTComposerJUCEAudioProcessorEditor::GeneticVSTComposerJUCEAudioProcessorEditor (GeneticVSTComposerJUCEAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (400, 600);

    //===main data
    //string
    scaleTxt.setBounds(180, 50, 120, 30);
    scaleTxt.setInputRestrictions(15);
    scaleTxt.setText("A Minor");
    addAndMakeVisible(scaleTxt);

    //int
    minNoteRangeTxt.setBounds(100, 120, 50, 30);
    minNoteRangeTxt.setInputRestrictions(4, "0123456789");
    minNoteRangeTxt.setText("0");
    addAndMakeVisible(minNoteRangeTxt);
    //int
    maxNoteRangeTxt.setBounds(300, 120, 50, 30);
    maxNoteRangeTxt.setInputRestrictions(4, "0123456789");
    maxNoteRangeTxt.setText("127");
    addAndMakeVisible(maxNoteRangeTxt);
    //int
    minMeterTxt.setBounds(100, 190, 50, 30);
    minMeterTxt.setInputRestrictions(2, "0123456789");
    minMeterTxt.setText("4");
    addAndMakeVisible(minMeterTxt);
    //int
    maxMeterTxt.setBounds(300, 190, 50, 30);
    maxMeterTxt.setInputRestrictions(2, "0123456789");
    maxMeterTxt.setText("4");
    addAndMakeVisible(maxMeterTxt);
    //double
    noteDurationTxt.setBounds(180, 225, 60, 30);
    noteDurationTxt.setInputRestrictions(6, "0123456789.");
    noteDurationTxt.setText("0.25");
    addAndMakeVisible(noteDurationTxt);
    
    //===GA data
    //int
    populationSizeTxt.setBounds(240, 260, 60, 30);
    populationSizeTxt.setInputRestrictions(6, "0123456789");
    populationSizeTxt.setText("100");
    addAndMakeVisible(populationSizeTxt);
    //int
    genNumberTxt.setBounds(240, 295, 60, 30);
    genNumberTxt.setInputRestrictions(6, "0123456789");
    genNumberTxt.setText("200");
    addAndMakeVisible(genNumberTxt);


    startGenBtn.setBounds(100, 350, 150, 30);
    startGenBtn.setButtonText("Generate!");
    startGenBtn.addListener(this);
    addAndMakeVisible(&startGenBtn);

    debugLabel.setBounds(50, 400, 350, 120);
    debugLabel.setText("DEBUG", juce::NotificationType::dontSendNotification);
    addAndMakeVisible(&debugLabel);
}

GeneticVSTComposerJUCEAudioProcessorEditor::~GeneticVSTComposerJUCEAudioProcessorEditor()
{
    startGenBtn.removeListener(this);
}

void GeneticVSTComposerJUCEAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if (button == &startGenBtn)//Start Generation Button clicked
    {
        //TODO - set the debugInfo string that will be shown in the window (get it from generator)
        audioProcessor.GenerateMelody(scaleTxt.getText().toStdString(), { minNoteRangeTxt.getText().getIntValue(), maxNoteRangeTxt.getText().getIntValue() },
            { minMeterTxt.getText().getIntValue(), maxMeterTxt.getText().getIntValue() }, noteDurationTxt.getText().getDoubleValue(),
            populationSizeTxt.getText().getIntValue(), genNumberTxt.getText().getIntValue());

        //update the debug info on the plugin window
        debugLabel.setText(audioProcessor.debugInfo, juce::NotificationType::dontSendNotification);
    }
}

//==============================================================================
void GeneticVSTComposerJUCEAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    g.drawFittedText ("Scale", 50, 50, 50, 30, juce::Justification::centredLeft, 1);

    g.drawFittedText("Note range", 0, 85, getWidth(), 30, juce::Justification::centred, 1);
    g.drawFittedText("Min", 50, 120, 30, 30, juce::Justification::centredLeft, 1);
    g.drawFittedText("Max", 250, 120, 30, 30, juce::Justification::centredLeft, 1);

    g.drawFittedText("Meter", 0, 155, getWidth(), 30, juce::Justification::centred, 1);
    g.drawFittedText("Upper", 50, 190, 40, 30, juce::Justification::centredLeft, 1);
    g.drawFittedText("Lower", 250, 190, 40, 30, juce::Justification::centredLeft, 1);

    g.drawFittedText("Note duration", 50, 225, 100, 30, juce::Justification::centredLeft, 1);

    g.drawFittedText("Population size", 50, 260, 100, 30, juce::Justification::centredLeft, 1);
    
    g.drawFittedText("no generations", 50, 295, 100, 30, juce::Justification::centredLeft, 1);
}

void GeneticVSTComposerJUCEAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
}
