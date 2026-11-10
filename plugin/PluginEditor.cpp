#include "PluginEditor.h"

namespace {
constexpr int kTop = 56, kSide = 268;
}

// ---------------------------------------------------------------- drag chip
void DragChip::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(hover ? ui::panelHi.brighter(0.05f) : ui::panelHi);
    g.fillRoundedRectangle(r, r.getHeight() / 2.0f);
    g.setColour(colour);
    g.fillEllipse(r.getX() + 11.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
    g.setColour(ui::text);
    g.setFont(ui::font(12.5f));
    auto text = label;
    int room = getWidth() - 26 - (lockable ? 24 : 10);
    if (shortLabel.isNotEmpty() && juce::Font(ui::font(12.5f)).getStringWidthFloat(label) > (float)room) text = shortLabel;
    g.drawText(text, getLocalBounds().withTrimmedLeft(26).withTrimmedRight(lockable ? 24 : 8), juce::Justification::centredLeft);
    if (lockable) {
        auto z = lockZone().toFloat();
        auto b = juce::Rectangle<float>(z.getCentreX() - 5.0f, z.getCentreY() - 1.0f, 9.0f, 7.0f);
        g.setColour(locked ? ui::accent : ui::textDim.withAlpha(hover ? 0.9f : 0.35f));
        g.fillRoundedRectangle(b, 1.5f);
        juce::Path shackle;
        shackle.addCentredArc(b.getCentreX(), b.getY(), 3.0f, 3.6f, 0.0f, -juce::MathConstants<float>::halfPi,
                              juce::MathConstants<float>::halfPi, true);
        g.strokePath(shackle, juce::PathStrokeType(1.4f));
    }
    if (locked) {
        g.setColour(ui::accent.withAlpha(0.8f));
        g.drawRoundedRectangle(r, r.getHeight() / 2.0f, 1.0f);
    }
}

void DragChip::mouseDrag(const juce::MouseEvent& e) {
    if (dragging || e.getDistanceFromDragStart() < 4 || !makeFile) return;
    dragging = true;
    auto f = makeFile();
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this);
}

void DragChip::mouseUp(const juce::MouseEvent& e) {
    // only the padlock locks: a sloppy drag over the name must not lock a part by accident
    if (!dragging && lockable && onLock && e.getDistanceFromDragStart() < 4 && lockZone().contains(e.getPosition())) onLock();
    dragging = false;
}

// ---------------------------------------------------------------- saved variants
juce::Rectangle<float> SlotBar::pill(int i) const {
    return {(float)i * 30.0f, (float)getHeight() / 2.0f - 12.0f, 24.0f, 24.0f};
}

int SlotBar::indexAt(juce::Point<int> p) const {
    for (int i = 0; i < count; ++i)
        if (pill(i).contains(p.toFloat())) return i;
    return -1;
}

void SlotBar::paint(juce::Graphics& g) {
    if (count == 0) {
        g.setColour(ui::textDim);
        g.setFont(ui::font(12.0f));
        g.drawText(U("нажмите +, чтобы запомнить вариант"), getLocalBounds(), juce::Justification::centredLeft);
        return;
    }
    for (int i = 0; i < count; ++i) {
        auto r = pill(i);
        bool on = i == active;
        g.setColour(on ? ui::accent : (i == hoverIndex ? ui::panelHi.brighter(0.08f) : ui::panelHi));
        g.fillEllipse(r);
        g.setColour(on ? ui::onAccent : ui::text);
        g.setFont(ui::font(12.0f, on));
        g.drawText(juce::String(i + 1), r, juce::Justification::centred);
    }
}

void SlotBar::mouseUp(const juce::MouseEvent& e) {
    int i = indexAt(e.getPosition());
    if (i < 0) return;
    if (e.mods.isPopupMenu() || e.mods.isAltDown()) {
        if (onRemove) onRemove(i);
    } else if (onPick) {
        onPick(i);
    }
}

// ---------------------------------------------------------------- editor
ChoirEditor::ChoirEditor(ChoirProcessor& p) : AudioProcessorEditor(p), proc(p) {
    setLookAndFeel(&look);
    juce::LookAndFeel::setDefaultLookAndFeel(&look);
    setWantsKeyboardFocus(true);

    titleLbl.setText("Choir Arranger", juce::dontSendNotification);
    titleLbl.setFont(ui::font(16.0f, true));
    addAndMakeVisible(titleLbl);
    sourceLbl.setFont(ui::font(12.5f));
    sourceLbl.setColour(juce::Label::textColourId, ui::textDim);
    sourceLbl.setJustificationType(juce::Justification::centredLeft);
    sourceLbl.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(sourceLbl);

    auto icon = [](juce::Button& b, const char* name, bool ghost) {
        b.getProperties().set("icon", name);
        if (ghost) b.getProperties().set("ghost", true);
    };
    for (auto* b : {&captureBtn, &openBtn, &pasteBtn, &refreshBtn, &playBtn, &exportBtn, &scoreBtn, &prevVarBtn,
                    &nextVarBtn, &saveVarBtn})
        addAndMakeVisible(b);
    icon(captureBtn, "rec", true);
    icon(openBtn, "open", true);
    icon(pasteBtn, "paste", true);
    icon(refreshBtn, "refresh", true);
    icon(playBtn, "play", false);
    icon(exportBtn, "export", false);
    icon(scoreBtn, "export", false);
    icon(prevVarBtn, "prev", false);
    icon(nextVarBtn, "next", false);
    icon(saveVarBtn, "plus", false);
    captureBtn.getProperties().set("danger", true);
    captureBtn.onClick = [this] { proc.setCapture(!proc.isCapturing()); timerCallback(); };
    captureBtn.setTooltip(U("Положите регион с аккордами на эту дорожку, нажмите «Захват» и запустите воспроизведение"));
    openBtn.onClick = [this] { openFile(); };
    openBtn.setTooltip(U("Открыть MIDI-файл"));
    pasteBtn.onClick = [this] { paste(); };
    pasteBtn.setTooltip(U("Вставить MIDI из буфера обмена (Cmd+V): скопируйте регион в Logic и вставьте сюда"));
    refreshBtn.onClick = [this] { proc.refreshAll(); };
    refreshBtn.setTooltip(U("Пересчитать всё заново по входу: снять закрепления, вернуться к основному варианту"));
    playBtn.onClick = [this] { proc.setPreview(!proc.isPreviewing()); timerCallback(); };
    exportBtn.onClick = [this] { exportAll(); };
    exportBtn.setTooltip(U("Сохранить все партии одним MIDI-файлом"));
    scoreBtn.onClick = [this] { exportScore(); };
    scoreBtn.setTooltip(U("Партитура MusicXML для MuseScore, Sibelius, Dorico, Finale"));
    prevVarBtn.onClick = [this] { proc.stepVariant(-1); };
    nextVarBtn.onClick = [this] { proc.stepVariant(+1); };
    nextVarBtn.setTooltip(U("Другой вариант: те же настройки, другое голосоведение"));
    prevVarBtn.setTooltip(U("Предыдущий вариант"));
    saveVarBtn.onClick = [this] { proc.saveSlot(); refresh(); };
    saveVarBtn.setTooltip(U("Запомнить этот вариант в стиле"));

    ChoirLook::sectionLabel(inputLbl, U("Вход"));
    ChoirLook::sectionLabel(styleLbl, U("Стиль"));
    ChoirLook::sectionLabel(choirLbl, U("Состав хора"));
    ChoirLook::sectionLabel(textureLbl, U("Фактура"));
    ChoirLook::sectionLabel(outOfModeLbl, U("Аккорды вне лада"));
    ChoirLook::sectionLabel(complexityLbl, U("Сложность"));
    ChoirLook::sectionLabel(voicesLbl, U("По голосам"));
    ChoirLook::sectionLabel(variantsLbl, U("Варианты"));
    ChoirLook::sectionLabel(soundLbl, U("Звук прослушивания"));
    for (auto* l : {&inputLbl, &styleLbl, &choirLbl, &textureLbl, &outOfModeLbl, &complexityLbl, &voicesLbl, &variantsLbl,
                    &soundLbl})
        addAndMakeVisible(l);

    for (auto* c : {&styleBox, &textureBox, &outOfModeBox, &soundBox, &choirBox, &inputBox}) addAndMakeVisible(c);
    styleBox.addItemList(ChoirProcessor::styleNames(), 1);
    textureBox.addItemList({U("Унисон"), U("Органум в кварту"), U("Органум в квинту")}, 1);
    outOfModeBox.addItemList({U("Подменять ладовым"), U("Пропускать")}, 1);
    soundBox.addItemList({U("Пианино"), U("Хор")}, 1);
    choirBox.addItemList(ChoirProcessor::choirNames(), 1);
    inputBox.addItemList({U("Авто"), U("Аккорды"), U("Гармонизовать мелодию")}, 1);
    inputBox.setTooltip(U("Авто: если на входе одна мелодия без аккордов, плагин подберёт аккорды сам"));
    styleA = std::make_unique<CA>(proc.params, "style", styleBox);
    textureA = std::make_unique<CA>(proc.params, "texture", textureBox);
    outOfModeA = std::make_unique<CA>(proc.params, "outofmode", outOfModeBox);
    soundA = std::make_unique<CA>(proc.params, "sound", soundBox);
    choirA = std::make_unique<CA>(proc.params, "choir", choirBox);
    inputA = std::make_unique<CA>(proc.params, "input", inputBox);
    for (auto* b : {&octavistsBtn, &mixedBtn, &amateurBtn, &majorFinalBtn}) addAndMakeVisible(b);
    octA = std::make_unique<BA>(proc.params, "octavists", octavistsBtn);
    mixedA = std::make_unique<BA>(proc.params, "mixed", mixedBtn);
    amateurA = std::make_unique<BA>(proc.params, "amateur", amateurBtn);
    majorFinalA = std::make_unique<BA>(proc.params, "majorfinal", majorFinalBtn);
    majorFinalBtn.setTooltip(U("С 6-го уровня минорная пьеса заканчивается мажорным аккордом. Выключите, чтобы оставить минор"));

    auto valueLabel = [this](juce::Label& l) {
        l.setFont(ui::font(13.0f, true));
        l.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(l);
    };
    complexity.setSliderStyle(juce::Slider::LinearHorizontal);
    complexity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(complexity);
    complexityA = std::make_unique<SA>(proc.params, "complexity", complexity);
    valueLabel(complexityVal);
    const char* names[4] = {"S", "A", "T", "B"};
    const char* full[4] = {"Soprano", "Alto", "Tenor", "Bass"};
    for (int i = 0; i < 4; ++i) {
        voice[i].setSliderStyle(juce::Slider::LinearHorizontal);
        voice[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        voice[i].getProperties().set("colour", (juce::int64)ui::partColour(full[i]).getARGB());
        voice[i].setTooltip(U("Сложность голоса; крайнее левое положение (=) означает «как общая»"));
        addAndMakeVisible(voice[i]);
        voiceA[i] = std::make_unique<SA>(proc.params, juce::String("voice") + names[i], voice[i]);
        voiceLbl[i].setText(names[i], juce::dontSendNotification);
        voiceLbl[i].setFont(ui::font(12.5f, true));
        voiceLbl[i].setColour(juce::Label::textColourId, ui::partColour(full[i]));
        addAndMakeVisible(voiceLbl[i]);
        valueLabel(voiceVal[i]);
        voiceVal[i].setColour(juce::Label::textColourId, ui::textDim);
    }
    styleBox.onChange = [this] { updateStyleControls(); };
    choirBox.onChange = [this] { updateStyleControls(); };

    variantLbl.setFont(ui::font(13.0f));
    variantLbl.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(variantLbl);
    slots.onPick = [this](int i) { proc.loadSlot(i); };
    slots.onRemove = [this](int i) { proc.removeSlot(i); refresh(); };
    slots.setTooltip(U("Запомненные варианты этого стиля. Щелчок возвращает вариант, правый щелчок удаляет"));
    addAndMakeVisible(slots);

    roll.setEmptyText(U("Перетащите сюда MIDI-файл с аккордами или мелодией,\nвставьте скопированный регион (Cmd+V)\nили нажмите «Захват» и запустите воспроизведение"));
    roll.makeDragFile = [this] { return proc.writePartFile(-1); };
    addAndMakeVisible(roll);
    allChip = std::make_unique<DragChip>();
    allChip->label = U("Все партии");
    allChip->colour = ui::accent;
    allChip->makeFile = [this] { return proc.writePartFile(-1); };
    allChip->setTooltip(U("Тяните на дорожки: все партии одним файлом"));
    addAndMakeVisible(*allChip);
    scoreChip = std::make_unique<DragChip>();
    scoreChip->label = U("Ноты");
    scoreChip->colour = ui::textDim;
    scoreChip->makeFile = [this] { return proc.writeScoreFile(); };
    scoreChip->setTooltip(U("Тяните в Finder или в нотный редактор: партитура MusicXML"));
    addAndMakeVisible(*scoreChip);

    report.setFont(ui::font(12.0f));
    report.setJustificationType(juce::Justification::topLeft);
    report.setColour(juce::Label::textColourId, ui::textDim);
    report.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(report);

    proc.onResultChanged = [this] { refresh(); };
    setResizable(true, true);
    setResizeLimits(960, 640, 1900, 1300);
    setSize(1120, 720);
    updateStyleControls();
    refresh();
    startTimerHz(30);
}

ChoirEditor::~ChoirEditor() {
    proc.onResultChanged = nullptr;
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void ChoirEditor::updateStyleControls() {
    using choir::Style;
    auto st = (Style)juce::jmax(0, styleBox.getSelectedItemIndex());
    // chant and the rule-based textures use one overall level: per-voice sliders are hidden there
    bool chant = st == Style::Gregorian || st == Style::Byzantine || st == Style::Epic || st == Style::SacredHarp ||
                 st == Style::Bulgarian || st == Style::Georgian || st == Style::Tintinnabuli ||
                 st == Style::CloseHarmony || st == Style::AcappellaPop || st == Style::Tenore || st == Style::Soviet;
    bool modal = st == Style::Gregorian || st == Style::Byzantine || st == Style::Znamenny;
    bool voiceLeading = st == Style::Bach || st == Style::Obikhod || st == Style::Renaissance ||
                        st == Style::Contemporary || st == Style::Gospel || st == Style::Partes ||
                        st == Style::Spiritual || st == Style::Anglican;
    octavistsBtn.setVisible(st == Style::Obikhod || st == Style::Znamenny);
    textureLbl.setVisible(st == Style::Gregorian);
    textureBox.setVisible(st == Style::Gregorian);
    outOfModeLbl.setVisible(modal);
    outOfModeBox.setVisible(modal);
    mixedBtn.setVisible(st == Style::Gregorian || st == Style::Byzantine);
    majorFinalBtn.setVisible(st == Style::Soviet);
    choirLbl.setVisible(voiceLeading);
    choirBox.setVisible(voiceLeading);
    amateurBtn.setVisible(voiceLeading);
    voicesLbl.setVisible(!chant);
    const char* names[4] = {"S", "A", "T", "B"};
    const char* bb[4] = {"T", "L", "Br", "B"};      // barbershop names its voices differently
    for (int i = 0; i < 4; ++i) {
        voice[i].setVisible(!chant);
        voiceLbl[i].setVisible(!chant);
        voiceVal[i].setVisible(!chant);
        voiceLbl[i].setText(st == Style::Barbershop ? bb[i] : names[i], juce::dontSendNotification);
    }
    resized();
}

void ChoirEditor::refresh() {
    const auto& r = proc.result();
    roll.setData(&r.parts, &proc.input(), proc.meta());
    chips.clear();
    for (size_t i = 0; i < r.parts.size(); ++i) {
        auto* c = chips.add(new DragChip());
        c->label = juce::String::fromUTF8(r.parts[i].name.c_str());
        // "Soprano 2" -> "S2", "Baritone" -> "Br"
        auto words = juce::StringArray::fromTokens(c->label, " ", "");
        c->shortLabel = words[0].startsWith("Bari") ? juce::String("Br") : words[0].startsWith("Octav") ? juce::String("Oct")
                                                                                                           : words[0].substring(0, 1);
        if (words.size() > 1) c->shortLabel << (words[1].startsWith("I") ? " " : "") << words[1];
        c->colour = ui::partColour(r.parts[i].name);
        int idx = (int)i;
        c->makeFile = [this, idx] { return proc.writePartFile(idx); };
        c->lockable = true;
        c->locked = proc.isLocked(c->label);
        auto nm = c->label;
        c->onLock = [this, nm] { proc.toggleLock(nm); };
        c->setTooltip(U("Тяните на дорожку. Замок справа закрепляет партию: при смене сложности и вариантов она не меняется"));
        addAndMakeVisible(c);
    }
    allChip->setVisible(!r.parts.empty());
    scoreChip->setVisible(!r.parts.empty());
    report.setText(proc.input().empty() ? juce::String() : juce::String::fromUTF8(r.report().c_str()),
                   juce::dontSendNotification);
    sourceLbl.setText(proc.input().empty() ? U("нет входа") : proc.inputSource(), juce::dontSendNotification);
    bool have = !r.parts.empty();
    for (auto* b : {&exportBtn, &scoreBtn, &playBtn, &nextVarBtn, &saveVarBtn}) b->setEnabled(have);
    int var = (int)*proc.params.getRawParameterValue("variant");
    variantLbl.setText(var == 0 ? U("Основной") : U("Вариант ") + juce::String(var), juce::dontSendNotification);
    prevVarBtn.setEnabled(var > 0);
    slots.count = proc.numSlots();
    slots.active = proc.currentSlot();
    saveVarBtn.setEnabled(have && slots.active < 0);
    slots.repaint();
    resized();
    repaint();
}

void ChoirEditor::timerCallback() {
    bool cap = proc.isCapturing();
    captureBtn.setToggleState(cap, juce::dontSendNotification);
    captureBtn.setButtonText(cap ? (proc.captureHasData() ? U("Запись") : U("Жду play")) : U("Захват"));
    bool pv = proc.isPreviewing();
    playBtn.setToggleState(pv, juce::dontSendNotification);
    playBtn.setButtonText(pv ? U("Стоп") : U("Слушать"));
    playBtn.getProperties().set("icon", pv ? "stop" : "play");
    roll.setPlayhead(pv ? proc.previewPositionTicks() : -1.0);
    complexityVal.setText(juce::String((int)complexity.getValue()), juce::dontSendNotification);
    for (int i = 0; i < 4; ++i)
        voiceVal[i].setText(voice[i].getValue() < 0 ? juce::String("=") : juce::String((int)voice[i].getValue()),
                            juce::dontSendNotification);
}

void ChoirEditor::paint(juce::Graphics& g) {
    g.fillAll(ui::bg);
    g.setColour(ui::line);
    g.drawHorizontalLine(kTop - 1, 0.0f, (float)getWidth());
    g.drawVerticalLine(kSide, (float)kTop, (float)getHeight());
    if (dropHover) {
        g.setColour(ui::accent);
        g.drawRoundedRectangle(roll.getBounds().toFloat().expanded(2), 11.0f, 1.5f);
    }
}

void ChoirEditor::resized() {
    auto b = getLocalBounds();
    auto top = b.removeFromTop(kTop).reduced(18, 12);
    titleLbl.setBounds(top.removeFromLeft(128));
    scoreBtn.setBounds(top.removeFromRight(84));
    top.removeFromRight(6);
    exportBtn.setBounds(top.removeFromRight(84));
    top.removeFromRight(6);
    playBtn.setBounds(top.removeFromRight(108));
    top.removeFromRight(14);
    refreshBtn.setBounds(top.removeFromRight(106));
    pasteBtn.setBounds(top.removeFromRight(100));
    openBtn.setBounds(top.removeFromRight(96));
    captureBtn.setBounds(top.removeFromRight(100));
    sourceLbl.setBounds(top.withTrimmedLeft(8).withTrimmedRight(8));

    auto side = b.removeFromLeft(kSide).reduced(20, 16);
    auto row = [&](juce::Component& c, int h, int gap) {
        c.setBounds(side.removeFromTop(h));
        side.removeFromTop(gap);
    };
    row(inputLbl, 14, 5);
    row(inputBox, 30, 14);
    row(styleLbl, 14, 5);
    row(styleBox, 30, 14);
    if (choirBox.isVisible()) {
        row(choirLbl, 14, 5);
        row(choirBox, 30, 8);
        row(amateurBtn, 22, 14);
    }
    if (octavistsBtn.isVisible()) row(octavistsBtn, 22, 14);
    if (textureBox.isVisible()) {
        row(textureLbl, 14, 5);
        row(textureBox, 30, 14);
    }
    if (outOfModeBox.isVisible()) {
        row(outOfModeLbl, 14, 5);
        row(outOfModeBox, 30, 10);
    }
    if (mixedBtn.isVisible()) row(mixedBtn, 22, 14);
    if (majorFinalBtn.isVisible()) row(majorFinalBtn, 22, 14);

    // bottom up: sound, then variants
    auto snd = side.removeFromBottom(49);
    soundLbl.setBounds(snd.removeFromTop(14));
    snd.removeFromTop(5);
    soundBox.setBounds(snd);
    side.removeFromBottom(16);
    auto slotRow = side.removeFromBottom(28);
    saveVarBtn.setBounds(slotRow.removeFromRight(28));
    slotRow.removeFromRight(6);
    slots.setBounds(slotRow);
    side.removeFromBottom(8);
    auto varRow = side.removeFromBottom(30);
    prevVarBtn.setBounds(varRow.removeFromLeft(30));
    nextVarBtn.setBounds(varRow.removeFromRight(30));
    variantLbl.setBounds(varRow);
    side.removeFromBottom(5);
    variantsLbl.setBounds(side.removeFromBottom(14));

    auto crow = side.removeFromTop(14);
    complexityVal.setBounds(crow.removeFromRight(30));
    complexityLbl.setBounds(crow);
    side.removeFromTop(4);
    complexity.setBounds(side.removeFromTop(22));
    side.removeFromTop(12);
    if (voicesLbl.isVisible()) {
        voicesLbl.setBounds(side.removeFromTop(14));
        side.removeFromTop(4);
        for (int i = 0; i < 4; ++i) {
            auto vr = side.removeFromTop(22);
            voiceLbl[i].setBounds(vr.removeFromLeft(24));
            voiceVal[i].setBounds(vr.removeFromRight(24));
            voice[i].setBounds(vr.reduced(4, 0));
        }
    }

    auto main = b.reduced(18, 0).withTrimmedTop(16).withTrimmedBottom(14);
    auto rep = main.removeFromBottom(proc.result().issues.size() > 2 ? 140 : 108);
    report.setBounds(rep.withTrimmedTop(10));
    auto chipRow = main.removeFromBottom(42).withTrimmedTop(12);
    if (scoreChip->isVisible()) {
        scoreChip->setBounds(chipRow.removeFromRight(74));
        chipRow.removeFromRight(8);
    }
    if (allChip->isVisible()) {
        allChip->setBounds(chipRow.removeFromLeft(112));
        chipRow.removeFromLeft(8);
    }
    int cw = chips.isEmpty() ? 104 : juce::jlimit(62, 124, (chipRow.getWidth() - 6 * chips.size()) / chips.size());
    for (auto* c : chips) {
        c->setBounds(chipRow.removeFromLeft(cw));
        chipRow.removeFromLeft(6);
    }
    roll.setBounds(main);
}

bool ChoirEditor::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress('v', juce::ModifierKeys::commandModifier, 0)) {
        paste();
        return true;
    }
    return false;   // everything else goes on to the host (space bar, transport keys)
}

void ChoirEditor::paste() {
    juce::String msg;
    if (!proc.pasteFromClipboard(msg)) showError(msg);
}

// Any file is welcome: regions dragged from a DAW may arrive with an unusual name, or as a file
// promise that only becomes a real file on drop. The content (MThd header) decides.
bool ChoirEditor::isInterestedInFileDrag(const juce::StringArray& files) { return !files.isEmpty(); }

void ChoirEditor::showError(const juce::String& err) {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Choir Arranger", err);
}

void ChoirEditor::filesDropped(const juce::StringArray& files, int, int) {
    dropHover = false;
    repaint();
    for (auto& path : files) {
        juce::File f(path);
        if (f.getFileName() == "choir-drag-report.txt") {
            // nothing usable came with the drag: show what did, and keep a copy for sending to the developer
            auto copy = juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("Choir Arranger drag report.txt");
            f.copyFileTo(copy);
            showError(U("Не получилось взять MIDI из перетащенного. Пока пользуйтесь «Захватом» или "
                        "File > Export > Selection as MIDI File.\n\nОтчёт сохранён на рабочий стол: "
                        "«Choir Arranger drag report.txt».\n\n") +
                      f.loadFileAsString().substring(0, 900));
            return;
        }
        if (f.isDirectory()) {
            for (auto& c : f.findChildFiles(juce::File::findFiles, true))
                if (ChoirProcessor::isMidiFile(c)) { f = c; break; }
        }
        if (!f.existsAsFile() || !ChoirProcessor::isMidiFile(f)) continue;
        juce::String err;
        if (!proc.loadMidiFile(f, err)) showError(err);
        return;
    }
    showError(U("Не нашёл MIDI в том, что перетащили. Если это регион из Logic, "
                "сделайте File > Export > Selection as MIDI File или включите «Захват»."));
}

void ChoirEditor::openFile() {
    chooser = std::make_unique<juce::FileChooser>(U("Файл с аккордами или мелодией"), juce::File(), "*.mid;*.midi;*.smf;*");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (!f.existsAsFile()) return;
                             juce::String err;
                             if (!proc.loadMidiFile(f, err)) showError(err);
                         });
}

void ChoirEditor::exportScore() {
    auto tmp = proc.writeScoreFile();
    chooser = std::make_unique<juce::FileChooser>(U("Сохранить ноты"),
                                                  juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                                                      .getChildFile("Choir score.musicxml"),
                                                  "*.musicxml");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                         [tmp](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (f != juce::File()) tmp.copyFileTo(f.withFileExtension("musicxml"));
                         });
}

void ChoirEditor::exportAll() {
    auto tmp = proc.writePartFile(-1);
    chooser = std::make_unique<juce::FileChooser>(U("Сохранить партии"),
                                                  juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                                                      .getChildFile("Choir parts.mid"),
                                                  "*.mid");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                         [tmp](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (f != juce::File()) tmp.copyFileTo(f.withFileExtension("mid"));
                         });
}
