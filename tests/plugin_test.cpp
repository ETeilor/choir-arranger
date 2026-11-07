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
        ed->setSize(1120, 740);
        pump(100);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
        auto out = outDir.getChildFile("ui_harmonized.png");
        out.deleteFile();
        juce::FileOutputStream os(out);
        juce::PNGImageFormat().writeImageToStream(img, os);
        ed.reset();
        expect(p.loadMidiFile(input, e3), "вход возвращён");
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
        std::unique_ptr<juce::AudioProcessorEditor> ed(lp.createEditor());
        pump(100);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
        auto out = outDir.getChildFile("ui_live.png");
        out.deleteFile();
        juce::FileOutputStream os(out);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }

    // 6. editor snapshots, one per style
    const char* tags[20] = {"bach", "obikhod", "gregorian", "znamenny", "byzantine", "renaissance",
                            "contemporary", "gospel", "barbershop", "epic", "partes", "spiritual", "anglican",
                            "sacredharp", "bulgarian", "georgian", "tintinnabuli", "closeharmony", "apop", "tenore"};
    for (int st = 0; st < 20; ++st) {
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
        ed->setSize(1120, 740);
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
    ed->setSize(1120, 740);
    pump(100);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
    auto out = outDir.getChildFile("ui_empty.png");
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);

    std::cout << (failures ? "FAILED: " : "all passed, failures: ") << failures << "\n";
    return failures ? 1 : 0;
}
