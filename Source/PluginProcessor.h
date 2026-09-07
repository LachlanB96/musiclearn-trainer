#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>

/** One note-on / note-off, stamped with the audio clock (sample accurate). */
struct MidiNoteEvent
{
    int note = 0;
    int velocity = 0;
    int channel = 1;
    bool on = false;
    double timeMs = 0.0;
};

/**
    The processor does exactly two things: it forwards every note event to the editor
    through a lock-free FIFO, and it passes the MIDI through untouched so the host can
    route it on to a real instrument. Audio output is silence.
*/
class TrainerProcessor final : public juce::AudioProcessor
{
public:
    TrainerProcessor();
    ~TrainerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Drains queued note events into `out` (message thread). Returns how many were read. */
    int popEvents (std::vector<MidiNoteEvent>& out);

    std::atomic<int> editorWidth { 1100 };
    std::atomic<int> editorHeight { 760 };

private:
    juce::AbstractFifo fifo { 2048 };
    std::vector<MidiNoteEvent> fifoStorage;
    double currentSampleRate = 44100.0;
    juce::int64 samplePosition = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrainerProcessor)
};
