#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>
#include <cmath>

// Pitch shifter à deux têtes de lecture (delay line + fenêtre de Hann).
class PitchShifter
{
public:
    void prepare (double sr)
    {
        buf.assign ((size_t) juce::nextPowerOfTwo ((int) (sr * 0.25)), 0.0f);
        mask = (int) buf.size() - 1;
        w = (float) (0.02 * sr);
        phase = 0.0f;
        wp = 0;
    }

    float process (float in, float ratio, float targetW)
    {
        buf[(size_t) (wp & mask)] = in;
        w += 0.001f * (targetW - w);
        phase += (1.0f - ratio) / w;
        phase -= std::floor (phase);

        float out = 0.0f;
        for (int t = 0; t < 2; ++t)
        {
            float p = phase + 0.5f * (float) t;
            p -= std::floor (p);
            const float win = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * p);
            out += win * readAt ((float) wp - (p * w + 1.0f));
        }
        ++wp;
        return out;
    }

private:
    float readAt (float pos) const
    {
        const int i = (int) std::floor (pos);
        const float f = pos - (float) i;
        return buf[(size_t) (i & mask)] * (1.0f - f) + buf[(size_t) ((i + 1) & mask)] * f;
    }

    std::vector<float> buf;
    int mask = 0, wp = 0;
    float w = 1000.0f, phase = 0.0f;
};

class HyperTuneAudioProcessor : public juce::AudioProcessor
{
public:
    HyperTuneAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "HyperTune"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void runDetect();
    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    static constexpr int kFrame = 2048;
    static constexpr int kHop = 512;

    double fs = 44100.0;
    std::array<PitchShifter, 2> mainShift, harmShift;

    std::array<float, kFrame> ring {}, frame {};
    std::vector<float> yin;
    int detPos = 0, hopCount = 0;

    bool voiced = false;
    float targetCorr = 0.0f, corrSmooth = 0.0f, targetW = 800.0f;
    float vibPhase = 0.0f, glitchTarget = 0.0f, glitchSmooth = 0.0f;
    int glitchCount = 0;
    juce::Random rng;

    // snapshot des réglages utilisés par la détection
    int keyIdx = 0, scaleIdx = 0;
    float amount = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HyperTuneAudioProcessor)
};
