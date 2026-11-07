#include "LiveEditor.h"

LiveEditor::LiveEditor(LiveProcessor& p) : AudioProcessorEditor(p), proc(p) {
    setLookAndFeel(&look);
    auto label = [this](juce::Label& l, const juce::String& t) {
        l.setText(t, juce::dontSendNotification);
        l.setFont(juce::FontOptions(12.0f));
        l.setColour(juce::Label::textColourId, ui::textDim);
        addAndMakeVisible(l);
    };
    label(styleLbl, U("Стиль"));
    label(choirLbl, U("Состав хора"));
    label(chanLbl, U("MIDI-каналы"));
    label(varLbl, {});
    styleBox.addItemList(names::styles(), 1);
    choirBox.addItemList(names::choirs(), 1);
    chanBox.addItemList({U("Все голоса в один канал"), U("Каждый голос в свой канал")}, 1);
    for (auto* c : {&styleBox, &choirBox, &chanBox}) addAndMakeVisible(c);
    styleA = std::make_unique<CA>(proc.params, "style", styleBox);
    choirA = std::make_unique<CA>(proc.params, "choir", choirBox);
    chanA = std::make_unique<CA>(proc.params, "channels", chanBox);
    for (auto* b : {&amateurBtn, &topBtn}) addAndMakeVisible(b);
    amateurA = std::make_unique<BA>(proc.params, "amateur", amateurBtn);
    topA = std::make_unique<BA>(proc.params, "topmelody", topBtn);
    for (auto* b : {&prevVar, &nextVar}) addAndMakeVisible(b);
    auto step = [this](int d) {
        auto* prm = proc.params.getParameter("variant");
        int v = juce::jlimit(0, 99, (int)*proc.params.getRawParameterValue("variant") + d);
        prm->setValueNotifyingHost(prm->convertTo0to1((float)v));
    };
    prevVar.onClick = [step] { step(-1); };
    nextVar.onClick = [step] { step(+1); };
    setSize(720, 420);
    startTimerHz(30);
}

LiveEditor::~LiveEditor() { setLookAndFeel(nullptr); }

void LiveEditor::timerCallback() {
    auto s = proc.snapshot();
    int v = (int)*proc.params.getRawParameterValue("variant");
    varLbl.setText(v == 0 ? U("Основной вариант") : U("Вариант ") + juce::String(v), juce::dontSendNotification);
    if (s.pitches != shown.pitches || s.chord != shown.chord) {
        shown = s;
        repaint();
    }
}

void LiveEditor::paint(juce::Graphics& g) {
    g.fillAll(ui::bg);
    g.setColour(ui::text);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("Choir Arranger Live", 20, 12, 300, 30, juce::Justification::centredLeft);
    g.setColour(ui::textDim);
    g.setFont(juce::FontOptions(12.5f));
    g.drawText(U("MIDI FX: играйте аккорды, хоровая библиотека на этой дорожке получит голоса"), 20, 40, 680, 18,
               juce::Justification::centredLeft);

    auto area = juce::Rectangle<int>(260, 72, getWidth() - 280, getHeight() - 92);
    g.setColour(ui::panel);
    g.fillRoundedRectangle(area.toFloat(), 8.0f);
    auto in = area.reduced(16, 12);
    g.setColour(ui::text);
    g.setFont(juce::FontOptions(34.0f, juce::Font::bold));
    g.drawText(shown.chord.empty() ? juce::String("-") : juce::String::fromUTF8(shown.chord.c_str()),
               in.removeFromTop(46), juce::Justification::centredLeft);
    // the voices, top to bottom
    static const char* NOTE[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    g.setFont(juce::FontOptions(15.0f));
    auto list = in.removeFromTop(26 * 4 + 4);
    for (size_t i = 0; i < shown.names.size(); ++i) {
        auto r = list.removeFromTop(26);
        auto c = ui::partColour(shown.names[i]);
        g.setColour(c);
        g.fillRoundedRectangle(r.removeFromLeft(6).reduced(0, 5).toFloat(), 2.0f);
        r.removeFromLeft(10);
        g.setColour(ui::text);
        g.drawText(juce::String::fromUTF8(shown.names[i].c_str()), r.removeFromLeft(120), juce::Justification::centredLeft);
        int p = shown.pitches[i];
        g.setColour(c);
        g.drawText(juce::String(NOTE[p % 12]) + juce::String(p / 12 - 1), r, juce::Justification::centredLeft);
    }
    // keyboard C2..C6 with the sung notes lit
    auto kb = in.removeFromBottom(54).toFloat();
    const int lo = 36, hi = 84;
    int whites = 0;
    for (int p = lo; p <= hi; ++p)
        if (juce::MidiMessage::isMidiNoteBlack(p) == false) ++whites;
    float w = kb.getWidth() / (float)whites;
    auto colourOf = [&](int p) -> juce::Colour {
        for (size_t i = 0; i < shown.pitches.size(); ++i)
            if (shown.pitches[i] == p) return ui::partColour(shown.names[i]);
        return juce::Colours::transparentBlack;
    };
    int k = 0;
    std::map<int, float> xOf;
    for (int p = lo; p <= hi; ++p) {
        if (juce::MidiMessage::isMidiNoteBlack(p)) continue;
        juce::Rectangle<float> r(kb.getX() + k * w, kb.getY(), w - 1, kb.getHeight());
        auto c = colourOf(p);
        g.setColour(c.isTransparent() ? juce::Colour(0xffe9e4da) : c);
        g.fillRect(r);
        xOf[p] = r.getX();
        ++k;
    }
    for (int p = lo; p <= hi; ++p) {
        if (!juce::MidiMessage::isMidiNoteBlack(p)) continue;
        juce::Rectangle<float> r(xOf[p - 1] + w * 0.65f, kb.getY(), w * 0.7f, kb.getHeight() * 0.6f);
        auto c = colourOf(p);
        g.setColour(c.isTransparent() ? juce::Colour(0xff1e1d22) : c);
        g.fillRect(r);
    }
}

void LiveEditor::resized() {
    auto side = juce::Rectangle<int>(20, 72, 224, getHeight() - 92);
    auto row = [&](juce::Component& c, int h, int gap) {
        c.setBounds(side.removeFromTop(h));
        side.removeFromTop(gap);
    };
    row(styleLbl, 16, 0);
    row(styleBox, 28, 8);
    row(choirLbl, 16, 0);
    row(choirBox, 28, 2);
    row(amateurBtn, 22, 8);
    row(topBtn, 22, 8);
    row(chanLbl, 16, 0);
    row(chanBox, 28, 12);
    auto vr = side.removeFromTop(28);
    prevVar.setBounds(vr.removeFromLeft(34));
    nextVar.setBounds(vr.removeFromRight(34));
    varLbl.setBounds(vr);
    varLbl.setJustificationType(juce::Justification::centred);
}
