#include "PianoRoll.h"

juce::Colour ui::partColour(const std::string& n) {
    if (n == "Soprano") return juce::Colour(0xffe8b04b);
    if (n == "Alto") return juce::Colour(0xffe07a6a);
    if (n == "Tenor") return juce::Colour(0xff5fb7a6);
    if (n == "Bass") return juce::Colour(0xff6d8fd8);
    if (n == "Octavists") return juce::Colour(0xff9a7ad6);
    return juce::Colours::grey;
}

void PianoRoll::setData(const std::vector<choir::Part>* p, const std::vector<choir::Note>* in, const choir::Meta& m) {
    parts = p;
    input = in;
    meta = m;
    repaint();
}

void PianoRoll::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(ui::panel);
    g.fillRoundedRectangle(r, 8.0f);

    bool haveParts = parts && !parts->empty();
    bool haveInput = input && !input->empty();
    if (!haveParts && !haveInput) {
        g.setColour(ui::textDim);
        g.setFont(juce::FontOptions(15.0f));
        g.drawFittedText(emptyText, getLocalBounds().reduced(40), juce::Justification::centred, 4);
        return;
    }

    int lo = 127, hi = 0, endT = 0;
    auto scan = [&](const std::vector<choir::Note>& ns) {
        for (auto& n : ns) lo = std::min(lo, n.pitch), hi = std::max(hi, n.pitch), endT = std::max(endT, n.end);
    };
    if (haveParts)
        for (auto& p : *parts) scan(p.notes);
    else
        scan(*input);
    lo -= 2, hi += 2;
    if (endT <= 0) return;

    auto area = r.reduced(12.0f, 10.0f);
    area.removeFromLeft(34.0f);   // pitch labels
    const float rowH = area.getHeight() / (float)(hi - lo + 1);
    auto xOf = [&](double t) { return area.getX() + (float)(t / endT) * area.getWidth(); };
    auto yOf = [&](int p) { return area.getBottom() - (float)(p - lo + 1) * rowH; };

    // octave lines and labels (C notes)
    g.setFont(juce::FontOptions(10.5f));
    for (int p = lo; p <= hi; ++p) {
        if (p % 12 != 0) continue;
        g.setColour(ui::line);
        g.drawHorizontalLine((int)(yOf(p) + rowH), area.getX(), area.getRight());
        g.setColour(ui::textDim);
        g.drawText("C" + juce::String(p / 12 - 1), juce::Rectangle<float>(r.getX() + 8, yOf(p), 30, rowH + 2),
                   juce::Justification::centredLeft);
    }
    // bars
    int barT = meta.num * choir::TPB * 4 / std::max(1, meta.den);
    for (int t = 0, b = 1; t <= endT; t += barT, ++b) {
        g.setColour(ui::line);
        g.drawVerticalLine((int)xOf(t), area.getY(), area.getBottom());
        g.setColour(ui::textDim);
        if (t < endT) g.drawText(juce::String(b), juce::Rectangle<float>(xOf(t) + 3, area.getY(), 24, 12), juce::Justification::topLeft);
    }
    // the user's chords, faint, behind the parts
    if (haveInput) {
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        for (auto& n : *input)
            if (n.pitch >= lo && n.pitch <= hi)
                g.fillRect(juce::Rectangle<float>(xOf(n.start), yOf(n.pitch), xOf(n.end) - xOf(n.start), rowH));
    }
    if (haveParts) {
        for (auto& p : *parts) {
            auto c = ui::partColour(p.name);
            for (auto& n : p.notes) {
                juce::Rectangle<float> nr(xOf(n.start) + 0.5f, yOf(n.pitch) + 0.5f,
                                          std::max(2.0f, xOf(n.end) - xOf(n.start) - 1.0f), std::max(2.0f, rowH - 1.0f));
                g.setColour(c.withAlpha(0.85f));
                g.fillRoundedRectangle(nr, std::min(3.0f, rowH * 0.4f));
            }
        }
    }
    if (playhead >= 0 && playhead <= endT) {
        g.setColour(ui::accent);
        g.drawLine(xOf(playhead), area.getY(), xOf(playhead), area.getBottom(), 1.5f);
    }
}
