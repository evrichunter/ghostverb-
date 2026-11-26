#pragma once

#include "BinaryData.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace gv::theme
{
const juce::Colour carbon    { 0xff111111 };
const juce::Colour surface   { 0xff1a1a1a };
const juce::Colour surfaceHi { 0xff262626 };
const juce::Colour line      { 0xff303030 };
const juce::Colour gold      { 0xffc8a96e };
const juce::Colour goldDim   { 0xff7a6841 };
const juce::Colour warm      { 0xfff4f1ea };
const juce::Colour muted     { 0xff8f8a80 };

/** Inter (embedded, SIL OFL). Shared so the typefaces are released with the last editor. */
struct Fonts
{
    juce::Typeface::Ptr regularFace  = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf,  (size_t) BinaryData::InterRegular_ttfSize);
    juce::Typeface::Ptr semiBoldFace = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, (size_t) BinaryData::InterSemiBold_ttfSize);

    juce::Font regular (float h, float tracking = 0.0f) const
    {
        return juce::Font (juce::FontOptions (regularFace).withHeight (h)).withExtraKerningFactor (tracking);
    }
    juce::Font semiBold (float h, float tracking = 0.0f) const
    {
        return juce::Font (juce::FontOptions (semiBoldFace).withHeight (h)).withExtraKerningFactor (tracking);
    }
};
} // namespace gv::theme
