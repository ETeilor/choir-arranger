#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "ChoirEngine.h"

// Choir Arranger Live: a MIDI FX. Sits in the MIDI FX slot in front of a choir library, voices the
// chord being played in real time and sends the choir voices on, all on one channel or one per voice.
class LiveProcessor : public juce::AudioProcessor {
public:
    LiveProcessor();
    void prepareToPlay(double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Choir Arranger Live"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState params;

    // what sounds now, for the editor (message thread reads a copy)
    struct Snapshot {
        std::vector<std::string> names;
        std::vector<int> pitches;
        std::string chord;
    };
    Snapshot snapshot() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    choir::Settings settings() const;
    void revoice(juce::MidiBuffer& out, int sample);

    choir::LiveVoicer voicer_;
    std::array<int, 128> held_{};        // key counts
    std::set<int> sustained_;            // keys released under the pedal
    bool pedal_ = false;
    int lastVel_ = 90;
    std::vector<int> sounding_;          // pitch per voice (-1 silent)
    std::vector<int> channels_;          // channel per voice it was sent on
    double sampleRate_ = 44100;
    long long pendingAt_ = -1;           // sample counter when the chord settles
    long long clock_ = 0;

    mutable juce::SpinLock snapLock_;
    Snapshot snap_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiveProcessor)
};
