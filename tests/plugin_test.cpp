// Plug-in level tests: file loading, capture from a simulated host, state round trip,
// preview audio and editor snapshots for every style.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginEditor.h"
#include "PluginProcessor.h"

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

    // 5. preview audio
    p.setPreview(true);
    float peak = 0;
    int blocks = 0;
    juce::MidiBuffer mo;
    while (p.isPreviewing() && blocks < 20000) {
        p.processBlock(buf, mo);
        peak = std::max(peak, buf.getMagnitude(0, 512));
        ++blocks;
    }
    expect(peak > 0.01f && peak < 1.0f, "прослушивание звучит, пик " + juce::String(peak, 3));
    expect(!p.isPreviewing(), "прослушивание само остановилось в конце");

    // 6. editor snapshots, one per style
    const char* tags[3] = {"bach", "obikhod", "gregorian"};
    for (int st = 0; st < 3; ++st) {
        p.params.getParameter("style")->setValueNotifyingHost(p.params.getParameter("style")->convertTo0to1((float)st));
        if (st == 2) p.params.getParameter("texture")->setValueNotifyingHost(p.params.getParameter("texture")->convertTo0to1(1));
        pump(200);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setSize(1040, 660);
        pump(100);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
        auto out = outDir.getChildFile(juce::String("ui_") + tags[st] + ".png");
        out.deleteFile();
        juce::FileOutputStream os(out);
        juce::PNGImageFormat().writeImageToStream(img, os);
        expect(out.getSize() > 1000, "снимок интерфейса " + out.getFileName());
    }
    ChoirProcessor empty;
    std::unique_ptr<juce::AudioProcessorEditor> ed(empty.createEditor());
    ed->setSize(1040, 660);
    pump(100);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
    auto out = outDir.getChildFile("ui_empty.png");
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);

    std::cout << (failures ? "FAILED: " : "all passed, failures: ") << failures << "\n";
    return failures ? 1 : 0;
}
