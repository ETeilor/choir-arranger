#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "ChoirEngine.h"

// The processor keeps the input progression, the settings and the last arrangement.
// Input arrives two ways: a .mid file dropped on the window, or "capture": the plug-in sits on
// an instrument track and records the region while the host plays it.
// Audio output is only the built-in preview voice (a rough "aah"), used for auditioning.
class ChoirProcessor : public juce::AudioProcessor,
                      private juce::AudioProcessorValueTreeState::Listener,
                      private juce::AsyncUpdater {
public:
    ChoirProcessor();
    ~ChoirProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Choir Arranger"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- model, message thread only
    juce::AudioProcessorValueTreeState params;
    bool loadMidiFile(const juce::File& f, juce::String& err);
    void setInput(std::vector<choir::Note> notes, choir::Meta meta, const juce::String& source);
    void regenerate();
    const choir::Result& result() const { return result_; }
    const std::vector<choir::Note>& input() const { return input_; }
    const choir::Meta& meta() const { return meta_; }
    juce::String inputSource() const { return inputSource_; }
    juce::File writePartFile(int partIndex) const;      // -1 = all parts in one file
    std::function<void()> onResultChanged;

    // capture
    void setCapture(bool armed);
    bool isCapturing() const { return captureArmed_.load(); }
    bool captureHasData() const { return captureCount_.load() > 0; }

    // preview
    void setPreview(bool on);
    bool isPreviewing() const { return previewOn_.load(); }
    double previewPositionTicks() const { return previewTicks_.load(); }

private:
    void parameterChanged(const juce::String&, float) override;
    void handleAsyncUpdate() override;
    choir::Settings currentSettings() const;
    void finishCapture();
    void rebuildPreviewEvents();
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    std::vector<choir::Note> input_;
    choir::Meta meta_;
    juce::String inputSource_;
    choir::Result result_;

    // capture (audio thread writes, message thread reads after stop)
    struct RawEvent { double ppq; int note, vel; bool on; };
    juce::SpinLock captureLock_;
    std::vector<RawEvent> captured_;
    std::atomic<bool> captureArmed_{false}, wasPlaying_{false};
    std::atomic<int> captureCount_{0};
    std::atomic<double> captureBpm_{120.0};
    std::atomic<int> captureNum_{4}, captureDen_{4};

    // preview synth
    struct PEvent { double tick; int note; bool on; };
    juce::SpinLock previewLock_;
    std::vector<PEvent> previewEvents_;
    double previewLenTicks_ = 0;
    std::atomic<bool> previewOn_{false}, killNotes_{false};
    std::atomic<double> previewTicks_{0};
    size_t previewIdx_ = 0;
    juce::Synthesiser synth_;
    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChoirProcessor)
};
