#pragma once

#include "AudioCaptureEngine.h"
#include "AudioRenderer.h"
#include "Params.h"
#include "PreviewPlayer.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace gv
{
class GhostVerbAudioProcessor : public juce::AudioProcessor
{
public:
    enum class PreviewKind { None = 0, Selection = 1, Result = 2 };

    struct HostInfo { double bpm; int numerator; int denominator; bool tempoFromHost; };

    GhostVerbAudioProcessor();
    ~GhostVerbAudioProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool supportsDoublePrecisionProcessing() const override { return false; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "GHOST VERB"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Host tempo / time signature (written in processBlock, read by the UI; lock-free)
    HostInfo getHostInfo() const noexcept;

    // Selection (sample indices into the capture; start = attack reference)
    void setSelection (int startSample, int endSample) noexcept;
    void getSelection (int& startSample, int& endSample) const noexcept;
    void ensureSelection();   // sets a sensible default whenever a new capture exists

    // Message-thread actions
    bool startCapture();
    void stopCapture();
    juce::String generate();                    // empty string = started OK, otherwise the error to show
    bool previewSelection();
    bool previewResult();
    void stopPreview();
    PreviewKind getPreviewKind() const noexcept;
    float getPreviewPosition() const noexcept   { return preview.getPosition(); }
    void setCaptureLimitSeconds (double s);

    juce::File makeTempFile (int lengthIndex, double bpm);

    juce::AudioProcessorValueTreeState apvts;
    AudioCaptureEngine capture;
    AudioRenderer renderer;

private:
    PreviewPlayer preview;
    std::atomic<double> hostBpm { kFallbackBpm };
    std::atomic<int> hostNum { kFallbackNumerator }, hostDen { kFallbackDenominator };
    std::atomic<bool> hostTempoKnown { false };
    std::atomic<int> selStart { 0 }, selEnd { 0 }, selSerial { -1 };
    std::atomic<int> previewKind { 0 };
    std::atomic<int> tempCounter { 0 };
    double currentSampleRate = 0.0;
    juce::File sessionDir;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GhostVerbAudioProcessor)
};
} // namespace gv
