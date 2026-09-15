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

/** Host transport as seen at the start of the most recent audio block. */
struct TransportInfo
{
    double timeMs = 0.0;     // audio clock at the start of the block (same clock as MidiNoteEvent::timeMs)
    double ppq = 0.0;        // host position in quarter notes at that instant
    double bpm = 0.0;
    int numerator = 4, denominator = 4;
    bool playing = false;
    bool hasHost = false;    // false in the Standalone build or when the host gives no play head
};

/** A note the page wants played at a moment on the audio clock (the "guide" part in Songs mode). */
struct ScheduledNote
{
    int note = 0;
    int velocity = 0;
    int channel = 2;
    bool on = false;
    double timeMs = 0.0;
};

/**
    The processor forwards every incoming note event to the editor through a lock-free FIFO,
    passes the MIDI through untouched so the host can route it on to a real instrument, and
    merges in guide notes scheduled by the page (on their own MIDI channel, sample-accurate)
    so the same instrument can sound the part being practised. Audio output is silence.
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

    /** Latest host transport snapshot (message thread). */
    TransportInfo getTransport() const;

    /** Queue guide notes for output at their audio-clock time (message thread). */
    void scheduleNotes (const std::vector<ScheduledNote>& notes);
    /** Silence every sounding guide note and drop what is still queued (message thread). */
    void cancelGuide() { guideCancel = true; }

    std::atomic<int> editorWidth { 1100 };
    std::atomic<int> editorHeight { 760 };

private:
    juce::AbstractFifo fifo { 2048 };
    std::vector<MidiNoteEvent> fifoStorage;
    double currentSampleRate = 44100.0;
    juce::int64 samplePosition = 0;

    // Written by the audio thread under a try-lock (never blocks), read by the editor timer.
    mutable juce::SpinLock transportLock;
    TransportInfo transport;

    // Guide notes: message thread -> FIFO -> audio thread, which holds them until their time comes.
    juce::AbstractFifo guideFifo { 1024 };
    std::vector<ScheduledNote> guideStorage;
    std::vector<ScheduledNote> guidePending;      // audio thread only, preallocated
    std::atomic<bool> guideCancel { false };
    bool guideSounding[16][128] = {};             // audio thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrainerProcessor)
};
