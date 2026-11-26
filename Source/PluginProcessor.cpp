#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace gv
{
namespace
{
    juce::File tempRoot() { return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GhostVerb"); }
}

GhostVerbAudioProcessor::GhostVerbAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GHOSTVERB", createParameterLayout())
{
    // temp folder for this instance; also sweep leftovers (> 1 day) from crashed sessions
    for (const auto& d : tempRoot().findChildFiles (juce::File::findDirectories, false))
        if (d.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::days (1.0))
            d.deleteRecursively();

    sessionDir = tempRoot().getChildFile (juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
}

GhostVerbAudioProcessor::~GhostVerbAudioProcessor()
{
    preview.stop();
    renderer.shutdown();    // stop the render thread before deleting the files it may be writing
    sessionDir.deleteRecursively();
}

bool GhostVerbAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return out == l.getMainInputChannelSet();
}

void GhostVerbAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    capture.prepare (sampleRate);
    preview.prepare (sampleRate);
}

void GhostVerbAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // The playhead is only queried here, on the audio thread; the UI reads the atomics.
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
                if (*bpm > 1.0 && *bpm < 999.0) { hostBpm.store (*bpm, std::memory_order_relaxed); hostTempoKnown.store (true, std::memory_order_relaxed); }
            if (auto ts = pos->getTimeSignature())
                if (ts->numerator > 0 && ts->denominator > 0)
                {
                    hostNum.store (ts->numerator, std::memory_order_relaxed);
                    hostDen.store (ts->denominator, std::memory_order_relaxed);
                }
        }

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    capture.process (buffer);       // records the untouched input
    preview.mixInto (buffer);       // only adds audio while PREVIEW / LISTEN is playing
}

GhostVerbAudioProcessor::HostInfo GhostVerbAudioProcessor::getHostInfo() const noexcept
{
    return { hostBpm.load (std::memory_order_relaxed), hostNum.load (std::memory_order_relaxed),
             hostDen.load (std::memory_order_relaxed), hostTempoKnown.load (std::memory_order_relaxed) };
}

//==============================================================================
void GhostVerbAudioProcessor::setSelection (int s, int e) noexcept
{
    selStart.store (s);
    selEnd.store (e);
}

void GhostVerbAudioProcessor::getSelection (int& s, int& e) const noexcept
{
    s = selStart.load();
    e = selEnd.load();
}

void GhostVerbAudioProcessor::ensureSelection()
{
    const int serial = capture.getSerial();
    const int n = capture.getNumCaptured();
    if (capture.getState() == AudioCaptureEngine::State::Capturing || n <= 0 || selSerial.load() == serial) return;

    selSerial.store (serial);
    setSelection ((int) (n * 0.2), (int) (n * 0.6));
}

bool GhostVerbAudioProcessor::startCapture()
{
    stopPreview();
    return capture.start();
}

void GhostVerbAudioProcessor::stopCapture()
{
    capture.stop();
    ensureSelection();
}

void GhostVerbAudioProcessor::setCaptureLimitSeconds (double s) { capture.setLimitSeconds (s); }

juce::File GhostVerbAudioProcessor::makeTempFile (int lengthIndex, double bpm)
{
    static const char* names[] = { "quarter-bar", "half-bar", "1-bar" };
    return sessionDir.getChildFile ("GhostVerb_" + juce::String (names[juce::jlimit (0, 2, lengthIndex)])
                                    + "_" + juce::String (juce::roundToInt (bpm)) + "bpm_"
                                    + juce::String (++tempCounter) + ".wav");
}

juce::String GhostVerbAudioProcessor::generate()
{
    if (capture.getState() == AudioCaptureEngine::State::Capturing) return "Stop the capture first";

    const int n = capture.getNumCaptured();
    if (n <= 0) return "Capture some audio first";

    ensureSelection();
    int s, e;
    getSelection (s, e);
    s = juce::jlimit (0, n, s);
    e = juce::jlimit (0, n, e);
    const double sr = capture.getSampleRate();
    if (e - s < (int) (0.05 * sr)) return "Selection is too short";

    const auto host = getHostInfo();
    const int lengthIndex = (int) apvts.getRawParameterValue (ids::length)->load();

    AudioRenderer::Job job;
    job.selection = capture.copyRange (s, e - s);
    job.sampleRate = sr;
    job.settings.size = apvts.getRawParameterValue (ids::size)->load();
    job.settings.tone = apvts.getRawParameterValue (ids::tone)->load();
    job.settings.width = apvts.getRawParameterValue (ids::width)->load();
    job.settings.outputDb = apvts.getRawParameterValue (ids::output)->load();
    job.lengthSamples = lengthInSamples (lengthIndex, host.bpm, host.numerator, host.denominator, sr);
    job.bpm = host.bpm;
    job.numerator = host.numerator;
    job.denominator = host.denominator;
    job.outputFile = makeTempFile (lengthIndex, host.bpm);

    if ((double) job.lengthSamples > ReverseReverbEngine::maxSwellSeconds * sr)
        return "LENGTH is too long at this tempo";

    stopPreview();
    renderer.submit (std::move (job));
    return {};
}

bool GhostVerbAudioProcessor::previewSelection()
{
    const int n = capture.getNumCaptured();
    if (n <= 0 || capture.getState() == AudioCaptureEngine::State::Capturing) return false;

    int s, e;
    getSelection (s, e);
    s = juce::jlimit (0, n, s);
    e = juce::jlimit (0, n, e);
    if (e <= s) return false;

    previewKind.store ((int) PreviewKind::Selection);
    preview.play (std::make_shared<juce::AudioBuffer<float>> (capture.copyRange (s, e - s)));
    return true;
}

bool GhostVerbAudioProcessor::previewResult()
{
    auto out = renderer.getOutput();
    if (out == nullptr || out->audio.getNumSamples() <= 0) return false;
    if (std::abs (out->sampleRate - currentSampleRate) > 0.5) return false;   // host sample rate changed since render

    previewKind.store ((int) PreviewKind::Result);
    preview.play (std::shared_ptr<const juce::AudioBuffer<float>> (out, &out->audio));
    return true;
}

void GhostVerbAudioProcessor::stopPreview() { preview.stop(); }

GhostVerbAudioProcessor::PreviewKind GhostVerbAudioProcessor::getPreviewKind() const noexcept
{
    return preview.isPlaying() ? (PreviewKind) previewKind.load() : PreviewKind::None;
}

//==============================================================================
juce::AudioProcessorEditor* GhostVerbAudioProcessor::createEditor()
{
    return new GhostVerbAudioProcessorEditor (*this);
}

void GhostVerbAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("captureLimitSeconds", capture.getLimitSeconds(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void GhostVerbAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            if (tree.hasProperty ("captureLimitSeconds"))
                capture.setLimitSeconds ((double) tree.getProperty ("captureLimitSeconds"));
            apvts.replaceState (tree);
        }
}
} // namespace gv

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new gv::GhostVerbAudioProcessor();
}
