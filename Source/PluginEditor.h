#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

/**
    The editor is a WebView2 panel showing ui/index.html (embedded as binary data).
    MIDI events are pushed to the page as a "midi" event and the host transport as a "transport"
    event (60 Hz); the page sends "ui" events back.

    Live UI: when %LOCALAPPDATA%\MusicLearnTrainer\ui-dir.txt names a folder holding index.html
    (live-ui.cmd writes it), the page is served from that folder instead of the embedded copy and
    reloads itself when index.html changes, so UI edits need no rebuild, install or re-added device.
*/
class TrainerEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit TrainerEditor (TrainerProcessor&);
    ~TrainerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void handleUiEvent (const juce::var& payload);
    void setMaximised (bool shouldBeMaximised);
    void fillDisplay();
    void checkLiveUi();

    int savedWidth = 1100, savedHeight = 760;

    static juce::WebBrowserComponent::Options makeOptions (TrainerEditor& self);
    static std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url, const juce::File& uiDir);

    TrainerProcessor& processor;
    const juce::File uiDir;                  // live UI folder, or empty; declared before `web`, which reads it
    juce::Time uiSeen;                       // index.html's modification time when last (re)loaded
    bool uiChanged = false;
    int uiCheckTicks = 0;
    juce::WebBrowserComponent web;
    std::vector<MidiNoteEvent> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrainerEditor)
};
