#include "PluginEditor.h"
#include "UIComponents.h"

namespace gv
{
//==============================================================================
class GhostVerbAudioProcessorEditor::Content : public juce::Component,
                                               private juce::Timer
{
public:
    explicit Content (GhostVerbAudioProcessor& p)
        : proc (p),
          wave (p),
          lengthSel (p.apvts, ids::length, { "1/4 BAR", "1/2 BAR", "1 BAR" }),
          knobSize (p.apvts, ids::size, "SIZE"),
          knobTone (p.apvts, ids::tone, "TONE"),
          knobWidth (p.apvts, ids::width, "WIDTH"),
          knobOutput (p.apvts, ids::output, "OUTPUT")
    {
        setLookAndFeel (&laf);

        for (auto* b : { &captureBtn, &stopBtn, &listenBtn, &generateBtn, &previewBtn, &exportBtn })
            addAndMakeVisible (*b);
        for (auto* c : std::initializer_list<juce::Component*> { &wave, &lengthSel, &knobSize, &knobTone, &knobWidth, &knobOutput, &dragZone })
            addAndMakeVisible (*c);

        generateBtn.getProperties().set ("primary", true);
        captureBtn.getProperties().set ("primary", true);

        captureBtn.onClick = [this] { onCapture(); };
        stopBtn.onClick = [this] { proc.stopCapture(); updateUi(); };
        listenBtn.onClick = [this] { onListen(); };
        generateBtn.onClick = [this] { onGenerate(); };
        previewBtn.onClick = [this] { onPreview(); };
        exportBtn.onClick = [this] { onExport(); };

        captureBtn.setTooltip ("Start recording the plugin input (right-click to set the capture limit)");
        captureBtn.addMouseListener (this, false);

        dragZone.getFile = [this]
        {
            auto out = proc.renderer.getOutput();
            return out != nullptr ? out->file : juce::File();
        };
        dragZone.onProblem = [this] (const juce::String& m) { flash (m); };

        startTimerHz (30);
        updateUi();
    }

    ~Content() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void resized() override
    {
        wave.setBounds (20, 68, 520, 138);

        captureBtn.setBounds (20, 214, 100, 28);
        stopBtn.setBounds (126, 214, 76, 28);
        listenBtn.setBounds (208, 214, 96, 28);

        lengthSel.setBounds (32, 276, 128, 26);
        knobSize.setBounds (176, 258, 88, 84);
        knobTone.setBounds (266, 258, 88, 84);
        knobWidth.setBounds (356, 258, 88, 84);
        knobOutput.setBounds (446, 258, 88, 84);

        generateBtn.setBounds (20, 358, 112, 34);
        previewBtn.setBounds (138, 358, 100, 34);
        exportBtn.setBounds (244, 358, 100, 34);
        dragZone.setBounds (350, 358, 190, 34);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::carbon);

        // header
        g.setColour (theme::warm);
        g.setFont (fonts->semiBold (23.0f, 0.12f));
        g.drawText ("GHOST VERB", 20, 12, 320, 30, juce::Justification::centredLeft);
        g.setColour (theme::gold);
        g.setFont (fonts->semiBold (9.0f, 0.22f));
        g.drawText ("REVERSE REVERB GENERATOR", 21, 42, 320, 12, juce::Justification::centredLeft);

        const juce::Rectangle<float> badge (448.0f, 18.0f, 92.0f, 24.0f);
        g.setColour (theme::gold);
        g.drawRoundedRectangle (badge, 6.0f, 1.0f);
        g.setFont (fonts->semiBold (10.0f, 0.14f));
        g.drawText ("FREE VST3", badge, juce::Justification::centred);

        // status line
        g.setColour (statusIsError ? juce::Colour (0xffe07a6a) : theme::muted);
        g.setFont (fonts->regular (11.0f));
        g.drawText (statusText, 312, 214, 228, 28, juce::Justification::centredRight);

        // controls panel
        g.setColour (theme::surface);
        g.fillRoundedRectangle (20.0f, 252.0f, 520.0f, 96.0f, 8.0f);
        g.setColour (theme::line);
        g.drawRoundedRectangle (20.5f, 252.5f, 519.0f, 95.0f, 8.0f, 1.0f);

        g.setColour (theme::gold);
        g.setFont (fonts->semiBold (10.0f, 0.14f));
        g.drawText ("LENGTH", 32, 260, 128, 14, juce::Justification::centredLeft);
        g.setColour (theme::warm);
        g.setFont (fonts->semiBold (13.0f));
        g.drawText (lengthText, 32, 307, 128, 16, juce::Justification::centredLeft);
        g.setColour (theme::muted);
        g.setFont (fonts->regular (10.0f));
        g.drawText (tempoText, 32, 323, 130, 14, juce::Justification::centredLeft);

        // footer
        g.setColour (theme::line);
        g.drawHorizontalLine (404, 20.0f, 540.0f);
        g.setColour (theme::goldDim.brighter (0.25f));
        g.setFont (fonts->semiBold (9.5f, 0.26f));
        g.drawText ("DESIGNED BY EVRIC HUNTER", 0, 410, getWidth(), 18, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &captureBtn && e.mods.isPopupMenu())
            showLimitMenu();
    }

private:
    //==========================================================================
    void showLimitMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader ("CAPTURE LIMIT");
        const int cur = juce::roundToInt (proc.capture.getLimitSeconds());
        for (int s : { 5, 10, 20, 30 })
            m.addItem (s, juce::String (s) + " seconds", true, s == cur);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (captureBtn),
                         [safe = SafePointer<Content> (this)] (int r)
                         {
                             if (safe != nullptr && r > 0) { safe->proc.setCaptureLimitSeconds ((double) r); safe->updateUi(); }
                         });
    }

    void onCapture()
    {
        if (! proc.startCapture())
            flash ("Audio engine not ready - start playback", true);
        updateUi();
    }

    void onListen()
    {
        if (proc.getPreviewKind() == GhostVerbAudioProcessor::PreviewKind::Selection) proc.stopPreview();
        else if (! proc.previewSelection()) flash ("Nothing to listen to", true);
        updateUi();
    }

    void onGenerate()
    {
        const auto err = proc.generate();
        if (err.isNotEmpty()) flash (err, true);
        updateUi();
    }

    void onPreview()
    {
        if (proc.getPreviewKind() == GhostVerbAudioProcessor::PreviewKind::Result) { proc.stopPreview(); updateUi(); return; }
        if (! proc.previewResult()) flash ("Generate first (sample rate must match the render)", true);
        updateUi();
    }

    void onExport()
    {
        auto out = proc.renderer.getOutput();
        if (out == nullptr) { flash ("Generate first", true); return; }

        const juce::String name = out->file != juce::File() ? out->file.getFileName() : juce::String ("GhostVerb.wav");
        chooser = std::make_unique<juce::FileChooser> ("Export WAV (24-bit stereo)",
                                                       juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (name),
                                                       "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = SafePointer<Content> (this), out] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == juce::File() || safe == nullptr) return;
                                  f = f.withFileExtension ("wav");
                                  juce::String err;
                                  if (AudioRenderer::writeWav24 (out->audio, out->sampleRate, f, err))
                                      safe->flash ("Exported " + f.getFileName());
                                  else
                                      safe->flash (err, true);
                              });
    }

    void flash (const juce::String& msg, bool isError = false)
    {
        flashText = msg;
        flashError = isError;
        flashUntil = juce::Time::getMillisecondCounter() + 4000;
    }

    //==========================================================================
    void timerCallback() override { updateUi(); }

    static juce::String fmtTime (double s) { return juce::String (s, 1) + " s"; }

    void updateUi()
    {
        using State = AudioCaptureEngine::State;
        using Kind = GhostVerbAudioProcessor::PreviewKind;

        const auto cs = proc.capture.getState();
        const bool capturing = cs == State::Capturing;
        const bool haveCapture = cs == State::Ready && proc.capture.getNumCaptured() > 0;
        const auto rs = proc.renderer.getStatus();
        const bool rendering = rs == AudioRenderer::Status::Rendering;
        auto out = proc.renderer.getOutput();
        const bool haveResult = out != nullptr;
        const auto pk = proc.getPreviewKind();

        // new capture finished: default markers + normalised display
        proc.ensureSelection();
        const int serial = proc.capture.getSerial();
        if (! capturing && haveCapture && serial != displaySerial)
        {
            displaySerial = serial;
            const auto snap = proc.capture.getSnapshot();
            float peak = 0.0f;
            if (snap)
                for (int ch = 0; ch < 2; ++ch)
                    peak = juce::jmax (peak, juce::FloatVectorOperations::findMaximum (snap.channel (ch), snap.numSamples),
                                       -juce::FloatVectorOperations::findMinimum (snap.channel (ch), snap.numSamples));
            wave.setDisplayGain (peak > 0.001f ? juce::jmin (16.0f, 0.92f / peak) : 1.0f);
        }
        else if (capturing)
            wave.setDisplayGain (1.0f);

        captureBtn.setEnabled (! capturing);
        stopBtn.setEnabled (capturing);
        listenBtn.setEnabled (haveCapture);
        listenBtn.setToggleState (pk == Kind::Selection, juce::dontSendNotification);
        listenBtn.setButtonText (pk == Kind::Selection ? "STOP LISTEN" : "LISTEN");
        generateBtn.setEnabled (haveCapture && ! rendering);
        generateBtn.setButtonText (rendering ? "RENDERING..." : "GENERATE");
        previewBtn.setEnabled (haveResult);
        previewBtn.setToggleState (pk == Kind::Result, juce::dontSendNotification);
        previewBtn.setButtonText (pk == Kind::Result ? "STOP" : "PREVIEW");
        exportBtn.setEnabled (haveResult);
        dragZone.setReady (haveResult && out->file.existsAsFile());

        // LENGTH read-out (musical duration at the host tempo; 120 BPM fallback)
        const auto host = proc.getHostInfo();
        const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 44100.0;
        const int len = lengthInSamples (lengthSel.getSelected(), host.bpm, host.numerator, host.denominator, sr);
        lengthText = juce::String ((double) len / sr, 2) + " s";
        tempoText = juce::String (host.bpm, host.bpm == std::floor (host.bpm) ? 0 : 1) + " BPM  " + juce::String (host.numerator)
                    + "/" + juce::String (host.denominator) + (host.tempoFromHost ? "" : "  (default)");

        // status text
        juce::String st;
        bool err = false;
        if (juce::Time::getMillisecondCounter() < flashUntil) { st = flashText; err = flashError; }
        else if (capturing) st = juce::String::charToString (0x25CF) + " CAPTURING  " + fmtTime ((double) proc.capture.getNumCaptured() / sr)
                                 + " / " + fmtTime (proc.capture.getLimitSeconds());
        else if (rendering) st = "GENERATING...";
        else if (rs == AudioRenderer::Status::Failed) { st = proc.renderer.getError(); err = true; }
        else if (haveResult) st = "READY  " + juce::String ((double) out->audio.getNumSamples() / out->sampleRate, 2) + " s";
        else if (haveCapture) st = "CAPTURED " + fmtTime ((double) proc.capture.getNumCaptured() / sr) + "  -  set the markers";
        else st = "CAPTURE, THEN PLAY AUDIO";

        if (st != statusText || err != statusIsError || lengthText != lastLength || tempoText != lastTempo)
        {
            statusText = st; statusIsError = err; lastLength = lengthText; lastTempo = tempoText;
            repaint();
        }

        if (capturing || pk == Kind::Selection || cs != lastState)
            wave.repaint();
        lastState = cs;
    }

    //==========================================================================
    GhostLookAndFeel laf;                                  // must outlive the child components
    juce::SharedResourcePointer<theme::Fonts> fonts;
    GhostVerbAudioProcessor& proc;

    WaveformView wave;
    SegmentedChoice lengthSel;
    LabeledKnob knobSize, knobTone, knobWidth, knobOutput;
    DragZone dragZone;
    juce::TextButton captureBtn { "CAPTURE" }, stopBtn { "STOP" }, listenBtn { "LISTEN" },
                     generateBtn { "GENERATE" }, previewBtn { "PREVIEW" }, exportBtn { "EXPORT WAV" };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String statusText, lengthText, tempoText, lastLength, lastTempo, flashText;
    bool statusIsError = false, flashError = false;
    juce::uint32 flashUntil = 0;
    int displaySerial = -1;
    AudioCaptureEngine::State lastState = AudioCaptureEngine::State::Idle;
};

//==============================================================================
GhostVerbAudioProcessorEditor::GhostVerbAudioProcessorEditor (GhostVerbAudioProcessor& p)
    : juce::AudioProcessorEditor (p)
{
    content = std::make_unique<Content> (p);
    addAndMakeVisible (*content);

    setResizable (true, true);
    setResizeLimits (420, 330, 1400, 1100);
    getConstrainer()->setFixedAspectRatio ((double) designWidth / (double) designHeight);
    setSize (designWidth, designHeight);
}

GhostVerbAudioProcessorEditor::~GhostVerbAudioProcessorEditor() = default;

void GhostVerbAudioProcessorEditor::paint (juce::Graphics& g) { g.fillAll (theme::carbon); }

void GhostVerbAudioProcessorEditor::resized()
{
    content->setBounds (0, 0, designWidth, designHeight);
    content->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) designWidth));
}
} // namespace gv
