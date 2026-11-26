#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>

namespace gv
{
/** Plays a shared, immutable stereo buffer into the plugin output (added on top of the host signal).
    The audio thread only ever try-locks; the buffer is owned and released by the message thread. */
class PreviewPlayer
{
public:
    void prepare (double sampleRate) noexcept;
    void play (std::shared_ptr<const juce::AudioBuffer<float>> audio);   // message thread
    void stop() noexcept;                                                 // message thread (fades out)
    bool isPlaying() const noexcept        { return playing.load (std::memory_order_acquire); }
    float getPosition() const noexcept;                                   // 0..1

    /** Audio thread. Adds the preview to `out` (1 or 2 channels). */
    void mixInto (juce::AudioBuffer<float>& out) noexcept;

private:
    juce::SpinLock lock;
    std::shared_ptr<const juce::AudioBuffer<float>> held;
    const juce::AudioBuffer<float>* raw = nullptr;
    int pos = 0;
    float gain = 0.0f;
    float fadeStep = 1.0f / 96.0f;
    std::atomic<bool> playing { false }, stopRequested { false };
    std::atomic<int> posAtomic { 0 }, totalAtomic { 0 };
};
} // namespace gv
