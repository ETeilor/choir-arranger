#pragma once
#include "Look.h"
#include "PianoRoll.h"
#include "PluginProcessor.h"

// A part chip: click shows it, drag carries the part to a DAW track as a .mid file.
class DragChip : public juce::Component, public juce::SettableTooltipClient {
public:
    std::function<juce::File()> makeFile;
    std::function<void()> onClick;     // a click without dragging (locks the part)
    bool locked = false, lockable = false;
    juce::String label, shortLabel;   // the short one is used when the chip is narrow
    juce::Colour colour;
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    bool hover = false, dragging = false;
    void mouseUp(const juce::MouseEvent& e) override {
        if (!dragging && e.getDistanceFromDragStart() < 4 && onClick) onClick();
        dragging = false;
    }
};

class ChoirEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit ChoirEditor(ChoirProcessor&);
    ~ChoirEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dropHover = false; repaint(); }

private:
    void timerCallback() override;
    void refresh();
    void updateStyleControls();
    void openFile();
    void exportAll();
    void exportScore();
    void showError(const juce::String& err);

    ChoirProcessor& proc;
    ChoirLook look;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::TextButton captureBtn{U("Захват")}, openBtn{U("Открыть MIDI")}, playBtn{U("Прослушать")}, exportBtn{U("Экспорт .mid")},
        scoreBtn{U("Ноты (MusicXML)")}, prevVarBtn{juce::String::fromUTF8("◀")}, nextVarBtn{U("Другой вариант ▶")};
    juce::ComboBox styleBox, textureBox, outOfModeBox, soundBox, choirBox, inputBox;
    juce::ToggleButton amateurBtn{U("Любительский хор")};
    std::unique_ptr<BA> amateurA;
    juce::Label variantLbl;
    juce::ToggleButton octavistsBtn{U("Октависты")}, mixedBtn{U("Смешанный хор")};
    juce::Label styleLbl, textureLbl, outOfModeLbl, complexityLbl, voicesLbl, soundLbl, choirLbl, inputLbl;
    juce::Slider complexity;
    juce::Slider voice[4];          // S A T B
    juce::Label voiceLbl[4];
    std::unique_ptr<CA> styleA, textureA, outOfModeA, soundA, choirA, inputA;
    std::unique_ptr<BA> octA, mixedA;
    std::unique_ptr<SA> complexityA, voiceA[4];

    PianoRoll roll;
    juce::OwnedArray<DragChip> chips;
    std::unique_ptr<DragChip> allChip, scoreChip;
    juce::Label report, sourceLbl, hintLbl;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dropHover = false;
    juce::TooltipWindow tips{this, 700};
};
