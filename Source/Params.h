#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace gv
{
namespace ids
{
    inline constexpr const char* length = "length";
    inline constexpr const char* size   = "size";
    inline constexpr const char* tone   = "tone";
    inline constexpr const char* width  = "width";
    inline constexpr const char* output = "output";
}

constexpr double kFallbackBpm = 120.0;
constexpr int    kFallbackNumerator = 4;
constexpr int    kFallbackDenominator = 4;

/** LENGTH choice index -> fraction of a bar (0 = 1/4, 1 = 1/2, 2 = 1 bar). */
inline double lengthInBars (int index) noexcept
{
    return index <= 0 ? 0.25 : (index == 1 ? 0.5 : 1.0);
}

/** Duration of one bar in seconds. Invalid tempo / time signature fall back to 120 BPM, 4/4. */
inline double barDurationSeconds (double bpm, int numerator, int denominator) noexcept
{
    if (! (std::isfinite (bpm) && bpm > 1.0 && bpm < 999.0)) bpm = kFallbackBpm;
    if (numerator <= 0)   numerator = kFallbackNumerator;
    if (denominator <= 0) denominator = kFallbackDenominator;
    return (double) numerator * (4.0 / (double) denominator) * 60.0 / bpm;
}

/** Exact length of the generated swell in samples for the chosen musical LENGTH. */
inline int lengthInSamples (int lengthIndex, double bpm, int numerator, int denominator, double sampleRate) noexcept
{
    return (int) std::llround (lengthInBars (lengthIndex) * barDurationSeconds (bpm, numerator, denominator) * sampleRate);
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::length, 1 }, "Length",
                                                         StringArray { "1/4 BAR", "1/2 BAR", "1 BAR" }, 1));

    auto percent = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
    {
        return String (roundToInt (v * 100.0f)) + " %";
    });

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::size, 1 }, "Size",
                                                        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::tone, 1 }, "Tone",
                                                        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::width, 1 }, "Width",
                                                        NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f, percent));

    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
    {
        return String (v, 1) + " dB";
    });
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::output, 1 }, "Output",
                                                        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, db));
    return layout;
}
} // namespace gv
