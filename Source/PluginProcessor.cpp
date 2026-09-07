#include "PluginProcessor.h"
#include "PluginEditor.h"

TrainerProcessor::TrainerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    fifoStorage.resize ((size_t) fifo.getTotalSize());
}

void TrainerProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    samplePosition = 0;
}

bool TrainerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        || out == juce::AudioChannelSet::mono()
        || out.isDisabled();
}

void TrainerProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();

    for (const auto metadata : midi)
    {
        const auto m = metadata.getMessage();

        if (! (m.isNoteOn() || m.isNoteOff()))
            continue;

        MidiNoteEvent ev;
        ev.note     = m.getNoteNumber();
        ev.velocity = m.getVelocity();
        ev.channel  = m.getChannel();
        ev.on       = m.isNoteOn();
        ev.timeMs   = (double) (samplePosition + metadata.samplePosition) * 1000.0 / currentSampleRate;

        const auto scope = fifo.write (1);

        if (scope.blockSize1 > 0)
            fifoStorage[(size_t) scope.startIndex1] = ev;
        // If the FIFO is full the event is dropped; the editor drains it at 60 Hz so this never happens in practice.
    }

    samplePosition += audio.getNumSamples();
    // `midi` is left untouched: the host receives the same notes as this plugin's MIDI output.
}

int TrainerProcessor::popEvents (std::vector<MidiNoteEvent>& out)
{
    out.clear();
    const auto ready = fifo.getNumReady();

    if (ready <= 0)
        return 0;

    const auto scope = fifo.read (ready);

    for (int i = 0; i < scope.blockSize1; ++i)
        out.push_back (fifoStorage[(size_t) (scope.startIndex1 + i)]);

    for (int i = 0; i < scope.blockSize2; ++i)
        out.push_back (fifoStorage[(size_t) (scope.startIndex2 + i)]);

    return (int) out.size();
}

void TrainerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree t ("MusicLearnTrainer");
    t.setProperty ("w", editorWidth.load(), nullptr);
    t.setProperty ("h", editorHeight.load(), nullptr);
    juce::MemoryOutputStream mos (destData, false);
    t.writeToStream (mos);
}

void TrainerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto t = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);

    if (t.isValid() && t.hasType ("MusicLearnTrainer"))
    {
        editorWidth  = (int) t.getProperty ("w", 1100);
        editorHeight = (int) t.getProperty ("h", 760);
    }
}

juce::AudioProcessorEditor* TrainerProcessor::createEditor()
{
    return new TrainerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TrainerProcessor();
}
