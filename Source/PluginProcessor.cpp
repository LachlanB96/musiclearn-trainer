#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

TrainerProcessor::TrainerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    fifoStorage.resize ((size_t) fifo.getTotalSize());
    guideStorage.resize ((size_t) guideFifo.getTotalSize());
    guidePending.reserve (2048);
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

    {
        TransportInfo info;
        info.timeMs = (double) samplePosition * 1000.0 / currentSampleRate;

        if (auto* head = getPlayHead())
        {
            if (const auto pos = head->getPosition())
            {
                info.hasHost = true;
                info.playing = pos->getIsPlaying();
                if (const auto bpm = pos->getBpm())          info.bpm = *bpm;
                if (const auto ppq = pos->getPpqPosition())  info.ppq = *ppq;
                if (const auto ts  = pos->getTimeSignature()) { info.numerator = ts->numerator; info.denominator = ts->denominator; }
            }
        }

        const juce::SpinLock::ScopedTryLockType lock (transportLock);

        if (lock.isLocked())
            transport = info;
        // If the editor is reading right now this block's snapshot is skipped; the next block refreshes it.
    }

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

    // Incoming notes are left untouched: the host receives the same notes as this plugin's MIDI
    // output. Guide notes are merged in below, on their own channel, at their scheduled sample.
    const auto numSamples = audio.getNumSamples();
    const double blockStartMs = (double) samplePosition * 1000.0 / currentSampleRate;
    const double blockEndMs   = (double) (samplePosition + numSamples) * 1000.0 / currentSampleRate;

    if (guideCancel.exchange (false))
    {
        guidePending.clear();
        const auto scope = guideFifo.read (guideFifo.getNumReady());   // drop anything queued
        juce::ignoreUnused (scope);

        for (int ch = 0; ch < 16; ++ch)
            for (int n = 0; n < 128; ++n)
                if (guideSounding[ch][n])
                {
                    midi.addEvent (juce::MidiMessage::noteOff (ch + 1, n), 0);
                    guideSounding[ch][n] = false;
                }
    }

    {
        const auto scope = guideFifo.read (guideFifo.getNumReady());

        for (int i = 0; i < scope.blockSize1; ++i) if (guidePending.size() < guidePending.capacity()) guidePending.push_back (guideStorage[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) if (guidePending.size() < guidePending.capacity()) guidePending.push_back (guideStorage[(size_t) (scope.startIndex2 + i)]);
    }

    if (! guidePending.empty())
    {
        auto due = std::remove_if (guidePending.begin(), guidePending.end(), [&] (const ScheduledNote& n)
        {
            if (n.timeMs >= blockEndMs)
                return false;   // not yet

            const int offset = juce::jlimit (0, numSamples - 1, (int) std::floor ((n.timeMs - blockStartMs) * currentSampleRate / 1000.0));
            const int ch = juce::jlimit (1, 16, n.channel), note = juce::jlimit (0, 127, n.note);

            if (n.on)
            {
                midi.addEvent (juce::MidiMessage::noteOn (ch, note, (juce::uint8) juce::jlimit (1, 127, n.velocity)), offset);
                guideSounding[ch - 1][note] = true;
            }
            else
            {
                midi.addEvent (juce::MidiMessage::noteOff (ch, note), offset);
                guideSounding[ch - 1][note] = false;
            }

            return true;
        });

        guidePending.erase (due, guidePending.end());
    }

    samplePosition += numSamples;
}

void TrainerProcessor::scheduleNotes (const std::vector<ScheduledNote>& notes)
{
    for (const auto& n : notes)
    {
        const auto scope = guideFifo.write (1);

        if (scope.blockSize1 > 0)
            guideStorage[(size_t) scope.startIndex1] = n;
        else if (scope.blockSize2 > 0)
            guideStorage[(size_t) scope.startIndex2] = n;
        // A full FIFO drops the note; the page schedules a few hundred ms ahead, so this needs a stalled audio engine.
    }
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

TransportInfo TrainerProcessor::getTransport() const
{
    const juce::SpinLock::ScopedLockType lock (transportLock);
    return transport;
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
