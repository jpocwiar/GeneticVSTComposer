/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

void InitializeRotarySlider(juce::Slider *slider, std::pair<float, float> range, float interval, juce::LookAndFeel *lookAndFeel)
{
    slider->setSliderStyle(juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag);
    slider->setRange(range.first, range.second, interval);
    slider->setLookAndFeel(lookAndFeel);
    slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, slider->getTextBoxHeight());
}

void InitializeAttatchedLabel(juce::Label *label, const juce::String &text, juce::Component *owner, bool onLeft, juce::Justification justification)
{
    label->setText(text, juce::dontSendNotification);
    label->attachToComponent(owner, onLeft);
    label->setJustificationType(justification);
}

//==============================================================================
GeneticVSTComposerJUCEAudioProcessorEditor::GeneticVSTComposerJUCEAudioProcessorEditor (GeneticVSTComposerJUCEAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (400, 750);

    int currentHeight = 20;
    juce::LookAndFeel *mainLookAndFeel = new juce::LookAndFeel_V4(juce::LookAndFeel_V4::getMidnightColourScheme());
    float rotaryWidth = 80;
    float rotaryHeight = 100;

    //===NEW DATA
    //scale
    scaleBox1.setBounds(130, currentHeight, 60, 30);
    scaleBox1.setLookAndFeel(mainLookAndFeel);
    scaleBox1.addItemList({ "A", "B", "C", "D", "E", "F" }, 1);
    scaleBox1.setSelectedId(1);
    addAndMakeVisible(scaleBox1);

    scaleBox2.setBounds(200, currentHeight, 160, 30);
    scaleBox2.setLookAndFeel(mainLookAndFeel);
    scaleBox2.addSectionHeading("The diatonic scales");
    scaleBox2.addItem("Diatonic", 1);
    scaleBox2.addSectionHeading("Ancient scales");
    scaleBox2.addItem("Ionian", 2);
    scaleBox2.addItem("Dorian", 3);
    scaleBox2.addItem("Phrygian", 4);
    scaleBox2.addItem("Lydian", 5);
    scaleBox2.addItem("Mixolydian", 6);
    scaleBox2.addItem("Aeolian", 7);
    scaleBox2.addItem("Locrian", 8);
    scaleBox2.addSectionHeading("The major scales");
    scaleBox2.addItem("Major", 9);
    scaleBox2.addItem("Harmonic", 10);
    scaleBox2.addSectionHeading("The minor scales");
    scaleBox2.addItem("Natural minor", 11);
    scaleBox2.addItem("Harmonic minor", 12);
    scaleBox2.addItem("Melodic minor", 13);
    scaleBox2.addItem("Bachian", 14);
    scaleBox2.addItem("Minor neapolitan", 15);
    scaleBox2.addSectionHeading("Other scales");
    scaleBox2.addItem("Chromatic", 16);
    scaleBox2.addItem("Whole tone", 17);
    scaleBox2.addItem("Octatonic", 18);
    scaleBox2.setSelectedId(1);
    addAndMakeVisible(scaleBox2);

    scaleLbl.setText("Scale", juce::dontSendNotification);
    scaleLbl.attachToComponent(&scaleBox1, true);
    addAndMakeVisible(scaleLbl);

    currentHeight += 40;

    //sequence length
    seqLenBox.setBounds(130, currentHeight, 60, 30);
    seqLenBox.setLookAndFeel(mainLookAndFeel);
    seqLenBox.addItemList({ "1/2", "1", "2" }, 1);
    seqLenBox.setSelectedId(1);
    addAndMakeVisible(seqLenBox);

    seqLenLbl.setText("Sequence length", juce::dontSendNotification);
    seqLenLbl.attachToComponent(&seqLenBox, true);
    addAndMakeVisible(seqLenLbl);

    //note duration
    noteDurationBox.setBounds(300, currentHeight, 60, 30);
    noteDurationBox.setLookAndFeel(mainLookAndFeel);
    noteDurationBox.addItemList({ "0.1", "0.2", "0.3", "0.4", "0.5"}, 1);
    noteDurationBox.setSelectedId(1);
    addAndMakeVisible(noteDurationBox);

    noteDurationLbl.setText("Note duration", juce::dontSendNotification);
    noteDurationLbl.attachToComponent(&noteDurationBox, true);
    addAndMakeVisible(noteDurationLbl);

    currentHeight += 60;

    //diversity
    diversitySlid.setBounds(40, currentHeight, rotaryWidth, rotaryHeight);
    InitializeRotarySlider(&diversitySlid, {0, 1}, 0.01, mainLookAndFeel);
    diversitySlid.setValue(0.8);
    addAndMakeVisible(diversitySlid);

    InitializeAttatchedLabel(&diversityLbl, "Diversity", &diversitySlid, false, juce::Justification::centred);
    addAndMakeVisible(diversityLbl);

    //dynamics
    dynamicsSlid.setBounds(140, currentHeight, rotaryWidth, rotaryHeight);
    InitializeRotarySlider(&dynamicsSlid, { 0, 1 }, 0.01, mainLookAndFeel);
    dynamicsSlid.setValue(0.8);
    addAndMakeVisible(dynamicsSlid);

    InitializeAttatchedLabel(&dynamicsLbl, "Dynamics", &dynamicsSlid, false, juce::Justification::centred);
    addAndMakeVisible(dynamicsLbl);

    //arousal
    arousalSlid.setBounds(240, currentHeight, rotaryWidth, rotaryHeight);
    InitializeRotarySlider(&arousalSlid, { 0, 1 }, 0.01, mainLookAndFeel);
    arousalSlid.setValue(0.8);
    addAndMakeVisible(arousalSlid);

    InitializeAttatchedLabel(&arousalLbl, "Arousal", &arousalSlid, false, juce::Justification::centred);
    addAndMakeVisible(arousalLbl);

    currentHeight += 140;

    //valence
    valenceSlid.setBounds(40, currentHeight, rotaryWidth, rotaryHeight);
    InitializeRotarySlider(&valenceSlid, { 0, 1 }, 0.01, mainLookAndFeel);
    valenceSlid.setValue(0.8);
    addAndMakeVisible(valenceSlid);

    InitializeAttatchedLabel(&valenceLbl, "Valence", &valenceSlid, false, juce::Justification::centred);
    addAndMakeVisible(valenceLbl);

    //jazziness
    jazzinessSlid.setBounds(140, currentHeight, rotaryWidth, rotaryHeight);
    InitializeRotarySlider(&jazzinessSlid, { 0, 1 }, 0.01, mainLookAndFeel);
    jazzinessSlid.setValue(0.5);
    addAndMakeVisible(jazzinessSlid);

    InitializeAttatchedLabel(&jazzinessLbl, "Jazziness", &jazzinessSlid, false, juce::Justification::centred);
    addAndMakeVisible(jazzinessLbl);

    //weirdness
    weirdnessSlid.setBounds(240, currentHeight, rotaryWidth, rotaryHeight);
    InitializeRotarySlider(&weirdnessSlid, { 0, 1 }, 0.01, mainLookAndFeel);
    weirdnessSlid.setValue(0.5);
    addAndMakeVisible(weirdnessSlid);

    InitializeAttatchedLabel(&weirdnessLbl, "Weirdness", &weirdnessSlid, false, juce::Justification::centred);
    addAndMakeVisible(weirdnessLbl);

    currentHeight += 110;

    //note range
    noteRangeSlid.setBounds(60, currentHeight, 250, 20);
    noteRangeSlid.setSliderStyle(juce::Slider::TwoValueHorizontal);
    noteRangeSlid.setTextBoxStyle(juce::Slider::NoTextBox, false, 40, speedQualitySlid.getTextBoxHeight());
    noteRangeSlid.setLookAndFeel(mainLookAndFeel);
    noteRangeSlid.setRange(1, 8, 1);
    noteRangeSlid.setMaxValue(5);
    noteRangeSlid.setMinValue(3);
    addAndMakeVisible(noteRangeSlid);

    InitializeAttatchedLabel(&noteRangeLbl, "Note range", &noteRangeSlid, true, juce::Justification::right);
    addAndMakeVisible(noteRangeLbl);

    currentHeight += 50;

    //speed-quality
    speedQualitySlid.setBounds(60, currentHeight, 250, 20);
    speedQualitySlid.setTextBoxStyle(juce::Slider::NoTextBox, false, 30, speedQualitySlid.getTextBoxHeight());
    speedQualitySlid.setLookAndFeel(mainLookAndFeel);
    speedQualitySlid.setRange(0, 2, 1);
    //speedQualitySlid.setNumDecimalPlacesToDisplay
    addAndMakeVisible(speedQualitySlid);

    InitializeAttatchedLabel(&speedLbl, "Speed", &speedQualitySlid, true, juce::Justification::right);
    addAndMakeVisible(speedLbl);

    qualityLbl.setBounds(speedQualitySlid.getX() + speedQualitySlid.getWidth() + 5, currentHeight, 50, 20);
    qualityLbl.setText("Quality", juce::dontSendNotification);
    qualityLbl.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(qualityLbl);

    currentHeight += 30;

    //===main data
    //string
    /*scaleTxt.setBounds(180, 50, 120, 30);
    scaleTxt.setInputRestrictions(15);
    scaleTxt.setText("A Minor");
    addAndMakeVisible(scaleTxt);*/

    //int
    /*minNoteRangeTxt.setBounds(100, 120, 50, 30);
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
    addAndMakeVisible(genNumberTxt);*/


    startGenBtn.setBounds(100, currentHeight, 150, 30);
    startGenBtn.setButtonText("Generate!");
    startGenBtn.addListener(this);
    addAndMakeVisible(&startGenBtn);

    currentHeight += 30;

    debugLabel.setBounds(50, currentHeight, 350, 250);
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
        int speedQualityNO = speedQualitySlid.getValue();//index for values of population size and generation number

        //TODO - set the debugInfo string that will be shown in the window (get it from generator)
        audioProcessor.GenerateMelody(  scaleBox1.getText().toStdString() + " " + scaleBox2.getText().toStdString(),//scale
                                        { noteRangeSlid.getMinValue() , noteRangeSlid.getMaxValue() },//note range
                                        { 1, 4 },//meter
                                        diversitySlid.getValue(),//diversity
                                        dynamicsSlid.getValue(),//dynamics
                                        arousalSlid.getValue(),//arousal
                                        valenceSlid.getValue(),//valence
                                        jazzinessSlid.getValue(),//jazziness
                                        weirdnessSlid.getValue(),//weirdness
                                        noteDurationBox.getText().getDoubleValue(),//note duration
                                        SpeedQualityValues[speedQualityNO].first,//population size
                                        SpeedQualityValues[speedQualityNO].second);//generation number

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
    /*g.drawFittedText("Scale", 50, 50, 50, 30, juce::Justification::centredLeft, 1);

    g.drawFittedText("Note range", 0, 85, getWidth(), 30, juce::Justification::centred, 1);
    g.drawFittedText("Min", 50, 120, 30, 30, juce::Justification::centredLeft, 1);
    g.drawFittedText("Max", 250, 120, 30, 30, juce::Justification::centredLeft, 1);

    g.drawFittedText("Meter", 0, 155, getWidth(), 30, juce::Justification::centred, 1);
    g.drawFittedText("Upper", 50, 190, 40, 30, juce::Justification::centredLeft, 1);
    g.drawFittedText("Lower", 250, 190, 40, 30, juce::Justification::centredLeft, 1);

    g.drawFittedText("Note duration", 50, 225, 100, 30, juce::Justification::centredLeft, 1);

    g.drawFittedText("Population size", 50, 260, 100, 30, juce::Justification::centredLeft, 1);
    
    g.drawFittedText("no generations", 50, 295, 100, 30, juce::Justification::centredLeft, 1);*/

    debugLabel.setText(audioProcessor.debugInfo, juce::NotificationType::dontSendNotification);
}

void GeneticVSTComposerJUCEAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
}
