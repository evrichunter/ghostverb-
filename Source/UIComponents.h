#pragma once

#include "PluginProcessor.h"
#include "Theme.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace gv
{
//==============================================================================
class GhostLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GhostLookAndFeel()
    {
        setDefaultSansSerifTypeface (fonts->regularFace);
        setColour (juce::PopupMenu::backgroundColourId, theme::surface);
        setColour (juce::PopupMenu::textColourId, theme::warm);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::gold);
        setColour (juce::PopupMenu::highlightedTextColourId, theme::carbon);
        setColour (juce::AlertWindow::backgroundColourId, theme::surface);
        setColour (juce::AlertWindow::textColourId, theme::warm);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return fonts->semiBold (11.0f, 0.06f); }
    juce::Font getPopupMenuFont() override { return fonts->regular (13.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool primary = (bool) b.getProperties().getWithDefault ("primary", false);
        const bool active = b.getToggleState();
        const float alpha = b.isEnabled() ? 1.0f : 0.35f;

        juce::Colour fill = primary ? theme::gold : (active ? theme::goldDim : theme::surfaceHi);
        if (b.isEnabled() && over)  fill = fill.brighter (0.12f);
        if (b.isEnabled() && down)  fill = fill.darker (0.15f);

        g.setColour (fill.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, 6.0f);
        if (! primary)
        {
            g.setColour ((active ? theme::gold : theme::line).withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool primary = (bool) b.getProperties().getWithDefault ("primary", false);
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour ((primary ? theme::carbon : theme::warm).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.35f));
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float arcR = radius - 1.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool on = s.isEnabled();

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (theme::line);
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (theme::gold.withMultipliedAlpha (on ? 1.0f : 0.4f));
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float bodyR = arcR - 6.0f;
        g.setColour (theme::surfaceHi);
        g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (theme::line.brighter (0.2f));
        g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        juce::Path tick;
        tick.addRoundedRectangle (-1.1f, -bodyR + 3.0f, 2.2f, bodyR * 0.42f, 1.0f);
        g.setColour (theme::warm);
        g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    }

private:
    juce::SharedResourcePointer<theme::Fonts> fonts;
};

//==============================================================================
/** Rotary knob + name + value read-out, bound to one APVTS float parameter. */
class LabeledKnob : public juce::Component
{
public:
    LabeledKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, const juce::String& labelText)
        : label (labelText)
    {
        auto* param = apvts.getParameter (paramId);
        jassert (param != nullptr);

        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        slider.setMouseDragSensitivity (220);
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        slider.setPopupDisplayEnabled (false, false, nullptr);
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::SliderParameterAttachment> (*static_cast<juce::RangedAudioParameter*> (param), slider);
        parameter = param;
    }

    void resized() override
    {
        slider.setBounds (getLocalBounds().withTrimmedTop (16).withTrimmedBottom (18));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::gold);
        g.setFont (fonts->semiBold (10.0f, 0.14f));
        g.drawText (label, 0, 0, getWidth(), 14, juce::Justification::centred);

        g.setColour (theme::warm);
        g.setFont (fonts->regular (11.0f));
        g.drawText (parameter->getText (parameter->getValue(), 16), 0, getHeight() - 16, getWidth(), 14, juce::Justification::centred);
    }

private:
    juce::SharedResourcePointer<theme::Fonts> fonts;
    juce::String label;
    juce::Slider slider;
    juce::RangedAudioParameter* parameter = nullptr;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
};

//==============================================================================
/** 3-way segmented selector bound to the LENGTH choice parameter. */
class SegmentedChoice : public juce::Component
{
public:
    SegmentedChoice (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, juce::StringArray names)
        : options (std::move (names))
    {
        auto* p = static_cast<juce::RangedAudioParameter*> (apvts.getParameter (paramId));
        parameter = p;
        attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v)
        {
            selected = juce::roundToInt (v);
            repaint();
        });
        attachment->sendInitialUpdate();
    }

    int getSelected() const noexcept { return selected; }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (theme::surfaceHi);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (theme::line);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        const float w = r.getWidth() / (float) options.size();
        for (int i = 0; i < options.size(); ++i)
        {
            auto seg = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (2.0f);
            const bool on = i == selected;
            if (on)
            {
                g.setColour (theme::gold);
                g.fillRoundedRectangle (seg, 4.5f);
            }
            g.setColour (on ? theme::carbon : theme::warm);
            g.setFont (fonts->semiBold (9.5f, 0.05f));
            g.drawText (options[i], seg, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int idx = juce::jlimit (0, options.size() - 1, (int) ((float) e.x / (float) getWidth() * (float) options.size()));
        if (idx != selected)
        {
            attachment->setValueAsCompleteGesture ((float) idx);
        }
    }

private:
    juce::SharedResourcePointer<theme::Fonts> fonts;
    juce::StringArray options;
    int selected = 0;
    juce::RangedAudioParameter* parameter = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

//==============================================================================
/** Real waveform of the captured audio with two draggable markers.
    The start marker is the attack: the reverse swell ends exactly there. */
class WaveformView : public juce::Component
{
public:
    explicit WaveformView (GhostVerbAudioProcessor& p) : proc (p) {}

    void setDisplayGain (float g) { displayGain = g; }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        g.setColour (theme::surface);
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (theme::line);
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

        const auto state = proc.capture.getState();
        const auto snap = proc.capture.getSnapshot();          // immutable view: the audio thread never touches these samples
        const int num = snap.numSamples;
        const bool capturing = state == AudioCaptureEngine::State::Capturing;
        const auto inner = getLocalBounds().reduced (10, 8);
        const float midY = (float) inner.getCentreY();
        g.setColour (theme::line);
        g.drawHorizontalLine ((int) midY, (float) inner.getX(), (float) inner.getRight());

        if (num <= 0)
        {
            g.setColour (theme::muted);
            g.setFont (fonts->semiBold (11.0f, 0.16f));
            g.drawText (capturing ? "LISTENING..." : "PRESS CAPTURE AND PLAY AUDIO", getLocalBounds(), juce::Justification::centred);
            return;
        }

        const int span = capturing ? juce::jmax (num, snap.capacity) : num;
        const float* l = snap.channel (0);
        const float* r = snap.channel (1);

        int selA = 0, selB = 0;
        proc.getSelection (selA, selB);
        const float xA = sampleToX (selA, span, inner), xB = sampleToX (selB, span, inner);
        const bool showSel = ! capturing && selB > selA;

        if (showSel)
        {
            g.setColour (theme::gold.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float> (xA, (float) inner.getY(), xB - xA, (float) inner.getHeight()));
        }

        const float halfH = (float) inner.getHeight() * 0.5f - 2.0f;
        const int cols = inner.getWidth();
        for (int c = 0; c < cols; ++c)
        {
            const int s0 = (int) ((double) c / cols * span);
            const int s1 = juce::jmin (num, (int) ((double) (c + 1) / cols * span));
            if (s0 >= num) break;
            const int step = juce::jmax (1, (s1 - s0) / 48);
            float mn = 0.0f, mx = 0.0f;
            for (int i = s0; i < juce::jmax (s1, s0 + 1); i += step)
            {
                mn = juce::jmin (mn, l[i], r[i]);
                mx = juce::jmax (mx, l[i], r[i]);
            }
            const float x = (float) inner.getX() + (float) c + 0.5f;
            const bool inside = showSel && x >= xA && x <= xB;
            g.setColour (inside ? theme::gold : (showSel || capturing ? theme::goldDim : theme::gold.withAlpha (0.85f)));
            const float top = midY - juce::jlimit (0.0f, 1.0f, mx * displayGain) * halfH;
            const float bot = midY - juce::jlimit (-1.0f, 0.0f, mn * displayGain) * halfH;
            g.drawVerticalLine ((int) x, top, juce::jmax (bot, top + 1.0f));
        }

        if (capturing)
        {
            const float x = sampleToX (num, span, inner);
            g.setColour (theme::warm);
            g.drawVerticalLine ((int) x, (float) inner.getY(), (float) inner.getBottom());
        }

        if (showSel)
        {
            drawMarker (g, xA, inner, "ATTACK", true);
            drawMarker (g, xB, inner, "END", false);

            if (proc.getPreviewKind() == GhostVerbAudioProcessor::PreviewKind::Selection)
            {
                const float px = xA + (xB - xA) * proc.getPreviewPosition();
                g.setColour (theme::warm);
                g.drawVerticalLine ((int) px, (float) inner.getY(), (float) inner.getBottom());
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! canEdit()) return;
        const auto inner = getLocalBounds().reduced (10, 8);
        const int num = proc.capture.getNumCaptured();
        int a, b;
        proc.getSelection (a, b);
        const float xA = sampleToX (a, num, inner), xB = sampleToX (b, num, inner);
        const float x = (float) e.x;

        if (std::abs (x - xA) <= 8.0f)       drag = Drag::start;
        else if (std::abs (x - xB) <= 8.0f)  drag = Drag::end;
        else if (x > xA && x < xB)           { drag = Drag::both; grabOffset = xToSample (x, num, inner) - a; }
        else                                 drag = std::abs (x - xA) < std::abs (x - xB) ? Drag::start : Drag::end;

        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! canEdit() || drag == Drag::none) return;
        const auto inner = getLocalBounds().reduced (10, 8);
        const int num = proc.capture.getNumCaptured();
        const int minLen = (int) (0.05 * proc.capture.getSampleRate());
        const int pos = xToSample ((float) e.x, num, inner);
        int a, b;
        proc.getSelection (a, b);

        switch (drag)
        {
            case Drag::start: a = juce::jlimit (0, b - minLen, pos); break;
            case Drag::end:   b = juce::jlimit (a + minLen, num, pos); break;
            case Drag::both:
            {
                const int len = b - a;
                a = juce::jlimit (0, num - len, pos - grabOffset);
                b = a + len;
                break;
            }
            default: break;
        }
        proc.setSelection (a, b);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { drag = Drag::none; }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (! canEdit()) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
        const auto inner = getLocalBounds().reduced (10, 8);
        const int num = proc.capture.getNumCaptured();
        int a, b;
        proc.getSelection (a, b);
        const bool near = std::abs ((float) e.x - sampleToX (a, num, inner)) <= 8.0f
                       || std::abs ((float) e.x - sampleToX (b, num, inner)) <= 8.0f;
        setMouseCursor (near ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
    }

private:
    enum class Drag { none, start, end, both };

    bool canEdit() const
    {
        return proc.capture.getState() == AudioCaptureEngine::State::Ready && proc.capture.getNumCaptured() > 0;
    }

    static float sampleToX (int s, int span, juce::Rectangle<int> inner)
    {
        return (float) inner.getX() + (span > 0 ? (float) ((double) s / span * inner.getWidth()) : 0.0f);
    }

    static int xToSample (float x, int span, juce::Rectangle<int> inner)
    {
        return juce::jlimit (0, span, (int) std::round ((double) (x - (float) inner.getX()) / juce::jmax (1, inner.getWidth()) * span));
    }

    void drawMarker (juce::Graphics& g, float x, juce::Rectangle<int> inner, const char* text, bool isAttack) const
    {
        const auto col = isAttack ? theme::gold : theme::warm;
        g.setColour (col);
        g.drawVerticalLine ((int) x, (float) inner.getY(), (float) inner.getBottom());

        const float flagW = isAttack ? 46.0f : 30.0f;
        const float fx = isAttack ? x : x - flagW;
        juce::Rectangle<float> flag (fx, (float) inner.getY(), flagW, 13.0f);
        g.fillRoundedRectangle (flag, 3.0f);
        g.setColour (theme::carbon);
        g.setFont (fonts->semiBold (8.5f, 0.08f));
        g.drawText (text, flag, juce::Justification::centred);

        g.setColour (col);
        g.fillEllipse (x - 3.5f, (float) inner.getBottom() - 7.0f, 7.0f, 7.0f);
    }

    juce::SharedResourcePointer<theme::Fonts> fonts;
    GhostVerbAudioProcessor& proc;
    float displayGain = 1.0f;
    Drag drag = Drag::none;
    int grabOffset = 0;
};

//==============================================================================
/** Drag the generated WAV straight into FL Studio (native JUCE external drag & drop). */
class DragZone : public juce::Component
{
public:
    std::function<juce::File()> getFile;
    std::function<void (const juce::String&)> onProblem;

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool ready = isReady();
        g.setColour (ready ? theme::gold.withAlpha (isMouseOver() ? 0.22f : 0.12f) : theme::surface);
        g.fillRoundedRectangle (r, 6.0f);

        g.setColour (ready ? theme::gold : theme::line);
        juce::Path outline;
        outline.addRoundedRectangle (r, 6.0f);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.fillPath (dashed);

        g.setColour (ready ? theme::gold : theme::muted);
        g.setFont (fonts->semiBold (10.5f, 0.08f));
        g.drawText (ready ? "DRAG WAV TO FL STUDIO" : "DRAG & DROP", getLocalBounds(), juce::Justification::centred);
    }

    void setReady (bool r) { if (r != ready) { ready = r; repaint(); } }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() < 6 || dragging) return;
        const auto file = getFile ? getFile() : juce::File();
        if (! file.existsAsFile())
        {
            if (onProblem) onProblem ("Generate first, or use EXPORT WAV");
            return;
        }
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                     [safe = juce::Component::SafePointer<DragZone> (this)] { if (safe != nullptr) safe->dragging = false; });
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

    void mouseMove (const juce::MouseEvent&) override
    {
        setMouseCursor (isReady() ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }

private:
    bool isReady() const { return ready; }

    juce::SharedResourcePointer<theme::Fonts> fonts;
    bool dragging = false, ready = false;
};
} // namespace gv
