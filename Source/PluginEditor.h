#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

/**
    The editor is a WebView2 panel showing ui/index.html (embedded as binary data).
    MIDI events are pushed to the page as a "midi" event; the page sends "ui" events back.
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

    int savedWidth = 1100, savedHeight = 760;

    static juce::WebBrowserComponent::Options makeOptions (TrainerEditor& self);
    static std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    TrainerProcessor& processor;
    juce::WebBrowserComponent web;
    std::vector<MidiNoteEvent> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrainerEditor)
};
