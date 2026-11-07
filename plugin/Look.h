#pragma once
#include "PianoRoll.h"

// Shared look of both plug-ins.
class ChoirLook : public juce::LookAndFeel_V4 {
public:
    ChoirLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1,
                          juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool hi, bool down) override;
};


namespace names {
juce::StringArray styles();   // order of choir::Style
juce::StringArray choirs();   // order of choir::Choir
}
