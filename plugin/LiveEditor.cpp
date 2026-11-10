#include "LiveEditor.h"

namespace {
constexpr int kTop = 56, kSide = 268;
}

void TakeChip::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(enabled && hover ? ui::panelHi.brighter(0.05f) : ui::panelHi);
    g.fillRoundedRectangle(r, r.getHeight() / 2.0f);
    g.setColour(enabled ? ui::accent : ui::textDim.withAlpha(0.5f));
    g.fillEllipse(r.getX() + 12.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
    g.setColour(enabled ? ui::text : ui::textDim);
    g.setFont(ui::font(12.5f));
    g.drawText(label, getLocalBounds().withTrimmedLeft(28).withTrimmedRight(10), juce::Justification::centredLeft);
}

void TakeChip::mouseDrag(const juce::MouseEvent& e) {
    if (dragging || !enabled || e.getDistanceFromDragStart() < 4 || !makeFile) return;
    dragging = true;
    auto f = makeFile();
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this);
}

LiveEditor::LiveEditor(LiveProcessor& p) : AudioProcessorEditor(p), proc(p) {
    setLookAndFeel(&look);
    titleLbl.setText("Choir Arranger Live", juce::dontSendNotification);
    titleLbl.setFont(ui::font(16.0f, true));
    addAndMakeVisible(titleLbl);
    hintLbl.setText(U("MIDI FX: играйте аккорды, библиотека на этой дорожке получает голоса"), juce::dontSendNotification);
    hintLbl.setFont(ui::font(12.5f));
    hintLbl.setColour(juce::Label::textColourId, ui::textDim);
    hintLbl.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(hintLbl);
    ChoirLook::sectionLabel(styleLbl, U("Стиль"));
    ChoirLook::sectionLabel(choirLbl, U("Состав хора"));
    ChoirLook::sectionLabel(chanLbl, U("MIDI-каналы"));
    ChoirLook::sectionLabel(regLbl, U("Регистр"));
    for (auto* l : {&styleLbl, &choirLbl, &chanLbl, &regLbl}) addAndMakeVisible(l);
    varLbl.setFont(ui::font(13.0f));
    varLbl.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(varLbl);
    styleBox.addItemList(names::styles(), 1);
    choirBox.addItemList(names::choirs(), 1);
    chanBox.addItemList({U("Все голоса в один канал"), U("Каждый голос в свой канал")}, 1);
    regBox.addItemList({U("Как у хора"), U("На октаву ниже"), U("На две октавы ниже")}, 1);
    for (auto* c : {&styleBox, &choirBox, &chanBox, &regBox}) addAndMakeVisible(c);
    styleA = std::make_unique<CA>(proc.params, "style", styleBox);
    choirA = std::make_unique<CA>(proc.params, "choir", choirBox);
    chanA = std::make_unique<CA>(proc.params, "channels", chanBox);
    regA = std::make_unique<CA>(proc.params, "register", regBox);
    for (auto* b : {&amateurBtn, &topBtn, &velBtn}) addAndMakeVisible(b);
    amateurA = std::make_unique<BA>(proc.params, "amateur", amateurBtn);
    topA = std::make_unique<BA>(proc.params, "topmelody", topBtn);
    velA = std::make_unique<BA>(proc.params, "velocity", velBtn);
    velBtn.setTooltip(U("Каждый голос берёт силу у ближайших к нему клавиш; выключено: у всех голосов средняя"));
    prevVar.getProperties().set("icon", "prev");
    nextVar.getProperties().set("icon", "next");
    clearBtn.getProperties().set("ghost", true);
    for (auto* b : {&prevVar, &nextVar, &clearBtn}) addAndMakeVisible(b);
    auto step = [this](int d) {
        auto* prm = proc.params.getParameter("variant");
        int v = juce::jlimit(0, 99, (int)*proc.params.getRawParameterValue("variant") + d);
        prm->setValueNotifyingHost(prm->convertTo0to1((float)v));
    };
    prevVar.onClick = [step] { step(-1); };
    nextVar.onClick = [step] { step(+1); };
    clearBtn.onClick = [this] { proc.clearRecording(); };
    take.makeFile = [this] { return proc.writeRecordingFile(); };
    take.setTooltip(U("Всё, что вы сыграли с момента очистки. Тяните на дорожку: получится MIDI с партиями хора"));
    addAndMakeVisible(take);
    setSize(820, 500);
    startTimerHz(30);
}

LiveEditor::~LiveEditor() { setLookAndFeel(nullptr); }

void LiveEditor::timerCallback() {
    auto s = proc.snapshot();
    int v = (int)*proc.params.getRawParameterValue("variant");
    varLbl.setText(v == 0 ? U("Основной") : U("Вариант ") + juce::String(v), juce::dontSendNotification);
    int n = proc.recordedChords();
    if (n != shownChords) {
        shownChords = n;
        take.enabled = n > 0;
        take.label = n > 0 ? U("Сыгранное в MIDI: ") + juce::String(n) : U("Сыграйте, потом тяните MIDI отсюда");
        take.repaint();
    }
    if (s.pitches != shown.pitches || s.chord != shown.chord || s.velocities != shown.velocities) {
        shown = s;
        repaint();
    }
}

void LiveEditor::paint(juce::Graphics& g) {
    g.fillAll(ui::bg);
    g.setColour(ui::line);
    g.drawHorizontalLine(kTop - 1, 0.0f, (float)getWidth());
    g.drawVerticalLine(kSide, (float)kTop, (float)getHeight());

    auto in = getLocalBounds().withTrimmedTop(kTop).withTrimmedLeft(kSide).reduced(24, 18);
    in.removeFromBottom(46);   // take row
    g.setColour(ui::text);
    g.setFont(ui::font(44.0f, true));
    g.drawText(shown.chord.empty() ? juce::String::fromUTF8("—") : juce::String::fromUTF8(shown.chord.c_str()),
               in.removeFromTop(58), juce::Justification::centredLeft);
    in.removeFromTop(6);
    // the voices, top to bottom: name, note, velocity bar
    static const char* NOTE[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    auto kbArea = in.removeFromBottom(58);
    in.removeFromBottom(14);
    int rowH = juce::jlimit(20, 28, shown.names.empty() ? 26 : in.getHeight() / (int)std::max<size_t>(4, shown.names.size()));
    for (size_t i = 0; i < shown.names.size(); ++i) {
        auto r = in.removeFromTop(rowH);
        auto c = ui::partColour(shown.names[i]);
        g.setColour(c);
        g.fillEllipse((float)r.getX(), (float)r.getCentreY() - 3.5f, 7.0f, 7.0f);
        r.removeFromLeft(18);
        g.setColour(ui::text);
        g.setFont(ui::font(14.0f));
        g.drawText(juce::String::fromUTF8(shown.names[i].c_str()), r.removeFromLeft(110), juce::Justification::centredLeft);
        int p = shown.pitches[i];
        g.setFont(ui::font(14.0f, true));
        g.drawText(juce::String(NOTE[((p % 12) + 12) % 12]) + juce::String(p / 12 - 1), r.removeFromLeft(52),
                   juce::Justification::centredLeft);
        int vel = i < shown.velocities.size() ? shown.velocities[i] : 90;
        auto bar = r.withSizeKeepingCentre(r.getWidth(), 2).toFloat();
        g.setColour(ui::line);
        g.fillRoundedRectangle(bar, 1.0f);
        g.setColour(c);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * (float)vel / 127.0f), 1.0f);
    }
    // keyboard C1..C6 with the sung notes lit
    auto kb = kbArea.toFloat();
    const int lo = 24, hi = 84;
    int whites = 0;
    for (int p = lo; p <= hi; ++p)
        if (!juce::MidiMessage::isMidiNoteBlack(p)) ++whites;
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
        juce::Rectangle<float> r(kb.getX() + (float)k * w, kb.getY(), w - 1.0f, kb.getHeight());
        auto c = colourOf(p);
        g.setColour(c.isTransparent() ? juce::Colour(0xff2b2b31) : c);
        g.fillRoundedRectangle(r, 2.0f);
        xOf[p] = r.getX();
        ++k;
    }
    for (int p = lo; p <= hi; ++p) {
        if (!juce::MidiMessage::isMidiNoteBlack(p)) continue;
        juce::Rectangle<float> r(xOf[p - 1] + w * 0.66f, kb.getY(), w * 0.66f, kb.getHeight() * 0.58f);
        auto c = colourOf(p);
        g.setColour(c.isTransparent() ? ui::bg : c);
        g.fillRoundedRectangle(r, 1.5f);
    }
}

void LiveEditor::resized() {
    auto b = getLocalBounds();
    auto top = b.removeFromTop(kTop).reduced(18, 12);
    titleLbl.setBounds(top.removeFromLeft(190));
    hintLbl.setBounds(top);
    auto side = b.removeFromLeft(kSide).reduced(20, 16);
    auto row = [&](juce::Component& c, int h, int gap) {
        c.setBounds(side.removeFromTop(h));
        side.removeFromTop(gap);
    };
    row(styleLbl, 14, 5);
    row(styleBox, 30, 14);
    row(choirLbl, 14, 5);
    row(choirBox, 30, 8);
    row(amateurBtn, 22, 14);
    row(regLbl, 14, 5);
    row(regBox, 30, 14);
    row(chanLbl, 14, 5);
    row(chanBox, 30, 14);
    row(topBtn, 22, 8);
    row(velBtn, 22, 8);
    auto vr = side.removeFromBottom(30);
    prevVar.setBounds(vr.removeFromLeft(30));
    nextVar.setBounds(vr.removeFromRight(30));
    varLbl.setBounds(vr);
    auto main = b.reduced(24, 18);
    auto takeRow = main.removeFromBottom(30);
    clearBtn.setBounds(takeRow.removeFromRight(90));
    takeRow.removeFromRight(8);
    take.setBounds(takeRow.removeFromLeft(juce::jmin(320, takeRow.getWidth())));
}
