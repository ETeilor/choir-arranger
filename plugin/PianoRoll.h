#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ChoirEngine.h"

// Cyrillic literals must reach JUCE as UTF-8
inline juce::String U(const char* s) { return juce::String::fromUTF8(s); }

namespace ui {
// palette shared by both editors: near-black neutrals, one accent, soft part colours
const juce::Colour bg{0xff0c0c0e}, panel{0xff141417}, panelHi{0xff1c1c20}, line{0xff29292f},
    text{0xfff1f0ec}, textDim{0xff80808a}, accent{0xffc9f25c}, onAccent{0xff10110a};
juce::Colour partColour(const std::string& name);
juce::Font font(float size, bool medium = false);
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

    static constexpr float keyboardWidth = 44.0f;

private:
    bool hover = false, dragging = false;
    const std::vector<choir::Part>* parts = nullptr;
    const std::vector<choir::Note>* input = nullptr;
    choir::Meta meta;
    double playhead = -1;
    juce::String emptyText;
};
