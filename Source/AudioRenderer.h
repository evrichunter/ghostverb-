#pragma once

#include "ReverseReverbEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <optional>

namespace gv
{
/** Runs the reverse-reverb render on its own background thread (never the audio thread, never blocking the host),
    then writes the 24-bit stereo WAV used for drag & drop. */
class AudioRenderer : private juce::Thread
{
public:
    enum class Status { Idle = 0, Rendering = 1, Ready = 2, Failed = 3 };

    struct Job
    {
        juce::AudioBuffer<float> selection;
        double sampleRate = 44100.0;
        ReverseReverbEngine::Settings settings;
        int lengthSamples = 0;
        juce::File outputFile;                 // temp WAV for drag & drop (may be empty -> no file)
        double bpm = 120.0;
        int numerator = 4, denominator = 4;
    };

    struct Output
    {
        juce::AudioBuffer<float> audio;        // stereo, exactly lengthSamples, wet only
        double sampleRate = 44100.0;
        juce::File file;                       // empty if the temp file could not be written
        juce::String fileError;
        double bpm = 120.0;
        int numerator = 4, denominator = 4;
    };

    AudioRenderer();
    ~AudioRenderer() override;

    void submit (Job&& job);                   // any non-audio thread; supersedes running/pending jobs
    void shutdown();                           // cancels the render and stops the thread (idempotent)
    Status getStatus() const noexcept          { return (Status) status.load(); }
    juce::String getError() const;
    std::shared_ptr<const Output> getOutput() const;
    void clearOutput();

    /** Stereo 24-bit WAV. Returns false and fills `error` on failure. */
    static bool writeWav24 (const juce::AudioBuffer<float>& audio, double sampleRate,
                            const juce::File& destination, juce::String& error);

private:
    void run() override;
    void execute (Job&);

    mutable juce::CriticalSection mutex;
    std::optional<Job> pending;
    std::shared_ptr<const Output> output;
    juce::String error;
    std::atomic<int> status { (int) Status::Idle };
    std::atomic<bool> cancelCurrent { false };
    juce::Array<juce::File> writtenFiles;
};
} // namespace gv
