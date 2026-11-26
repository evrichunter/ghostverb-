#pragma once

#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

namespace gv
{
class GhostVerbAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit GhostVerbAudioProcessorEditor (GhostVerbAudioProcessor&);
    ~GhostVerbAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth = 560, designHeight = 440;

private:
    class Content;
    std::unique_ptr<Content> content;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GhostVerbAudioProcessorEditor)
};
} // namespace gv
