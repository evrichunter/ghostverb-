#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <vector>

namespace gv
{
/** Storage of ONE capture. A block is never reused: every start() / prepare() creates a new one.
    The audio thread only appends beyond `written`; samples below `written` are immutable, so readers
    that hold a shared_ptr can read [0, written-at-snapshot-time) with no lock and no race. */
struct CaptureBlock
{
    // Plain arrays on purpose: juce::AudioBuffer keeps mutable metadata (isClear) that would be a data race
    // between the audio thread (writer) and snapshot readers.
    CaptureBlock (int numSamples, double rate)
        : left ((size_t) numSamples, 0.0f), right ((size_t) numSamples, 0.0f), capacity (numSamples), sampleRate (rate) {}

    float* writeChannel (int ch) noexcept             { return ch == 0 ? left.data() : right.data(); }
    const float* readChannel (int ch) const noexcept  { return ch == 0 ? left.data() : right.data(); }

    std::vector<float> left, right;
    const int capacity;
    const double sampleRate;
    std::atomic<int> written { 0 };
};

/** Immutable view of a capture at one instant (what the UI draws / copies from). */
struct CaptureSnapshot
{
    std::shared_ptr<const CaptureBlock> block;   // keeps the memory alive
    int numSamples = 0;                          // valid, immutable samples
    int capacity = 0;
    double sampleRate = 0.0;
    int serial = 0;

    const float* channel (int ch) const noexcept { return block->readChannel (ch); }
    explicit operator bool() const noexcept { return block != nullptr; }
};

/** Records the plugin input into a pre-allocated stereo block.
    process() is real-time safe: no allocation, no blocking (try-lock only), the input is never modified.
    All other methods are for the message thread (they take a plain CriticalSection the audio thread never touches). */
class AudioCaptureEngine
{
public:
    enum class State { Idle = 0, Capturing = 1, Ready = 2 };

    static constexpr double maxLimitSeconds = 30.0;

    /** Not required to be exclusive with process(): swaps in a fresh block. Clears the capture if the rate changed. */
    void prepare (double sampleRate);

    /** Audio thread. Mono input is duplicated to both capture channels. */
    void process (const juce::AudioBuffer<float>& input) noexcept;

    bool start();
    void stop();
    void clear();

    State  getState() const noexcept        { return (State) state.load (std::memory_order_acquire); }
    int    getSerial() const noexcept       { return serial.load(); }
    double getSampleRate() const noexcept   { return sampleRate.load(); }
    int    getNumCaptured() const;
    int    getCapacity() const;

    void   setLimitSeconds (double seconds) noexcept;
    double getLimitSeconds() const noexcept { return limitSeconds.load(); }
    int    getLimitSamples() const noexcept;

    /** Immutable snapshot of the current capture (empty if there is none). */
    CaptureSnapshot getSnapshot() const;

    /** Always stereo. Works on a snapshot, so it is safe at any time. */
    juce::AudioBuffer<float> copyRange (int start, int numSamples) const;

private:
    std::shared_ptr<CaptureBlock> makeBlock() const;
    void install (std::shared_ptr<CaptureBlock> fresh, State newState, bool bumpSerial);

    mutable juce::CriticalSection ownerLock;          // message-side only
    std::shared_ptr<CaptureBlock> current;            // owner of the active block (guarded by ownerLock)

    juce::SpinLock audioLock;                         // audio thread only try-locks it
    CaptureBlock* active = nullptr;                   // used by process(), guarded by audioLock

    std::atomic<int> state { (int) State::Idle };
    std::atomic<int> serial { 0 };
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<double> limitSeconds { 20.0 };
};
} // namespace gv
