# Juce folder

This folder contains the JUCE project folder and any other files that may be needed for this part of the project

### About the JUCE project

Source files (located in "Source" folder) can be edited directly in Jucer or in any IDE, however to actually build the plugin (and thus test it) it needs to be open in an external IDE and compiled/built.

There are two main classes:
- PluginProcessor - for the actual audio processing
    -  method "processBlock()" is rensponsible for receiving and sending Audio and MIDI data, which means its the main operation of the plugin
- PluginEditor - for creating the Graphical interface of the plugin
    - it is meant to change the values in PluginProcessor by means of public variables

In simple terms the Audio and MIDI data is sent by reference and by changing those references we change the output of the plugin.

`\#TODO - more more specific instructions`

### Testing VST plugin

Application for testing the audio-plugin is not built by default (its located in the folder where the JUCE is installed).
The simplest way is to open the .juce file in Jucer, open in the specific IDE and compile it.
Its located in: `~/JUCE/extras/AudioPluginHost`.

To add a new plugin to the Tester its built version needs to be in a specific folder to scan. To add folder to scan we choose: "Options->Edit the List..." and then: "Options->Scan for new ... VST ...", then add a new folder to the list and scan it, if the plugin is built it will be automatically added to the Tester.

Plugin can now be added to the graph by using Right mouse button and its GUI can be seen when double-clicking on it in the graph.

`\#TODO - Actual testing of plugin in tester`

Small note: By default the audio input is set to the microphone by default. Just an advice to remember before you create a feedback loop of sounds from speakers into microphone.