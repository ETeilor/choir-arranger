#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ChoirEngine.h"

// Cyrillic literals must reach JUCE as UTF-8
inline juce::String U(const char* s) { return juce::String::fromUTF8(s); }

namespace ui {
// palette shared by the editor
const juce::Colour bg{0xff17161a}, panel{0xff201f24}, panelHi{0xff2a2930}, line{0xff34333a},
    text{0xffe9e4da}, textDim{0xff9a958c}, accent{0xffd9a441};
juce::Colour partColour(const std::string& name);
}  // namespace ui

class PianoRoll : public juce::Component {
public:
    void setData(const std::vector<choir::Part>* parts, const std::vector<choir::Note>* input, const choir::Meta& meta);
    void setPlayhead(double ticks) { playhead = ticks; repaint(); }
    void setEmptyText(const juce::String& t) { emptyText = t; repaint(); }
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }
    void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }
    std::function<juce::File()> makeDragFile;   // dragging the roll carries all parts out as one .mid

    static constexpr float keyboardWidth = 54.0f;

private:
    bool hover = false, dragging = false;
    const std::vector<choir::Part>* parts = nullptr;
    const std::vector<choir::Note>* input = nullptr;
    choir::Meta meta;
    double playhead = -1;
    juce::String emptyText;
};
