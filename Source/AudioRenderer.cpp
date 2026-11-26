#include "AudioRenderer.h"

namespace gv
{
AudioRenderer::AudioRenderer() : juce::Thread ("GhostVerb render")
{
    startThread (juce::Thread::Priority::background);
}

AudioRenderer::~AudioRenderer()
{
    shutdown();
}

void AudioRenderer::shutdown()
{
    cancelCurrent.store (true);
    signalThreadShouldExit();
    notify();
    stopThread (10000);
}

void AudioRenderer::submit (Job&& job)
{
    {
        const juce::ScopedLock sl (mutex);
        pending = std::move (job);
        cancelCurrent.store (true);
        status.store ((int) Status::Rendering);
        error.clear();
    }
    notify();
}

juce::String AudioRenderer::getError() const
{
    const juce::ScopedLock sl (mutex);
    return error;
}

std::shared_ptr<const AudioRenderer::Output> AudioRenderer::getOutput() const
{
    const juce::ScopedLock sl (mutex);
    return output;
}

void AudioRenderer::clearOutput()
{
    const juce::ScopedLock sl (mutex);
    output.reset();
    if (! pending.has_value() && status.load() != (int) Status::Rendering)
        status.store ((int) Status::Idle);
}

void AudioRenderer::run()
{
    while (! threadShouldExit())
    {
        std::optional<Job> job;
        {
            const juce::ScopedLock sl (mutex);
            job = std::move (pending);
            pending.reset();
            cancelCurrent.store (false);
        }

        if (! job.has_value()) { wait (-1); continue; }
        execute (*job);
    }
}

void AudioRenderer::execute (Job& job)
{
    auto abort = [this] { return cancelCurrent.load() || threadShouldExit(); };

    auto fail = [this] (const juce::String& msg)
    {
        const juce::ScopedLock sl (mutex);
        if (pending.has_value()) return;                  // a newer job is already queued
        error = msg;
        status.store ((int) Status::Failed);
    };

    if (job.lengthSamples <= 0 || job.selection.getNumSamples() <= 0)
        return fail ("Nothing to render");

    if ((double) job.lengthSamples > ReverseReverbEngine::maxSwellSeconds * job.sampleRate)
        return fail ("LENGTH is too long at this tempo");

    ReverseReverbEngine::FullRender full;
    bool ok = false;
    try
    {
        ok = ReverseReverbEngine::renderFull (job.selection, job.sampleRate, job.settings,
                                              job.lengthSamples, full, abort);
    }
    catch (const std::exception& e)
    {
        return fail (juce::String ("Render failed: ") + e.what());
    }

    if (! ok)
    {
        if (abort()) return;                              // superseded: the next job takes over
        return fail ("Render failed");
    }

    auto out = std::make_shared<Output>();
    out->audio = ReverseReverbEngine::extractSwell (full, job.lengthSamples, job.sampleRate, job.settings.outputDb);
    out->sampleRate = job.sampleRate;
    out->bpm = job.bpm;
    out->numerator = job.numerator;
    out->denominator = job.denominator;

    if (job.outputFile != juce::File())
    {
        juce::String err;
        if (writeWav24 (out->audio, job.sampleRate, job.outputFile, err))
        {
            out->file = job.outputFile;
            writtenFiles.add (job.outputFile);
            while (writtenFiles.size() > 8)               // keep the latest few, drop older temp files
            {
                writtenFiles.getReference (0).deleteFile();
                writtenFiles.remove (0);
            }
        }
        else
            out->fileError = err;
    }

    const juce::ScopedLock sl (mutex);
    if (cancelCurrent.load() || pending.has_value()) return;     // superseded while writing
    output = std::move (out);
    status.store ((int) Status::Ready);
}

bool AudioRenderer::writeWav24 (const juce::AudioBuffer<float>& audio, double sampleRate,
                                const juce::File& destination, juce::String& error)
{
    if (audio.getNumSamples() <= 0 || audio.getNumChannels() < 1)
    {
        error = "Nothing to export";
        return false;
    }

    if (! destination.getParentDirectory().createDirectory())
    {
        error = "Cannot create folder: " + destination.getParentDirectory().getFullPathName();
        return false;
    }

    destination.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (destination.createOutputStream());
    if (stream == nullptr || stream->failedToOpen())
    {
        error = "Cannot write file: " + destination.getFullPathName();
        return false;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate, 2u, 24, juce::StringPairArray(), 0));
    if (writer == nullptr)
    {
        error = "Cannot create WAV writer";
        return false;
    }
    stream.release();      // owned by the writer now

    juce::AudioBuffer<float> stereo (2, audio.getNumSamples());
    stereo.copyFrom (0, 0, audio, 0, 0, audio.getNumSamples());
    stereo.copyFrom (1, 0, audio, audio.getNumChannels() > 1 ? 1 : 0, 0, audio.getNumSamples());

    if (! writer->writeFromAudioSampleBuffer (stereo, 0, stereo.getNumSamples()))
    {
        error = "Error while writing WAV data";
        return false;
    }

    writer.reset();        // flush + close
    if (! destination.existsAsFile() || destination.getSize() <= 44)
    {
        error = "WAV file is empty";
        return false;
    }
    return true;
}
} // namespace gv
