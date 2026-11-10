#pragma once
#include "Look.h"
#include "LiveProcessor.h"

// the take played so far, dragged out to a track as a MIDI file
class TakeChip : public juce::Component, public juce::SettableTooltipClient {
public:
    std::function<juce::File()> makeFile;
    juce::String label;
    bool enabled = false;
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }
    void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    bool hover = false, dragging = false;
};

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
    juce::ComboBox styleBox, choirBox, chanBox, regBox;
    juce::ToggleButton amateurBtn{U("Любительский хор")}, topBtn{U("Верхняя нота — мелодия")},
        velBtn{U("Сила нажатия по голосам")};
    juce::TextButton prevVar, nextVar, clearBtn{U("Очистить")};
    juce::Label styleLbl, choirLbl, chanLbl, regLbl, varLbl, titleLbl, hintLbl;
    std::unique_ptr<CA> styleA, choirA, chanA, regA;
    std::unique_ptr<BA> amateurA, topA, velA;
    TakeChip take;
    LiveProcessor::Snapshot shown;
    int shownChords = -1;
    juce::TooltipWindow tips{this, 700};
};
