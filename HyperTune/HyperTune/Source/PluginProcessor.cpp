#include "PluginProcessor.h"

using APVTS = juce::AudioProcessorValueTreeState;

static const std::array<std::array<int, 12>, 7> kScales = {{
    {1,1,1,1,1,1,1,1,1,1,1,1}, // Chromatic
    {1,0,1,0,1,1,0,1,0,1,0,1}, // Major
    {1,0,1,1,0,1,0,1,1,0,1,0}, // Minor
    {1,0,1,1,0,1,0,1,1,0,0,1}, // Harmonic minor
    {1,0,1,0,1,0,0,1,0,1,0,0}, // Pentatonic major
    {1,0,0,1,0,1,0,1,0,0,1,0}, // Pentatonic minor
    {1,0,0,1,0,1,1,1,0,0,1,0}  // Blues
}};

HyperTuneAudioProcessor::HyperTuneAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

APVTS::ParameterLayout HyperTuneAudioProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    using R = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<C> (juce::ParameterID { "key", 1 }, "Key",
        juce::StringArray { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 0));
    p.push_back (std::make_unique<C> (juce::ParameterID { "scale", 1 }, "Scale",
        juce::StringArray { "Chromatic","Major","Minor","Harmonic Minor","Pentatonic Major","Pentatonic Minor","Blues" }, 0));
    p.push_back (std::make_unique<F> (juce::ParameterID { "retune", 1 }, "Retune Speed (ms)", R (0.0f, 400.0f, 0.1f, 0.4f), 0.0f));
    p.push_back (std::make_unique<F> (juce::ParameterID { "amount", 1 }, "Amount", R (0.0f, 1.0f, 0.01f), 1.0f));
    p.push_back (std::make_unique<F> (juce::ParameterID { "shift", 1 }, "Pitch Shift (st)", R (-12.0f, 12.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<F> (juce::ParameterID { "vibdepth", 1 }, "Vibrato Depth (cents)", R (0.0f, 100.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<F> (juce::ParameterID { "vibrate", 1 }, "Vibrato Rate (Hz)", R (0.1f, 10.0f, 0.01f), 5.0f));
    p.push_back (std::make_unique<F> (juce::ParameterID { "harmint", 1 }, "Harmony Interval (st)", R (-12.0f, 12.0f, 1.0f), 7.0f));
    p.push_back (std::make_unique<F> (juce::ParameterID { "harmlevel", 1 }, "Harmony Level", R (0.0f, 1.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<F> (juce::ParameterID { "glitch", 1 }, "Glitch Amount", R (0.0f, 1.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<F> (juce::ParameterID { "glitchrate", 1 }, "Glitch Rate (Hz)", R (1.0f, 16.0f, 0.1f), 6.0f));
    p.push_back (std::make_unique<F> (juce::ParameterID { "drive", 1 }, "Drive", R (0.0f, 1.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<F> (juce::ParameterID { "mix", 1 }, "Dry/Wet", R (0.0f, 1.0f, 0.01f), 1.0f));

    return { p.begin(), p.end() };
}

void HyperTuneAudioProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    for (auto& s : mainShift) s.prepare (fs);
    for (auto& s : harmShift) s.prepare (fs);
    ring.fill (0.0f);
    frame.fill (0.0f);
    yin.assign (kFrame / 2, 0.0f);
    detPos = hopCount = 0;
    voiced = false;
    targetCorr = corrSmooth = 0.0f;
    targetW = (float) (0.02 * fs);
    glitchCount = 0;
    glitchTarget = glitchSmooth = 0.0f;
}

bool HyperTuneAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

// Détection de hauteur YIN
void HyperTuneAudioProcessor::runDetect()
{
    const int N = kFrame, Wn = N / 2;
    float e = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        frame[(size_t) i] = ring[(size_t) ((detPos + i) & (N - 1))];
        e += frame[(size_t) i] * frame[(size_t) i];
    }
    if (std::sqrt (e / (float) N) < 0.004f) { voiced = false; return; }

    const int tauMax = juce::jmin ((int) (fs / 70.0), Wn - 2);
    const int tauMin = juce::jmax (3, (int) (fs / 1000.0));

    yin[0] = 1.0f;
    float run = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < Wn; ++j)
        {
            const float d = frame[(size_t) j] - frame[(size_t) (j + tau)];
            sum += d * d;
        }
        run += sum;
        yin[(size_t) tau] = run > 0.0f ? sum * (float) tau / run : 1.0f;
    }

    int found = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (yin[(size_t) tau] < 0.15f)
        {
            while (tau + 1 < tauMax && yin[(size_t) tau + 1] < yin[(size_t) tau]) ++tau;
            found = tau;
            break;
        }
    }
    if (found < 0) { voiced = false; return; }

    const float x0 = yin[(size_t) found - 1], x1 = yin[(size_t) found], x2 = yin[(size_t) found + 1];
    const float den = x0 - 2.0f * x1 + x2;
    const float tauF = (float) found + (std::abs (den) > 1e-9f ? 0.5f * (x0 - x2) / den : 0.0f);

    const float f0 = (float) fs / tauF;
    const float midi = 69.0f + 12.0f * std::log2 (f0 / 440.0f);

    // note la plus proche dans la gamme
    const auto& mask = kScales[(size_t) scaleIdx];
    float best = std::round (midi), bestDist = 1e9f;
    for (int n = (int) std::round (midi) - 6; n <= (int) std::round (midi) + 6; ++n)
    {
        const int deg = ((n - keyIdx) % 12 + 12) % 12;
        if (mask[(size_t) deg] == 0) continue;
        const float dist = std::abs ((float) n - midi);
        if (dist < bestDist) { bestDist = dist; best = (float) n; }
    }

    targetCorr = (best - midi) * amount;
    targetW = juce::jlimit ((float) (0.006 * fs), (float) (0.04 * fs), 2.0f * tauF);
    voiced = true;
}

void HyperTuneAudioProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = b.getNumSamples();
    const int nch = juce::jmin (b.getNumChannels(), 2);

    keyIdx = (int) param ("key");
    scaleIdx = (int) param ("scale");
    amount = param ("amount");
    const float retuneMs = param ("retune"), shift = param ("shift");
    const float vibCents = param ("vibdepth"), vibRate = param ("vibrate");
    const float harmInt = param ("harmint"), harmLevel = param ("harmlevel");
    const float glitchAmt = param ("glitch"), glitchRate = param ("glitchrate");
    const float drive = param ("drive"), mix = param ("mix");

    const float a = retuneMs <= 0.5f ? 0.0f : std::exp (-1.0f / (retuneMs * 0.001f * (float) fs));
    const float g = 1.0f + drive * 10.0f;
    static const float glitchOpts[7] = { 12.0f, -12.0f, 7.0f, 5.0f, -5.0f, 24.0f, -24.0f };

    for (int i = 0; i < n; ++i)
    {
        float mono = 0.0f;
        for (int c = 0; c < nch; ++c) mono += b.getSample (c, i);
        ring[(size_t) detPos] = mono / (float) nch;
        detPos = (detPos + 1) & (kFrame - 1);
        if (++hopCount >= kHop) { hopCount = 0; runDetect(); }

        corrSmooth = a * corrSmooth + (1.0f - a) * targetCorr;

        vibPhase += vibRate / (float) fs;
        vibPhase -= std::floor (vibPhase);
        const float vib = vibCents * 0.01f * std::sin (juce::MathConstants<float>::twoPi * vibPhase);

        if (--glitchCount <= 0)
        {
            glitchCount = juce::jmax (1, (int) ((float) fs / glitchRate));
            glitchTarget = rng.nextFloat() < glitchAmt ? glitchOpts[rng.nextInt (7)] : 0.0f;
        }
        glitchSmooth += 0.002f * (glitchTarget - glitchSmooth);

        const float semis = corrSmooth + shift + vib + glitchSmooth;
        const float ratio = std::pow (2.0f, semis / 12.0f);
        const float ratioH = std::pow (2.0f, (semis + harmInt) / 12.0f);

        for (int c = 0; c < nch; ++c)
        {
            const float dry = b.getSample (c, i);
            float wet = mainShift[(size_t) c].process (dry, ratio, targetW);
            if (harmLevel > 0.001f)
                wet += harmLevel * harmShift[(size_t) c].process (dry, ratioH, targetW);
            if (drive > 0.001f)
                wet = std::tanh (wet * g) / std::sqrt (g);
            b.setSample (c, i, dry * (1.0f - mix) + wet * mix);
        }
    }
}

juce::AudioProcessorEditor* HyperTuneAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void HyperTuneAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void HyperTuneAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HyperTuneAudioProcessor();
}
