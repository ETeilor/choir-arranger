#pragma once
#include "Look.h"
#include "LiveProcessor.h"

class LiveEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit LiveEditor(LiveProcessor&);
    ~LiveEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    LiveProcessor& proc;
    ChoirLook look;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    juce::ComboBox styleBox, choirBox, chanBox;
    juce::ToggleButton amateurBtn{U("Любительский хор")}, topBtn{U("Верхняя нота = мелодия")};
    juce::TextButton prevVar{juce::String::fromUTF8("◀")}, nextVar{juce::String::fromUTF8("▶")};
    juce::Label styleLbl, choirLbl, chanLbl, varLbl;
    std::unique_ptr<CA> styleA, choirA, chanA;
    std::unique_ptr<BA> amateurA, topA;
    LiveProcessor::Snapshot shown;
};
