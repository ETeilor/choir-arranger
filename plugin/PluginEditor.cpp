#include "PluginEditor.h"

// ---------------------------------------------------------------- drag chip
void DragChip::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(hover ? ui::panelHi.brighter(0.06f) : ui::panelHi);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(colour);
    g.fillRoundedRectangle(r.removeFromLeft(6.0f).reduced(0, 6), 2.0f);
    g.setColour(ui::text);
    g.setFont(juce::FontOptions(13.0f));
    auto text = label;
    if (shortLabel.isNotEmpty() &&
        juce::Font(juce::FontOptions(13.0f)).getStringWidthFloat(label) > getWidth() - 40)
        text = shortLabel;
    g.drawText(text, getLocalBounds().reduced(16, 0), juce::Justification::centredLeft);
    g.setColour(ui::textDim);
    g.drawText(juce::String::fromUTF8("⤴"), getLocalBounds().reduced(10, 0), juce::Justification::centredRight);
    if (lockable && (locked || hover)) {
        // padlock in the top-right corner: solid when locked, faint on hover
        auto b = juce::Rectangle<float>((float)getWidth() - 34.0f, 7.0f, 9.0f, 7.0f);
        g.setColour(locked ? ui::accent : ui::textDim.withAlpha(0.6f));
        g.fillRoundedRectangle(b, 1.5f);
        juce::Path shackle;
        shackle.addCentredArc(b.getCentreX(), b.getY(), 3.0f, 3.5f, 0.0f, -juce::MathConstants<float>::halfPi,
                              juce::MathConstants<float>::halfPi, true);
        g.strokePath(shackle, juce::PathStrokeType(1.4f));
    }
    if (locked) {
        g.setColour(ui::accent);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 6.0f, 1.2f);
    }
}

void DragChip::mouseDrag(const juce::MouseEvent& e) {
    if (dragging || e.getDistanceFromDragStart() < 4 || !makeFile) return;
    dragging = true;
    auto f = makeFile();
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this);
}

// ---------------------------------------------------------------- editor
ChoirEditor::ChoirEditor(ChoirProcessor& p) : AudioProcessorEditor(p), proc(p) {
    setLookAndFeel(&look);
    juce::LookAndFeel::setDefaultLookAndFeel(&look);

    for (auto* b : {&captureBtn, &openBtn, &playBtn, &exportBtn, &scoreBtn}) addAndMakeVisible(b);
    scoreBtn.onClick = [this] { exportScore(); };
    scoreBtn.setTooltip(U("Партитура для MuseScore, Sibelius, Dorico, Finale"));
    captureBtn.getProperties().set("danger", true);
    captureBtn.setClickingTogglesState(false);
    captureBtn.onClick = [this] { proc.setCapture(!proc.isCapturing()); timerCallback(); };
    openBtn.onClick = [this] { openFile(); };
    playBtn.onClick = [this] { proc.setPreview(!proc.isPreviewing()); timerCallback(); };
    exportBtn.onClick = [this] { exportAll(); };

    auto label = [this](juce::Label& l, const juce::String& t) {
        l.setText(t, juce::dontSendNotification);
        l.setFont(juce::FontOptions(12.0f));
        l.setColour(juce::Label::textColourId, ui::textDim);
        addAndMakeVisible(l);
    };
    label(styleLbl, U("Стиль"));
    label(textureLbl, U("Фактура"));
    label(outOfModeLbl, U("Аккорды вне лада"));
    label(complexityLbl, U("Сложность"));
    label(voicesLbl, U("По голосам (= общая)"));
    label(soundLbl, U("Звук прослушивания"));
    label(choirLbl, U("Состав хора"));
    label(inputLbl, U("Что на входе"));
    variantLbl.setFont(juce::FontOptions(12.5f));
    variantLbl.setColour(juce::Label::textColourId, ui::textDim);
    variantLbl.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(variantLbl);
    for (auto* b : {&prevVarBtn, &nextVarBtn}) addAndMakeVisible(b);
    prevVarBtn.onClick = [this] { proc.stepVariant(-1); };
    nextVarBtn.onClick = [this] { proc.stepVariant(+1); };
    nextVarBtn.setTooltip(U("Те же настройки, другое голосоведение"));

    for (auto* c : {&styleBox, &textureBox, &outOfModeBox, &soundBox, &choirBox, &inputBox}) addAndMakeVisible(c);
    choirBox.addItemList(ChoirProcessor::choirNames(), 1);
    inputBox.addItemList({U("Авто"), U("Аккорды"), U("Гармонизовать мелодию")}, 1);
    choirA = std::make_unique<CA>(proc.params, "choir", choirBox);
    inputA = std::make_unique<CA>(proc.params, "input", inputBox);
    inputBox.setTooltip(U("Авто: если на входе одна мелодия без аккордов, плагин подберёт аккорды сам"));
    addAndMakeVisible(amateurBtn);
    amateurA = std::make_unique<BA>(proc.params, "amateur", amateurBtn);
    choirBox.onChange = [this] { updateStyleControls(); };
    styleBox.addItemList(ChoirProcessor::styleNames(), 1);
    soundBox.addItemList({U("Пианино"), U("Хор")}, 1);
    textureBox.addItemList({U("Унисон"), U("Органум в кварту"), U("Органум в квинту")}, 1);
    outOfModeBox.addItemList({U("Подменять ладовым"), U("Пропускать")}, 1);
    styleA = std::make_unique<CA>(proc.params, "style", styleBox);
    textureA = std::make_unique<CA>(proc.params, "texture", textureBox);
    outOfModeA = std::make_unique<CA>(proc.params, "outofmode", outOfModeBox);
    soundA = std::make_unique<CA>(proc.params, "sound", soundBox);
    addAndMakeVisible(octavistsBtn);
    addAndMakeVisible(mixedBtn);
    octA = std::make_unique<BA>(proc.params, "octavists", octavistsBtn);
    mixedA = std::make_unique<BA>(proc.params, "mixed", mixedBtn);

    complexity.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    complexity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(complexity);
    complexityA = std::make_unique<SA>(proc.params, "complexity", complexity);
    const char* names[4] = {"S", "A", "T", "B"};
    const char* full[4] = {"Soprano", "Alto", "Tenor", "Bass"};
    for (int i = 0; i < 4; ++i) {
        voice[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        voice[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        voice[i].getProperties().set("colour", (juce::int64)ui::partColour(full[i]).getARGB());
        voice[i].setTooltip(U("Сложность голоса; крайнее левое положение = общая"));
        addAndMakeVisible(voice[i]);
        voiceA[i] = std::make_unique<SA>(proc.params, juce::String("voice") + names[i], voice[i]);
        voiceLbl[i].setText(names[i], juce::dontSendNotification);
        voiceLbl[i].setJustificationType(juce::Justification::centred);
        voiceLbl[i].setColour(juce::Label::textColourId, ui::partColour(full[i]));
        addAndMakeVisible(voiceLbl[i]);
    }
    styleBox.onChange = [this] { updateStyleControls(); };

    roll.setEmptyText(
        U("Перетащите сюда MIDI-файл с аккордами\nили нажмите «Захват» и запустите воспроизведение региона на этой дорожке"));
    roll.makeDragFile = [this] { return proc.writePartFile(-1); };
    addAndMakeVisible(roll);
    allChip = std::make_unique<DragChip>();
    allChip->label = U("Все партии");
    allChip->colour = ui::accent;
    allChip->makeFile = [this] { return proc.writePartFile(-1); };
    addAndMakeVisible(*allChip);
    scoreChip = std::make_unique<DragChip>();
    scoreChip->label = U("Ноты");
    scoreChip->colour = ui::textDim;
    scoreChip->makeFile = [this] { return proc.writeScoreFile(); };
    addAndMakeVisible(*scoreChip);

    report.setFont(juce::FontOptions(12.5f));
    report.setJustificationType(juce::Justification::topLeft);
    report.setColour(juce::Label::textColourId, ui::textDim);
    addAndMakeVisible(report);
    sourceLbl.setFont(juce::FontOptions(12.5f));
    sourceLbl.setColour(juce::Label::textColourId, ui::textDim);
    sourceLbl.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(sourceLbl);
    hintLbl.setFont(juce::FontOptions(12.0f));
    hintLbl.setColour(juce::Label::textColourId, ui::textDim);
    hintLbl.setText(U("Тяните партию или всю нотную ленту на дорожку"), juce::dontSendNotification);
    addAndMakeVisible(hintLbl);

    proc.onResultChanged = [this] { refresh(); };
    setResizable(true, true);
    setResizeLimits(940, 660, 1900, 1300);
    setSize(1120, 740);
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
    // chant and the rule-based textures use one overall level: per-voice knobs are hidden there
    bool chant = st == Style::Gregorian || st == Style::Byzantine || st == Style::Epic || st == Style::SacredHarp ||
                 st == Style::Bulgarian || st == Style::Georgian || st == Style::Tintinnabuli ||
                 st == Style::CloseHarmony || st == Style::AcappellaPop || st == Style::Tenore || st == Style::Soviet;
    bool modal = st == Style::Gregorian || st == Style::Byzantine || st == Style::Znamenny;
    octavistsBtn.setVisible(st == Style::Obikhod || st == Style::Znamenny);
    textureLbl.setVisible(st == Style::Gregorian);
    textureBox.setVisible(st == Style::Gregorian);
    outOfModeLbl.setVisible(modal);
    outOfModeBox.setVisible(modal);
    mixedBtn.setVisible(st == Style::Gregorian || st == Style::Byzantine);
    voicesLbl.setVisible(!chant);
    for (int i = 0; i < 4; ++i) {
        voice[i].setVisible(!chant);
        voiceLbl[i].setVisible(!chant);
    }
    bool voiceLeading = st == Style::Bach || st == Style::Obikhod || st == Style::Renaissance ||
                        st == Style::Contemporary || st == Style::Gospel || st == Style::Partes ||
                        st == Style::Spiritual || st == Style::Anglican;
    choirLbl.setVisible(voiceLeading);
    choirBox.setVisible(voiceLeading);
    amateurBtn.setVisible(voiceLeading);
    // barbershop names its voices differently
    const char* names[4] = {"S", "A", "T", "B"};
    const char* bb[4] = {"T", "L", "Br", "B"};
    for (int i = 0; i < 4; ++i)
        voiceLbl[i].setText(st == Style::Barbershop ? bb[i] : names[i], juce::dontSendNotification);
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
        c->onClick = [this, nm] { proc.toggleLock(nm); };
        c->setTooltip(U("Тяните на дорожку. Щелчок закрепляет партию: при смене сложности и вариантов она не меняется"));
        addAndMakeVisible(c);
    }
    allChip->setVisible(!r.parts.empty());
    report.setText(proc.input().empty() ? juce::String() : juce::String::fromUTF8(r.report().c_str()),
                   juce::dontSendNotification);
    sourceLbl.setText(proc.input().empty() ? U("Нет входа")
                                           : juce::String::fromUTF8("Вход: ") + proc.inputSource(),
                      juce::dontSendNotification);
    exportBtn.setEnabled(!r.parts.empty());
    int var = (int)*proc.params.getRawParameterValue("variant");
    variantLbl.setText(var == 0 ? U("Основной вариант") : U("Вариант ") + juce::String(var), juce::dontSendNotification);
    prevVarBtn.setEnabled(var > 0);
    nextVarBtn.setEnabled(!r.parts.empty());
    scoreBtn.setEnabled(!r.parts.empty());
    scoreChip->setVisible(!r.parts.empty());
    playBtn.setEnabled(!r.parts.empty());
    resized();
    repaint();
}

void ChoirEditor::timerCallback() {
    bool cap = proc.isCapturing();
    captureBtn.setToggleState(cap, juce::dontSendNotification);
    captureBtn.setButtonText(cap ? (proc.captureHasData() ? juce::String::fromUTF8("Запись…") : juce::String::fromUTF8("Ждёт воспроизведения"))
                                 : juce::String::fromUTF8("Захват"));
    bool pv = proc.isPreviewing();
    playBtn.setToggleState(pv, juce::dontSendNotification);
    playBtn.setButtonText(pv ? juce::String::fromUTF8("Стоп") : juce::String::fromUTF8("Прослушать"));
    roll.setPlayhead(pv ? proc.previewPositionTicks() : -1.0);
}

void ChoirEditor::paint(juce::Graphics& g) {
    g.fillAll(ui::bg);
    auto top = getLocalBounds().removeFromTop(56);
    g.setColour(ui::text);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("Choir Arranger", top.withTrimmedLeft(20).withWidth(220), juce::Justification::centredLeft);
    auto side = getLocalBounds().withTrimmedTop(56).removeFromLeft(256).reduced(12, 0).withTrimmedBottom(12);
    g.setColour(ui::panel);
    g.fillRoundedRectangle(side.toFloat(), 8.0f);
    if (dropHover) {
        g.setColour(ui::accent);
        g.drawRoundedRectangle(roll.getBounds().toFloat().expanded(2), 9.0f, 2.0f);
    }
}

void ChoirEditor::resized() {
    auto b = getLocalBounds();
    auto top = b.removeFromTop(56).reduced(16, 11);
    top.removeFromLeft(210);
    scoreBtn.setBounds(top.removeFromRight(132));
    top.removeFromRight(8);
    exportBtn.setBounds(top.removeFromRight(112));
    top.removeFromRight(8);
    playBtn.setBounds(top.removeFromRight(104));
    top.removeFromRight(8);
    openBtn.setBounds(top.removeFromRight(116));
    top.removeFromRight(8);
    captureBtn.setBounds(top.removeFromRight(176));
    sourceLbl.setBounds(top.reduced(8, 0));

    auto side = b.removeFromLeft(256).reduced(12, 0).withTrimmedBottom(12).reduced(14, 12);
    auto row = [&](juce::Component& c, int h, int gap) {
        c.setBounds(side.removeFromTop(h));
        side.removeFromTop(gap);
    };
    row(inputLbl, 16, 0);
    row(inputBox, 28, 8);
    row(styleLbl, 16, 0);
    row(styleBox, 28, 6);
    if (choirBox.isVisible()) {
        row(choirLbl, 16, 0);
        row(choirBox, 28, 2);
        row(amateurBtn, 22, 4);
    }
    if (octavistsBtn.isVisible()) row(octavistsBtn, 22, 4);
    if (textureBox.isVisible()) {
        row(textureLbl, 16, 0);
        row(textureBox, 28, 6);
    }
    if (outOfModeBox.isVisible()) {
        row(outOfModeLbl, 16, 0);
        row(outOfModeBox, 28, 4);
    }
    if (mixedBtn.isVisible()) row(mixedBtn, 22, 4);
    auto snd = side.removeFromBottom(46);
    soundLbl.setBounds(snd.removeFromTop(16));
    soundBox.setBounds(snd.removeFromTop(28));
    side.removeFromBottom(8);
    auto varRow = side.removeFromBottom(28);
    prevVarBtn.setBounds(varRow.removeFromLeft(34));
    varRow.removeFromLeft(6);
    nextVarBtn.setBounds(varRow);
    variantLbl.setBounds(side.removeFromBottom(18));
    side.removeFromBottom(4);
    complexityLbl.setBounds(side.removeFromTop(16));
    bool showVoices = voicesLbl.isVisible();
    int knob = juce::jlimit(64, 110, side.getHeight() - (showVoices ? 96 : 4));
    complexity.setBounds(side.removeFromTop(knob).withSizeKeepingCentre(knob, knob));
    if (showVoices) {
        side.removeFromTop(4);
        voicesLbl.setBounds(side.removeFromTop(16));
        auto vrow = side.removeFromTop(52);
        int w = vrow.getWidth() / 4;
        for (int i = 0; i < 4; ++i) voice[i].setBounds(vrow.removeFromLeft(w).withSizeKeepingCentre(48, 48));
        auto lrow = side.removeFromTop(16);
        for (int i = 0; i < 4; ++i) voiceLbl[i].setBounds(lrow.removeFromLeft(w));
    }

    auto main = b.reduced(4, 0).withTrimmedRight(12).withTrimmedBottom(12);
    auto rep = main.removeFromBottom(proc.result().issues.size() > 2 ? 150 : 118);
    report.setBounds(rep.withTrimmedTop(6));
    auto chipRow = main.removeFromBottom(40).withTrimmedTop(8);
    int need = 132 + (int)chips.size() * 104 + 86 + 300;
    bool hintFits = !proc.result().parts.empty() && chipRow.getWidth() >= need;
    hintLbl.setVisible(hintFits);
    if (scoreChip->isVisible()) {
        scoreChip->setBounds(chipRow.removeFromRight(86));
        chipRow.removeFromRight(10);
    }
    if (hintFits) hintLbl.setBounds(chipRow.removeFromRight(300));
    if (allChip->isVisible()) {
        allChip->setBounds(chipRow.removeFromLeft(124));
        chipRow.removeFromLeft(8);
    }
    int cw = chips.isEmpty() ? 104 : juce::jlimit(52, 118, (chipRow.getWidth() - 6 * chips.size()) / chips.size());
    for (auto* c : chips) {
        c->setBounds(chipRow.removeFromLeft(cw));
        chipRow.removeFromLeft(6);
    }
    roll.setBounds(main);
}

// Any file is welcome: regions dragged from Logic may arrive with an unusual name, or as a file
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
                        "«Choir Arranger drag report.txt». Пришлите его, по нему я доделаю приём.\n\n") +
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
    chooser = std::make_unique<juce::FileChooser>(juce::String::fromUTF8("Файл с аккордами"), juce::File(), "*.mid;*.midi;*.smf;*");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (!f.existsAsFile()) return;
                             juce::String err;
                             if (!proc.loadMidiFile(f, err))
                                 juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Choir Arranger", err);
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
    chooser = std::make_unique<juce::FileChooser>(juce::String::fromUTF8("Сохранить партии"),
                                                  juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                                                      .getChildFile("Choir parts.mid"),
                                                  "*.mid");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                         [tmp](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (f != juce::File()) tmp.copyFileTo(f.withFileExtension("mid"));
                         });
}
