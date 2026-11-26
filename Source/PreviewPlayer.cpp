#include "PreviewPlayer.h"

namespace gv
{
void PreviewPlayer::prepare (double sr) noexcept
{
    fadeStep = (float) (1.0 / juce::jmax (8.0, 0.002 * sr));
}

void PreviewPlayer::play (std::shared_ptr<const juce::AudioBuffer<float>> audio)
{
    std::shared_ptr<const juce::AudioBuffer<float>> old;
    {
        juce::SpinLock::ScopedLockType sl (lock);
        old = std::move (held);
        held = std::move (audio);
        raw = held.get();
        pos = 0;
        gain = 0.0f;
        stopRequested.store (false);
        totalAtomic.store (raw != nullptr ? raw->getNumSamples() : 0);
        posAtomic.store (0);
        playing.store (raw != nullptr && raw->getNumSamples() > 0, std::memory_order_release);
    }
}   // `old` is released here, outside the lock and away from the audio thread

void PreviewPlayer::stop() noexcept
{
    stopRequested.store (true);
}

float PreviewPlayer::getPosition() const noexcept
{
    const int total = totalAtomic.load();
    return total > 0 ? (float) posAtomic.load() / (float) total : 0.0f;
}

void PreviewPlayer::mixInto (juce::AudioBuffer<float>& out) noexcept
{
    if (! playing.load (std::memory_order_acquire)) return;

    juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked() || raw == nullptr) return;

    const int total = raw->getNumSamples();
    const int n = out.getNumSamples();
    const int numOut = out.getNumChannels();
    const float* srcL = raw->getReadPointer (0);
    const float* srcR = raw->getReadPointer (raw->getNumChannels() > 1 ? 1 : 0);
    const int fadeLen = (int) std::round (1.0f / fadeStep);

    for (int i = 0; i < n; ++i)
    {
        if (pos >= total) { playing.store (false, std::memory_order_release); break; }

        const float target = stopRequested.load (std::memory_order_relaxed) ? 0.0f : 1.0f;
        gain += juce::jlimit (-fadeStep, fadeStep, target - gain);
        if (target == 0.0f && gain <= 0.0f) { playing.store (false, std::memory_order_release); break; }

        const int remaining = total - pos;
        const float tailFade = remaining < fadeLen ? (float) remaining / (float) fadeLen : 1.0f;
        const float g = gain * tailFade;

        if (numOut >= 2)
        {
            out.getWritePointer (0)[i] += srcL[pos] * g;
            out.getWritePointer (1)[i] += srcR[pos] * g;
        }
        else if (numOut == 1)
            out.getWritePointer (0)[i] += 0.5f * (srcL[pos] + srcR[pos]) * g;

        ++pos;
    }

    posAtomic.store (pos);
}
} // namespace gv
