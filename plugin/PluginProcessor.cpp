#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PianoRoll.h"

namespace {

// ---------------------------------------------------------------- preview voice ("aah")
struct AahSound : juce::SynthesiserSound {
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

struct AahVoice : juce::SynthesiserVoice {
    using juce::SynthesiserVoice::renderNextBlock;
    bool canPlaySound(juce::SynthesiserSound* s) override { return dynamic_cast<AahSound*>(s) != nullptr; }

    void startNote(int note, float velocity, juce::SynthesiserSound*, int) override {
        f0 = juce::MidiMessage::getMidiNoteInHertz(note);
        gain = 0.12f * velocity;
        phase = 0;
        vibPhase = juce::Random::getSystemRandom().nextDouble() * juce::MathConstants<double>::twoPi;
        env.setSampleRate(getSampleRate());
        env.setParameters({0.09f, 0.1f, 0.9f, 0.22f});
        env.noteOn();
        // harmonic weights shaped by two "a" formants (750 and 1200 Hz)
        for (int h = 0; h < kH; ++h) {
            double fr = (h + 1) * f0;
            double form = std::exp(-std::pow((fr - 750) / 180, 2)) + 0.6 * std::exp(-std::pow((fr - 1200) / 220, 2)) +
                          0.15 * std::exp(-std::pow((fr - 2700) / 400, 2));
            amp[h] = fr < getSampleRate() / 2 - 1000 ? (float)((0.15 + form) / (h + 1)) : 0.0f;
        }
        float norm = 0;
        for (float a : amp) norm += a * a;
        norm = std::sqrt(norm);
        if (norm > 0) for (float& a : amp) a /= norm;
        age = 0;
    }
    void stopNote(float, bool allowTail) override {
        if (allowTail) env.noteOff();
        else { env.reset(); clearCurrentNote(); }
    }
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(juce::AudioBuffer<float>& out, int start, int num) override {
        if (!isVoiceActive()) return;
        const double sr = getSampleRate();
        for (int i = 0; i < num; ++i) {
            double vibDepth = 0.004 * juce::jlimit(0.0, 1.0, age / (0.35 * sr));
            double f = f0 * (1.0 + vibDepth * std::sin(vibPhase));
            vibPhase += juce::MathConstants<double>::twoPi * 5.2 / sr;
            phase += juce::MathConstants<double>::twoPi * f / sr;
            if (phase > juce::MathConstants<double>::twoPi * 1000) phase -= juce::MathConstants<double>::twoPi * 1000;
            float s = 0;
            for (int h = 0; h < kH; ++h)
                if (amp[h] != 0) s += amp[h] * (float)std::sin((h + 1) * phase);
            float e = env.getNextSample();
            float v = s * e * gain;
            for (int ch = 0; ch < out.getNumChannels(); ++ch) out.addSample(ch, start + i, v);
            ++age;
            if (!env.isActive()) { clearCurrentNote(); break; }
        }
    }
    static constexpr int kH = 24;
    float amp[kH]{};
    double f0 = 220, phase = 0, vibPhase = 0, age = 0;
    float gain = 0.1f;
    juce::ADSR env;
};

choir::Style styleFromIndex(int i) {
    return i == 1 ? choir::Style::Obikhod : i == 2 ? choir::Style::Gregorian : choir::Style::Bach;
}

const char* PARAM_IDS[] = {"style", "complexity", "voiceS", "voiceA", "voiceT", "voiceB",
                           "octavists", "texture", "outofmode", "mixed"};

}  // namespace

// ---------------------------------------------------------------- setup
juce::AudioProcessorValueTreeState::ParameterLayout ChoirProcessor::layout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"style", 1}, "Style",
                                                 StringArray{U("Хорал Баха"), U("Православный обиход"), U("Григорианский хорал")}, 0));
    l.add(std::make_unique<AudioParameterInt>(ParameterID{"complexity", 1}, "Complexity", 0, 10, 4));
    auto voiceAttr = AudioParameterIntAttributes().withStringFromValueFunction(
        [](int v, int) { return v < 0 ? String::fromUTF8("общая") : String(v); });
    for (auto* v : {"S", "A", "T", "B"})
        l.add(std::make_unique<AudioParameterInt>(ParameterID{String("voice") + v, 1}, String("Complexity ") + v, -1,
                                                  10, -1, voiceAttr));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{"octavists", 1}, "Octavists", false));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"texture", 1}, "Texture",
                                                 StringArray{U("Унисон"), U("Органум в кварту"), U("Органум в квинту")}, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"outofmode", 1}, "Out of mode",
                                                 StringArray{U("Подменять ладовым"), U("Пропускать")}, 0));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{"mixed", 1}, "Mixed choir", false));
    return l;
}

ChoirProcessor::ChoirProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "ChoirArranger", layout()) {
    for (auto* id : PARAM_IDS) params.addParameterListener(id, this);
    for (int i = 0; i < 24; ++i) synth_.addVoice(new AahVoice());
    synth_.addSound(new AahSound());
}

void ChoirProcessor::handleAsyncUpdate() {
    if (!wasPlaying_.load() && captureArmed_.load() && captureCount_.load() > 0) {
        finishCapture();   // calls regenerate()
        return;
    }
    regenerate();
}

ChoirProcessor::~ChoirProcessor() {
    for (auto* id : PARAM_IDS) params.removeParameterListener(id, this);
    cancelPendingUpdate();
}

bool ChoirProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ChoirProcessor::prepareToPlay(double sr, int) {
    sampleRate_ = sr;
    synth_.setCurrentPlaybackSampleRate(sr);
}

juce::AudioProcessorEditor* ChoirProcessor::createEditor() { return new ChoirEditor(*this); }

void ChoirProcessor::parameterChanged(const juce::String&, float) { triggerAsyncUpdate(); }

choir::Settings ChoirProcessor::currentSettings() const {
    choir::Settings s;
    s.style = styleFromIndex((int)*params.getRawParameterValue("style"));
    s.global = (int)*params.getRawParameterValue("complexity");
    s.S = (int)*params.getRawParameterValue("voiceS");
    s.A = (int)*params.getRawParameterValue("voiceA");
    s.T = (int)*params.getRawParameterValue("voiceT");
    s.B = (int)*params.getRawParameterValue("voiceB");
    s.octavists = *params.getRawParameterValue("octavists") > 0.5f;
    int tx = (int)*params.getRawParameterValue("texture");
    s.texture = tx == 1 ? choir::Texture::Organum4 : tx == 2 ? choir::Texture::Organum5 : choir::Texture::Unison;
    s.outOfMode = (int)*params.getRawParameterValue("outofmode") == 1 ? choir::OutOfMode::Skip : choir::OutOfMode::Substitute;
    s.mixed = *params.getRawParameterValue("mixed") > 0.5f;
    return s;
}

// ---------------------------------------------------------------- input
bool ChoirProcessor::loadMidiFile(const juce::File& f, juce::String& err) {
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) {
        err = U("не удалось прочитать файл");
        return false;
    }
    std::vector<unsigned char> bytes((const unsigned char*)mb.getData(), (const unsigned char*)mb.getData() + mb.getSize());
    std::vector<choir::Note> notes;
    choir::Meta meta;
    std::string e;
    if (!choir::readSmfBytes(bytes, notes, meta, e)) {
        err = juce::String::fromUTF8(e.c_str());
        return false;
    }
    if (notes.empty()) {
        err = U("в файле нет нот");
        return false;
    }
    setInput(std::move(notes), meta, f.getFileName());
    return true;
}

void ChoirProcessor::setInput(std::vector<choir::Note> notes, choir::Meta meta, const juce::String& source) {
    setPreview(false);
    input_ = std::move(notes);
    meta_ = meta;
    inputSource_ = source;
    regenerate();
}

void ChoirProcessor::regenerate() {
    if (input_.empty()) {
        result_ = choir::Result();
    } else {
        result_ = choir::arrange(input_, meta_, currentSettings());
    }
    rebuildPreviewEvents();
    if (onResultChanged) onResultChanged();
}

juce::File ChoirProcessor::writePartFile(int idx) const {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ChoirArranger");
    dir.createDirectory();
    std::vector<choir::Part> parts;
    juce::String name;
    if (idx < 0) {
        parts = result_.parts;
        name = "Choir All Parts";
    } else {
        parts = {result_.parts[(size_t)idx]};
        name = "Choir " + juce::String::fromUTF8(result_.parts[(size_t)idx].name.c_str());
    }
    auto bytes = choir::writeSmf(parts, meta_);
    auto f = dir.getChildFile(name + ".mid");
    f.replaceWithData(bytes.data(), bytes.size());
    return f;
}

// ---------------------------------------------------------------- capture
void ChoirProcessor::setCapture(bool armed) {
    {
        const juce::SpinLock::ScopedLockType l(captureLock_);
        captured_.clear();
    }
    captureCount_ = 0;
    wasPlaying_ = false;
    captureArmed_ = armed;
}

void ChoirProcessor::finishCapture() {
    std::vector<RawEvent> ev;
    {
        const juce::SpinLock::ScopedLockType l(captureLock_);
        ev.swap(captured_);
    }
    captureCount_ = 0;
    captureArmed_ = false;
    std::stable_sort(ev.begin(), ev.end(), [](const RawEvent& a, const RawEvent& b) { return a.ppq < b.ppq; });
    std::map<int, std::pair<int, int>> open;
    std::vector<choir::Note> notes;
    for (auto& e : ev) {
        int t = (int)std::lround(e.ppq * choir::TPB);
        auto it = open.find(e.note);
        if (it != open.end()) {
            if (t > it->second.first) notes.push_back({it->second.first, t, e.note, it->second.second, 0});
            open.erase(it);
        }
        if (e.on) open[e.note] = {t, e.vel};
    }
    int lastT = ev.empty() ? 0 : (int)std::lround(ev.back().ppq * choir::TPB);
    for (auto& kv : open)
        if (lastT > kv.second.first) notes.push_back({kv.second.first, lastT, kv.first, kv.second.second, 0});
    if (notes.empty()) return;
    choir::Meta meta;
    meta.tempo = (int)std::lround(60000000.0 / juce::jmax(20.0, captureBpm_.load()));
    meta.num = captureNum_.load();
    meta.den = captureDen_.load();
    int barTicks = meta.num * choir::TPB * 4 / juce::jmax(1, meta.den);
    int first = notes.front().start;
    for (auto& n : notes) first = std::min(first, n.start);
    int offset = (first / barTicks) * barTicks;
    for (auto& n : notes) n.start -= offset, n.end -= offset;
    std::stable_sort(notes.begin(), notes.end(), [](const choir::Note& a, const choir::Note& b) {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
    setInput(std::move(notes), meta, U("захват с дорожки"));
}

// ---------------------------------------------------------------- preview
void ChoirProcessor::rebuildPreviewEvents() {
    std::vector<PEvent> ev;
    double len = 0;
    for (auto& p : result_.parts)
        for (auto& n : p.notes) {
            ev.push_back({(double)n.start, n.pitch, true});
            ev.push_back({(double)n.end, n.pitch, false});
            len = std::max(len, (double)n.end);
        }
    std::stable_sort(ev.begin(), ev.end(), [](const PEvent& a, const PEvent& b) {
        return a.tick != b.tick ? a.tick < b.tick : (!a.on && b.on);
    });
    const juce::SpinLock::ScopedLockType l(previewLock_);
    previewEvents_.swap(ev);
    previewLenTicks_ = len;
    previewIdx_ = 0;
}

void ChoirProcessor::setPreview(bool on) {
    const juce::SpinLock::ScopedLockType l(previewLock_);
    previewTicks_ = 0;
    previewIdx_ = 0;
    previewOn_ = on && !previewEvents_.empty();
    killNotes_ = true;
}

// ---------------------------------------------------------------- audio thread
void ChoirProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    buffer.clear();
    const int n = buffer.getNumSamples();

    if (captureArmed_.load()) {
        bool playing = false;
        double ppq0 = 0, bpm = 120;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition()) {
                playing = pos->getIsPlaying();
                if (auto p = pos->getPpqPosition()) ppq0 = *p;
                if (auto b = pos->getBpm()) bpm = *b;
                if (auto ts = pos->getTimeSignature()) {
                    captureNum_ = ts->numerator;
                    captureDen_ = ts->denominator;
                }
            }
        if (playing) {
            captureBpm_ = bpm;
            wasPlaying_ = true;
            const juce::SpinLock::ScopedLockType l(captureLock_);
            for (const auto m : midi) {
                auto msg = m.getMessage();
                if (!msg.isNoteOnOrOff()) continue;
                double ppq = ppq0 + m.samplePosition / sampleRate_ * bpm / 60.0;
                captured_.push_back({ppq, msg.getNoteNumber(), msg.getVelocity(), msg.isNoteOn()});
                ++captureCount_;
            }
        } else if (wasPlaying_.load()) {
            wasPlaying_ = false;
            if (captureCount_.load() > 0) triggerAsyncUpdate();
        }
    }
    midi.clear();

    juce::MidiBuffer synthMidi;
    if (killNotes_.exchange(false)) {
        synthMidi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    }
    if (previewOn_.load()) {
        const juce::SpinLock::ScopedTryLockType l(previewLock_);
        if (l.isLocked()) {
            double ticksPerSample = choir::TPB * (1e6 / meta_.tempo) / sampleRate_;
            double t0 = previewTicks_.load(), t1 = t0 + n * ticksPerSample;
            while (previewIdx_ < previewEvents_.size() && previewEvents_[previewIdx_].tick < t1) {
                auto& e = previewEvents_[previewIdx_++];
                int s = juce::jlimit(0, n - 1, (int)((e.tick - t0) / ticksPerSample));
                auto m = e.on ? juce::MidiMessage::noteOn(1, e.note, (juce::uint8)80) : juce::MidiMessage::noteOff(1, e.note);
                synthMidi.addEvent(m, s);
                midi.addEvent(m, s);
            }
            previewTicks_ = t1;
            if (t1 > previewLenTicks_ + choir::TPB) {
                previewOn_ = false;
                synthMidi.addEvent(juce::MidiMessage::allNotesOff(1), n - 1);
            }
        }
    }
    synth_.renderNextBlock(buffer, synthMidi, 0, n);
}

// ---------------------------------------------------------------- state
void ChoirProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = params.copyState();
    auto xml = std::make_unique<juce::XmlElement>("ChoirArrangerState");
    xml->addChildElement(state.createXml().release());
    auto* in = xml->createNewChildElement("Input");
    in->setAttribute("tempo", meta_.tempo);
    in->setAttribute("num", meta_.num);
    in->setAttribute("den", meta_.den);
    in->setAttribute("source", inputSource_);
    juce::String s;
    for (auto& n : input_) s << n.start << ' ' << n.end << ' ' << n.pitch << ' ' << n.vel << ' ' << n.track << ';';
    in->setAttribute("notes", s);
    copyXmlToBinary(*xml, dest);
}

void ChoirProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (!xml) return;
    if (auto* st = xml->getChildByName(params.state.getType())) params.replaceState(juce::ValueTree::fromXml(*st));
    if (auto* in = xml->getChildByName("Input")) {
        choir::Meta m;
        m.tempo = in->getIntAttribute("tempo", 500000);
        m.num = in->getIntAttribute("num", 4);
        m.den = in->getIntAttribute("den", 4);
        std::vector<choir::Note> notes;
        for (auto& tok : juce::StringArray::fromTokens(in->getStringAttribute("notes"), ";", "")) {
            auto f = juce::StringArray::fromTokens(tok, " ", "");
            if (f.size() >= 5)
                notes.push_back({f[0].getIntValue(), f[1].getIntValue(), f[2].getIntValue(), f[3].getIntValue(), f[4].getIntValue()});
        }
        input_ = notes;
        meta_ = m;
        inputSource_ = in->getStringAttribute("source");
    }
    triggerAsyncUpdate();   // the host may call this off the message thread
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChoirProcessor(); }
