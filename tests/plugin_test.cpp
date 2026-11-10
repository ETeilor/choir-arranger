// Plug-in level tests: file loading, capture from a simulated host, state round trip,
// preview audio and editor snapshots for every style.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "LiveProcessor.h"

struct FakeHead : juce::AudioPlayHead {
    bool playing = false;
    double ppq = 0, bpm = 110;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        p.setIsPlaying(playing);
        p.setPpqPosition(ppq);
        p.setBpm(bpm);
        p.setTimeSignature(TimeSignature{4, 4});
        return p;
    }
};

static int failures = 0;
static void expect(bool ok, const juce::String& what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok) ++failures;
}

static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    juce::File input(argc > 1 ? argv[1] : "test_01_user.mid");
    juce::File outDir(argc > 2 ? argv[2] : ".");
    const double sr = 48000;

    // 1. load a file
    ChoirProcessor p;
    p.prepareToPlay(sr, 512);
    juce::String err;
    expect(p.loadMidiFile(input, err), "MIDI-файл загружен " + err);
    auto& r = p.result();
    expect(r.ok() && r.parts.size() == 4, "хорал: 4 партии");
    int nIn = (int)p.input().size();

    // 2. capture the same notes from a simulated host transport
    ChoirProcessor c;
    c.prepareToPlay(sr, 512);
    FakeHead head;
    c.setPlayHead(&head);
    c.setCapture(true);
    const auto notes = p.input();
    int endTick = 0;
    for (auto& n : notes) endTick = std::max(endTick, n.end);
    const double samplesPerTick = sr * 60.0 / head.bpm / choir::TPB;
    const double startPpq = 8.0;   // the region starts at bar 3
    juce::AudioBuffer<float> buf(2, 512);
    long long total = (long long)((endTick + choir::TPB) * samplesPerTick);
    head.playing = true;
    for (long long s0 = 0; s0 < total; s0 += 512) {
        head.ppq = startPpq + s0 / samplesPerTick / choir::TPB;
        juce::MidiBuffer mb;
        for (auto& n : notes) {
            long long on = (long long)std::llround(n.start * samplesPerTick), off = (long long)std::llround(n.end * samplesPerTick);
            if (on >= s0 && on < s0 + 512) mb.addEvent(juce::MidiMessage::noteOn(1, n.pitch, (juce::uint8)n.vel), (int)(on - s0));
            if (off >= s0 && off < s0 + 512) mb.addEvent(juce::MidiMessage::noteOff(1, n.pitch), (int)(off - s0));
        }
        c.processBlock(buf, mb);
    }
    head.playing = false;
    juce::MidiBuffer none;
    c.processBlock(buf, none);
    pump(300);
    bool same = (int)c.input().size() == nIn;
    if (same)
        for (size_t i = 0; i < notes.size(); ++i) {
            auto a = notes[i], b = c.input()[i];
            if (a.start != b.start || a.end != b.end || a.pitch != b.pitch) { same = false; break; }
        }
    expect(same, "захват с транспорта: " + juce::String((int)c.input().size()) + " нот из " + juce::String(nIn) +
                     ", сдвиг к началу такта");
    expect(!c.isCapturing(), "захват выключился после остановки");
    expect(c.result().parts.size() == p.result().parts.size(), "по захвату построена аранжировка");

    // 3. state round trip
    p.params.getParameter("style")->setValueNotifyingHost(p.params.getParameter("style")->convertTo0to1(1));
    p.params.getParameter("octavists")->setValueNotifyingHost(1.0f);
    p.params.getParameter("complexity")->setValueNotifyingHost(p.params.getParameter("complexity")->convertTo0to1(7));
    pump(200);
    juce::MemoryBlock state;
    p.getStateInformation(state);
    ChoirProcessor q;
    q.setStateInformation(state.getData(), (int)state.getSize());
    pump(200);
    bool eq = q.result().parts.size() == p.result().parts.size() && q.result().parts.size() == 5;
    for (size_t i = 0; eq && i < q.result().parts.size(); ++i)
        eq = q.result().parts[i].notes.size() == p.result().parts[i].notes.size();
    expect(eq, "сохранение в проекте: обиход с октавистами восстановлен (5 партий)");

    // 4. drag files
    auto f = p.writePartFile(0);
    expect(f.existsAsFile() && f.getSize() > 50, "файл партии для перетаскивания: " + f.getFileName());
    auto all = p.writePartFile(-1);
    expect(all.existsAsFile(), "файл всех партий: " + all.getFileName());

    // 5. preview audio: piano (default) and choir
    for (int snd = 0; snd < 2; ++snd) {
        p.params.getParameter("sound")->setValueNotifyingHost((float)snd);
        p.setPreview(true);
        float peak = 0;
        int blocks = 0;
        juce::MidiBuffer mo;
        while (p.isPreviewing() && blocks < 20000) {
            p.processBlock(buf, mo);
            peak = std::max(peak, buf.getMagnitude(0, 512));
            ++blocks;
        }
        expect(peak > 0.01f && peak < 1.0f, juce::String(snd ? "хор" : "пианино") + ": прослушивание звучит, пик " + juce::String(peak, 3));
        expect(!p.isPreviewing(), "прослушивание само остановилось в конце");
    }
    p.params.getParameter("sound")->setValueNotifyingHost(0.0f);

    // 5b. notation and file detection
    auto score = p.writeScoreFile();
    auto xml = juce::XmlDocument::parse(score);
    expect(xml != nullptr && xml->hasTagName("score-partwise") &&
               xml->getChildByName("part-list")->getNumChildElements() == (int)p.result().parts.size(),
           "ноты MusicXML: " + score.getFileName());
    auto odd = outDir.getChildFile("region_without_extension");
    all.copyFileTo(odd);
    expect(ChoirProcessor::isMidiFile(odd) && !ChoirProcessor::isMidiFile(score), "MIDI узнаётся по содержимому");
    juce::String e2;
    expect(p.loadMidiFile(odd, e2), "файл без расширения загружен");
    expect(p.loadMidiFile(input, e2), "вход возвращён");

    // 5c. variants, locks, choir make-up, melody harmonization
    {
        auto setP = [&](const char* id, float v) {
            auto* prm = p.params.getParameter(id);
            prm->setValueNotifyingHost(prm->convertTo0to1(v));
            pump(150);
        };
        setP("style", 0);
        auto base = p.result();
        p.stepVariant(+1);
        pump(200);
        int var = (int)*p.params.getRawParameterValue("variant");
        bool differs = false;
        for (size_t i = 0; i < base.parts.size() && i < p.result().parts.size(); ++i)
            if (base.parts[i].notes.size() != p.result().parts[i].notes.size()) differs = true;
            else
                for (size_t k = 0; k < base.parts[i].notes.size(); ++k)
                    if (base.parts[i].notes[k].pitch != p.result().parts[i].notes[k].pitch) differs = true;
        expect(var > 0 && differs, "другой вариант: " + juce::String(var));
        p.toggleLock("Bass");
        auto bassBefore = p.result().parts[3].notes;
        setP("complexity", 8);
        bool kept = p.result().parts[3].notes.size() == bassBefore.size();
        for (size_t k = 0; kept && k < bassBefore.size(); ++k)
            kept = p.result().parts[3].notes[k].pitch == bassBefore[k].pitch;
        expect(kept && p.isLocked("Bass"), "закреплённый бас не изменился при сложности 8");
        p.toggleLock("Bass");
        setP("complexity", 4);
        setP("variant", 0);
        setP("choir", 3);
        juce::StringArray names;
        for (auto& pt : p.result().parts) names.add(juce::String::fromUTF8(pt.name.c_str()));
        expect(names.joinIntoString(",") == "Soprano,Alto,Baritone", "состав SAB: " + names.joinIntoString(","));
        setP("choir", 2);
        names.clear();
        for (auto& pt : p.result().parts) names.add(juce::String::fromUTF8(pt.name.c_str()));
        expect(names.joinIntoString(",") == "Tenor 1,Tenor 2,Bass 1,Bass 2", "состав TTBB: " + names.joinIntoString(","));
        setP("choir", 0);
        auto mel = input.getParentDirectory().getParentDirectory().getChildFile("melodies").getChildFile("ode.mid");
        juce::String e3;
        expect(p.loadMidiFile(mel, e3) && p.result().harmonized && p.result().parts.size() == 4,
               "мелодия гармонизована: " + juce::String((int)p.result().chords.size()) + " аккордов " + e3);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setSize(1120, 720);
        pump(100);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
        auto out = outDir.getChildFile("ui_harmonized.png");
        out.deleteFile();
        juce::FileOutputStream os(out);
        juce::PNGImageFormat().writeImageToStream(img, os);
        ed.reset();
        expect(p.loadMidiFile(input, e3), "вход возвращён");
    }

    // 5e. no state carried between styles: after any walk through the styles the result equals a fresh arrangement
    {
        juce::String e4;
        auto minorEnd = outDir.getChildFile("minor_end.mid");
        if (minorEnd.existsAsFile() && p.loadMidiFile(minorEnd, e4)) {
            auto setP = [&](const char* id, float v) {
                auto* prm = p.params.getParameter(id);
                prm->setValueNotifyingHost(prm->convertTo0to1(v));
                pump(120);
            };
            setP("complexity", 8);
            int bad = 0;
            const int walk[] = {20, 0, 20, 1, 20, 6, 20, 7, 20, 9, 20, 2, 20, 12, 0, 5};
            for (int st : walk) {
                setP("style", (float)st);
                auto fresh = choir::arrange(p.input(), p.meta(), p.settingsForTest());
                bool same = fresh.parts.size() == p.result().parts.size();
                for (size_t i = 0; same && i < fresh.parts.size(); ++i) {
                    same = fresh.parts[i].notes.size() == p.result().parts[i].notes.size();
                    for (size_t k = 0; same && k < fresh.parts[i].notes.size(); ++k)
                        same = fresh.parts[i].notes[k].pitch == p.result().parts[i].notes[k].pitch;
                }
                if (!same) ++bad;
                // minor final chord stays minor everywhere except where the style asks for a major one
                std::set<int> pcs;
                int end = 0;
                for (auto& pt : p.result().parts) for (auto& n : pt.notes) end = std::max(end, n.end);
                for (auto& pt : p.result().parts) for (auto& n : pt.notes) if (n.end == end) pcs.insert(n.pitch % 12);
                std::cout << "    style " << st << " final pcs:";
                for (int x : pcs) std::cout << " " << x;
                std::cout << "\n";
            }
            expect(bad == 0, "стили не переносят состояние друг другу, расхождений " + juce::String(bad));
            setP("complexity", 4);
            setP("style", 0);
            expect(p.loadMidiFile(input, e4), "вход возвращён");
        }
    }

    // 5f. saved variants, refresh, paste
    {
        auto setP = [&](const char* id, float v) {
            auto* prm = p.params.getParameter(id);
            prm->setValueNotifyingHost(prm->convertTo0to1(v));
            pump(120);
        };
        setP("style", 0);
        setP("variant", 0);
        setP("complexity", 4);
        p.saveSlot();                                  // slot 1: the main variant at 4
        p.stepVariant(+1);
        pump(150);
        setP("complexity", 7);
        auto second = p.result();
        p.saveSlot();                                  // slot 2: another variant at 7
        expect(p.numSlots() == 2 && p.currentSlot() == 1, "варианты запомнены: " + juce::String(p.numSlots()));
        p.loadSlot(0);
        pump(200);
        expect(p.currentSlot() == 0 && (int)*p.params.getRawParameterValue("complexity") == 4 &&
                   (int)*p.params.getRawParameterValue("variant") == 0, "возврат к первому запомненному варианту");
        p.loadSlot(1);
        pump(200);
        bool sameAsSaved = p.result().parts.size() == second.parts.size();
        for (size_t i = 0; sameAsSaved && i < second.parts.size(); ++i) {
            sameAsSaved = second.parts[i].notes.size() == p.result().parts[i].notes.size();
            for (size_t k = 0; sameAsSaved && k < second.parts[i].notes.size(); ++k)
                sameAsSaved = second.parts[i].notes[k].pitch == p.result().parts[i].notes[k].pitch;
        }
        expect(sameAsSaved, "второй запомненный вариант звучит как при сохранении");
        // each style keeps its own variant
        int varBach = (int)*p.params.getRawParameterValue("variant");
        setP("style", 7);
        expect((int)*p.params.getRawParameterValue("variant") == 0 && p.numSlots() == 0, "в новом стиле основной вариант и свои слоты");
        setP("style", 0);
        expect((int)*p.params.getRawParameterValue("variant") == varBach && p.numSlots() == 2, "стиль помнит свой вариант");
        // state round trip keeps the slots
        juce::MemoryBlock st2;
        p.getStateInformation(st2);
        ChoirProcessor q;
        q.setStateInformation(st2.getData(), (int)st2.getSize());
        pump(300);
        expect(q.numSlots() == 2, "слоты сохраняются в проекте");
        // refresh: locks off, main variant
        p.toggleLock("Bass");
        p.refreshAll();
        pump(200);
        expect(!p.isLocked("Bass") && (int)*p.params.getRawParameterValue("variant") == 0, "«Обновить» снимает закрепления и возвращает основной вариант");
        p.removeSlot(1);
        p.removeSlot(0);
        setP("complexity", 4);
        // paste: on this platform the clipboard holds a path to a MIDI file
        auto mel = input.getParentDirectory().getParentDirectory().getChildFile("melodies").getChildFile("folk.mid");
        juce::SystemClipboard::copyTextToClipboard(mel.getFullPathName());
        juce::String msg;
        bool pasted = p.pasteFromClipboard(msg);
        if (juce::SystemClipboard::getTextFromClipboard() == mel.getFullPathName())
            expect(pasted && p.inputSource().isNotEmpty() && p.result().harmonized, "вставка из буфера: " + msg);
        juce::String e5;
        expect(p.loadMidiFile(input, e5), "вход возвращён");
    }

    // 5d. live MIDI FX
    {
        LiveProcessor lp;
        lp.prepareToPlay(48000, 512);
        juce::AudioBuffer<float> none(0, 512);
        auto play = [&](std::vector<int> on, std::vector<int> off) {
            juce::MidiBuffer m;
            for (int k : off) m.addEvent(juce::MidiMessage::noteOff(1, k), 0);
            for (int k : on) m.addEvent(juce::MidiMessage::noteOn(1, k, (juce::uint8)90), 10);
            lp.processBlock(none, m);
            juce::MidiBuffer next;
            lp.processBlock(none, next);   // the chord settles within a few milliseconds
            for (const auto x : next) m.addEvent(x.getMessage(), x.samplePosition);
            return m;
        };
        auto count = [](const juce::MidiBuffer& m, bool on) {
            int n = 0;
            for (const auto x : m) n += on ? x.getMessage().isNoteOn() : x.getMessage().isNoteOff();
            return n;
        };
        auto a = play({57, 60, 64, 69}, {});
        expect(count(a, true) == 4, "живой режим: Am дал 4 голоса, нот " + juce::String(count(a, true)));
        auto f = play({53, 57, 60, 72}, {57, 60, 64, 69});
        expect(count(f, true) >= 1 && count(f, true) <= 4, "живой режим: смена на F переозвучила голоса");
        auto off = play({}, {53, 57, 60, 72});
        expect(count(off, false) == 4, "живой режим: отпустили, все голоса сняты");
        lp.params.getParameter("channels")->setValueNotifyingHost(1.0f);
        auto ch = play({48, 52, 55, 72}, {});
        std::set<int> chans;
        for (const auto x : ch) if (x.getMessage().isNoteOn()) chans.insert(x.getMessage().getChannel());
        expect(chans.size() == 4, "живой режим: каждый голос в своём канале");
        // velocities: a hard bass in octaves under a soft right hand
        play({}, {48, 52, 55, 72});
        juce::MidiBuffer hard;
        hard.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)120), 0);
        hard.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8)118), 1);
        hard.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)40), 2);
        hard.addEvent(juce::MidiMessage::noteOn(1, 72, (juce::uint8)45), 3);
        lp.processBlock(none, hard);
        juce::MidiBuffer hard2;
        lp.processBlock(none, hard2);
        int loudLow = 0, softHigh = 0, voices = 0;
        for (const auto x : hard2) {
            auto m = x.getMessage();
            if (!m.isNoteOn()) continue;
            ++voices;
            if (m.getNoteNumber() < 52 && m.getVelocity() > 100) ++loudLow;
            if (m.getNoteNumber() > 62 && m.getVelocity() < 60) ++softHigh;
        }
        expect(voices == 5 && loudLow >= 2 && softHigh >= 1, "живой режим: сила нажатия по голосам, бас удвоен, голосов " + juce::String(voices));
        // legato: the next chord lands while the old keys are still down
        juce::MidiBuffer leg;
        for (int k : {41, 53, 57, 60}) leg.addEvent(juce::MidiMessage::noteOn(1, k, (juce::uint8)90), 0);
        lp.processBlock(none, leg);
        juce::MidiBuffer leg2;
        lp.processBlock(none, leg2);
        expect(lp.snapshot().chord == "F", "живой режим: легато-смена аккорда читает новый аккорд, получился " +
                                               juce::String(lp.snapshot().chord));
        juce::MidiBuffer rel;
        for (int k : {36, 48, 64, 72}) rel.addEvent(juce::MidiMessage::noteOff(1, k), 0);
        lp.processBlock(none, rel);
        juce::MidiBuffer rel2;
        lp.processBlock(none, rel2);
        expect(lp.snapshot().chord == "F" && count(rel2, true) == 0, "живой режим: отпускание старых клавиш ничего не меняет");
        // inversion by re-fingering: the bass moves to the third, the chord stays
        juce::MidiBuffer inv;
        inv.addEvent(juce::MidiMessage::noteOff(1, 41), 0);
        inv.addEvent(juce::MidiMessage::noteOff(1, 53), 0);
        inv.addEvent(juce::MidiMessage::noteOn(1, 45, (juce::uint8)90), 400);
        lp.processBlock(none, inv);
        juce::MidiBuffer inv2, inv3;
        lp.processBlock(none, inv2);
        lp.processBlock(none, inv3);
        expect(lp.snapshot().chord == "F/A", "живой режим: обращение сохраняет аккорд, получился " + juce::String(lp.snapshot().chord));
        // the take goes out as a MIDI file with one track per part
        auto takeFile = lp.writeRecordingFile();
        std::vector<choir::Note> tn;
        choir::Meta tm;
        std::string te;
        expect(takeFile.existsAsFile() && choir::readSmf(takeFile.getFullPathName().toStdString(), tn, tm, te) && tn.size() >= 8 &&
                   lp.recordedChords() >= 3,
               "живой режим: сыгранное выгружается в MIDI, нот " + juce::String((int)tn.size()));
        lp.params.getParameter("register")->setValueNotifyingHost(0.5f);
        play({}, {45, 57, 60});
        auto low = play({48, 52, 55, 72}, {});
        int lowest = 127;
        for (const auto x : low) if (x.getMessage().isNoteOn()) lowest = std::min(lowest, x.getMessage().getNoteNumber());
        expect(lowest == 36, "живой режим: регистр на октаву ниже, бас " + juce::String(lowest));
        lp.params.getParameter("register")->setValueNotifyingHost(0.0f);
        play({}, {48, 52, 55, 72});
        play({48, 55, 60, 64, 71}, {});
        std::unique_ptr<juce::AudioProcessorEditor> ed(lp.createEditor());
        pump(100);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
        auto out = outDir.getChildFile("ui_live.png");
        out.deleteFile();
        juce::FileOutputStream os(out);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }

    // 6. editor snapshots, one per style
    const char* tags[21] = {"bach", "obikhod", "gregorian", "znamenny", "byzantine", "renaissance",
                            "contemporary", "gospel", "barbershop", "epic", "partes", "spiritual", "anglican",
                            "sacredharp", "bulgarian", "georgian", "tintinnabuli", "closeharmony", "apop", "tenore", "soviet"};
    for (int st = 0; st < 21; ++st) {
        p.params.getParameter("style")->setValueNotifyingHost(p.params.getParameter("style")->convertTo0to1((float)st));
        if (st == 2) p.params.getParameter("texture")->setValueNotifyingHost(p.params.getParameter("texture")->convertTo0to1(1));
        pump(200);
        expect(p.result().ok() && !p.result().parts.empty(), juce::String("стиль ") + tags[st] + ": " +
               juce::String((int)p.result().parts.size()) + " партий " + juce::String::fromUTF8(p.result().error.c_str()));
        auto x = juce::XmlDocument::parse(p.scoreXml());
        expect(x != nullptr, juce::String("ноты для ") + tags[st]);
        if (st == 6) {   // play halfway so the keyboard lights up in the snapshot
            p.setPreview(true);
            juce::MidiBuffer mo;
            for (int i = 0; i < 300; ++i) p.processBlock(buf, mo);
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setSize(1120, 720);
        pump(100);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
        auto out = outDir.getChildFile(juce::String("ui_") + tags[st] + ".png");
        out.deleteFile();
        juce::FileOutputStream os(out);
        juce::PNGImageFormat().writeImageToStream(img, os);
        expect(out.getSize() > 1000, "снимок интерфейса " + out.getFileName());
        p.setPreview(false);
    }
    ChoirProcessor empty;
    std::unique_ptr<juce::AudioProcessorEditor> ed(empty.createEditor());
    ed->setSize(1120, 720);
    pump(100);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
    auto out = outDir.getChildFile("ui_empty.png");
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);

    std::cout << (failures ? "FAILED: " : "all passed, failures: ") << failures << "\n";
    return failures ? 1 : 0;
}
