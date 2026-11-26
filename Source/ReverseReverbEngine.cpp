#include "ReverseReverbEngine.h"
#include <cmath>

namespace gv
{
namespace
{
    constexpr std::array<double, FdnReverb::numLines> baseDelaysMs { 31.17, 37.31, 41.93, 47.77, 53.51, 59.39, 67.13, 73.87 };
    constexpr std::array<int, 3> apTimesL { 113, 173, 251 };   // at 48 kHz
    constexpr std::array<int, 3> apTimesR { 127, 191, 263 };

    int nextPow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }
}

double FdnReverb::rt60ForSize (float size) noexcept
{
    return 0.4 * std::pow (20.0, (double) juce::jlimit (0.0f, 1.0f, size));   // 0.4 s .. 8 s
}

void FdnReverb::prepare (double sr, float size, float tone)
{
    size = juce::jlimit (0.0f, 1.0f, size);
    tone = juce::jlimit (0.0f, 1.0f, tone);

    rt60 = rt60ForSize (size);
    const double scale = 0.5 + 1.5 * (double) size;
    const double fc = juce::jmin (800.0 * std::pow (22.0, (double) tone), sr * 0.45);   // loop damping 0.8 .. 17.6 kHz
    dampCoeff = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * fc / sr);

    const float modDepth = (float) (0.0003 * sr);
    float minDelay = 1.0e9f;

    for (int i = 0; i < numLines; ++i)
    {
        auto& l = lines[(size_t) i];
        const double d = std::round (baseDelaysMs[(size_t) i] * 0.001 * sr * scale);
        l.baseDelay = (float) d;
        l.modDepth = modDepth;
        l.buffer.assign ((size_t) nextPow2 ((int) d + (int) modDepth + 8), 0.0f);
        l.mask = (int) l.buffer.size() - 1;
        l.writePos = 0;
        l.phase = 0.37 * i;
        l.phaseInc = 2.0 * juce::MathConstants<double>::pi * (0.11 + 0.043 * i) / sr;
        l.damp = 0.0f;
        l.gain = (float) std::pow (10.0, -3.0 * d / (rt60 * sr));
        minDelay = juce::jmin (minDelay, l.baseDelay - l.modDepth);
    }

    for (size_t k = 0; k < apL.size(); ++k)
    {
        apL[k].buffer.assign ((size_t) juce::jmax (8, (int) std::round (apTimesL[k] * sr / 48000.0)), 0.0f);
        apR[k].buffer.assign ((size_t) juce::jmax (8, (int) std::round (apTimesR[k] * sr / 48000.0)), 0.0f);
        apL[k].pos = apR[k].pos = 0;
    }

    injectGain = 0.5f;
    outScale = 0.5f;
    // Output = interpolated read of delay lines only (<= minDelay - modDepth - 1 samples are untouched).
    silentSamples = juce::jmax (0, (int) std::floor (minDelay) - 2);
}

void FdnReverb::reset()
{
    for (auto& l : lines) { std::fill (l.buffer.begin(), l.buffer.end(), 0.0f); l.writePos = 0; l.damp = 0.0f; }
    for (auto& a : apL) { std::fill (a.buffer.begin(), a.buffer.end(), 0.0f); a.pos = 0; }
    for (auto& a : apR) { std::fill (a.buffer.begin(), a.buffer.end(), 0.0f); a.pos = 0; }
}

void FdnReverb::process (float inL, float inR, float& outL, float& outR) noexcept
{
    float a = inL, b = inR;
    for (size_t k = 0; k < apL.size(); ++k) { a = apL[k].process (a); b = apR[k].process (b); }

    float y[numLines];
    for (int i = 0; i < numLines; ++i)
    {
        auto& l = lines[(size_t) i];
        const float dl = l.baseDelay + l.modDepth * (float) std::sin (l.phase);
        l.phase += l.phaseInc;
        const int di = (int) dl;
        const float frac = dl - (float) di;
        const int i0 = (l.writePos - di) & l.mask;
        const int i1 = (i0 - 1) & l.mask;
        y[i] = l.buffer[(size_t) i0] * (1.0f - frac) + l.buffer[(size_t) i1] * frac;
    }

    outL = outScale * (y[0] - y[1] + y[2] - y[3] + y[4] - y[5] + y[6] - y[7]);
    outR = outScale * (y[0] + y[1] - y[2] - y[3] + y[4] + y[5] - y[6] - y[7]);

    float f[numLines];
    for (int i = 0; i < numLines; ++i)
    {
        auto& l = lines[(size_t) i];
        l.damp = (1.0f - dampCoeff) * y[i] + dampCoeff * l.damp;
        f[i] = l.damp * l.gain;
    }

    for (int h = 1; h < numLines; h <<= 1)
        for (int i = 0; i < numLines; i += h << 1)
            for (int j = i; j < i + h; ++j)
            {
                const float p = f[j], q = f[j + h];
                f[j] = p + q;
                f[j + h] = p - q;
            }

    for (int i = 0; i < numLines; ++i)
    {
        auto& l = lines[(size_t) i];
        l.buffer[(size_t) l.writePos] = f[i] * 0.35355339f + ((i & 1) ? b : a) * injectGain;
        l.writePos = (l.writePos + 1) & l.mask;
    }
}

//==============================================================================
int ReverseReverbEngine::tailSamplesFor (const Settings& s, double sr, int minTail)
{
    const double decay = FdnReverb::rt60ForSize (s.size) * 1.25;     // a bit beyond -60 dB: complete tail
    const int wanted = (int) std::ceil (decay * sr);
    return juce::jmax (minTail, juce::jmin (wanted, (int) (40.0 * sr)));
}

bool ReverseReverbEngine::renderFull (const juce::AudioBuffer<float>& selection, double sr, const Settings& s,
                                      int minTailSamples, FullRender& result,
                                      const std::function<bool()>& shouldAbort)
{
    const int F = selection.getNumSamples();
    if (F <= 0 || selection.getNumChannels() <= 0 || sr <= 0.0) return false;

    FdnReverb reverb;
    reverb.prepare (sr, s.size, s.tone);
    result.rt60 = reverb.rt60Seconds();
    result.onsetSamples = reverb.guaranteedSilentSamples();

    const int T = tailSamplesFor (s, sr, minTailSamples + result.onsetSamples);
    const int total = F + T;

    // 1-2. extract + reverse the selected fragment
    juce::AudioBuffer<float> rev (2, F);
    rev.copyFrom (0, 0, selection, 0, 0, F);
    rev.copyFrom (1, 0, selection, selection.getNumChannels() > 1 ? 1 : 0, 0, F);
    rev.reverse (0, F);

    // 3-4. 100 % wet algorithmic reverb, rendering the complete tail
    juce::AudioBuffer<float> wet (2, total);
    wet.clear();

    const float* r0 = rev.getReadPointer (0);
    const float* r1 = rev.getReadPointer (1);
    float* w0 = wet.getWritePointer (0);
    float* w1 = wet.getWritePointer (1);

    for (int i = 0; i < total; ++i)
    {
        if ((i & 4095) == 0 && shouldAbort && shouldAbort()) return false;
        const float xl = i < F ? r0[i] : 0.0f;
        const float xr = i < F ? r1[i] : 0.0f;
        reverb.process (xl, xr, w0[i], w1[i]);
    }

    // stereo width (mid/side)
    const float width = juce::jlimit (0.0f, 2.0f, s.width);
    if (width != 1.0f)
        for (int i = 0; i < total; ++i)
        {
            const float m = 0.5f * (w0[i] + w1[i]);
            const float sd = 0.5f * (w0[i] - w1[i]) * width;
            w0[i] = m + sd;
            w1[i] = m - sd;
        }

    // 5. reverse ALL the processed audio
    wet.reverse (0, total);
    result.audio = std::move (wet);
    result.attackIndex = T;       // reversed(selection start) ends where the tail starts: that is the attack
    return true;
}

juce::AudioBuffer<float> ReverseReverbEngine::extractSwell (const FullRender& full, int L, double sr, float outputDb)
{
    juce::AudioBuffer<float> out (2, juce::jmax (0, L));
    out.clear();
    if (L <= 0 || full.audio.getNumSamples() == 0) return out;

    const int src = full.attackIndex - full.onsetSamples - L;   // first source sample (negative -> leading silence)
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < L; ++i)
        {
            const int k = src + i;
            if (k >= 0 && k < full.audio.getNumSamples())
                out.setSample (ch, i, full.audio.getSample (ch, k));
        }

    out.applyGain (juce::Decibels::decibelsToGain (outputDb));

    // 2 ms de-click fades
    const int fade = juce::jmin (L / 2, (int) std::round (0.002 * sr));
    for (int i = 0; i < fade; ++i)
    {
        const float g = (float) i / (float) fade;
        for (int ch = 0; ch < 2; ++ch)
        {
            out.setSample (ch, i, out.getSample (ch, i) * g);
            out.setSample (ch, L - 1 - i, out.getSample (ch, L - 1 - i) * g);
        }
    }

    // never clip
    const float peak = juce::jmax (out.getMagnitude (0, 0, L), out.getMagnitude (1, 0, L));
    if (peak > 0.98f) out.applyGain (0.98f / peak);
    return out;
}
} // namespace gv
