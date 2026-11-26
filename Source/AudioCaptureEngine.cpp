#include "AudioCaptureEngine.h"

namespace gv
{
std::shared_ptr<CaptureBlock> AudioCaptureEngine::makeBlock() const
{
    const double sr = sampleRate.load();
    if (sr <= 0.0) return nullptr;
    const int n = (int) std::ceil (limitSeconds.load() * sr);
    return std::make_shared<CaptureBlock> (juce::jmax (1, n), sr);     // allocates here, on the message thread
}

void AudioCaptureEngine::install (std::shared_ptr<CaptureBlock> fresh, State newState, bool bumpSerial)
{
    std::shared_ptr<CaptureBlock> old;
    {
        const juce::ScopedLock owner (ownerLock);
        {
            juce::SpinLock::ScopedLockType sl (audioLock);   // waits at most for one process() block
            active = fresh.get();
            if (bumpSerial) ++serial;
            state.store ((int) newState, std::memory_order_release);
        }
        old = std::move (current);
        current = std::move (fresh);
    }
    // `old` is released here. Snapshots held by the UI keep it alive; the audio thread no longer points at it.
}

void AudioCaptureEngine::prepare (double sr)
{
    if (sr <= 0.0) return;
    if (sr == sampleRate.load() && getCapacity() > 0) return;

    sampleRate.store (sr);
    install (makeBlock(), State::Idle, true);
}

void AudioCaptureEngine::setLimitSeconds (double seconds) noexcept
{
    limitSeconds.store (juce::jlimit (1.0, maxLimitSeconds, seconds));     // applies to the next capture
}

int AudioCaptureEngine::getLimitSamples() const noexcept
{
    return (int) std::ceil (limitSeconds.load() * sampleRate.load());
}

bool AudioCaptureEngine::start()
{
    auto fresh = makeBlock();
    if (fresh == nullptr) return false;
    install (std::move (fresh), State::Capturing, true);
    return true;
}

void AudioCaptureEngine::stop()
{
    const juce::ScopedLock owner (ownerLock);
    juce::SpinLock::ScopedLockType sl (audioLock);
    if (getState() == State::Capturing)
        state.store ((int) (current != nullptr && current->written.load() > 0 ? State::Ready : State::Idle), std::memory_order_release);
}

void AudioCaptureEngine::clear()
{
    install (makeBlock(), State::Idle, true);
}

int AudioCaptureEngine::getNumCaptured() const
{
    const juce::ScopedLock owner (ownerLock);
    return current != nullptr ? current->written.load (std::memory_order_acquire) : 0;
}

int AudioCaptureEngine::getCapacity() const
{
    const juce::ScopedLock owner (ownerLock);
    return current != nullptr ? current->capacity : 0;
}

CaptureSnapshot AudioCaptureEngine::getSnapshot() const
{
    CaptureSnapshot s;
    const juce::ScopedLock owner (ownerLock);
    if (current == nullptr) return s;
    s.block = current;
    s.numSamples = current->written.load (std::memory_order_acquire);
    s.capacity = current->capacity;
    s.sampleRate = current->sampleRate;
    s.serial = serial.load();
    return s;
}

void AudioCaptureEngine::process (const juce::AudioBuffer<float>& input) noexcept
{
    if (state.load (std::memory_order_acquire) != (int) State::Capturing) return;

    juce::SpinLock::ScopedTryLockType sl (audioLock);
    if (! sl.isLocked() || active == nullptr || state.load (std::memory_order_relaxed) != (int) State::Capturing) return;

    const int numIn = input.getNumChannels();
    if (numIn <= 0) return;

    const int pos = active->written.load (std::memory_order_relaxed);
    const int n = juce::jmin (input.getNumSamples(), active->capacity - pos);

    if (n > 0)
    {
        juce::FloatVectorOperations::copy (active->writeChannel (0) + pos, input.getReadPointer (0), n);
        juce::FloatVectorOperations::copy (active->writeChannel (1) + pos, input.getReadPointer (numIn > 1 ? 1 : 0), n);
    }

    const int newPos = pos + juce::jmax (0, n);
    active->written.store (newPos, std::memory_order_release);       // publishes [pos, newPos) as immutable
    if (newPos >= active->capacity)                                   // capture limit reached: stop automatically
        state.store ((int) State::Ready, std::memory_order_release);
}

juce::AudioBuffer<float> AudioCaptureEngine::copyRange (int start, int numSamples) const
{
    const auto snap = getSnapshot();
    if (! snap) return juce::AudioBuffer<float> (2, 0);

    start = juce::jlimit (0, snap.numSamples, start);
    numSamples = juce::jlimit (0, snap.numSamples - start, numSamples);

    juce::AudioBuffer<float> out (2, numSamples);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::copy (out.getWritePointer (ch), snap.channel (ch) + start, numSamples);
    return out;
}
} // namespace gv
