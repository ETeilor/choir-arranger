#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PianoRoll.h"
#include "Look.h"
#include <cstring>

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

// ---------------------------------------------------------------- preview voice (piano)
// additive piano: slightly inharmonic partials, each decaying on its own, upper partials faster,
// two detuned "strings" per partial and a damper release on note-off.
struct PianoVoice : juce::SynthesiserVoice {
    using juce::SynthesiserVoice::renderNextBlock;
    bool canPlaySound(juce::SynthesiserSound* s) override { return dynamic_cast<AahSound*>(s) != nullptr; }

    void startNote(int note, float velocity, juce::SynthesiserSound*, int) override {
        const double sr = getSampleRate();
        double f0 = juce::MidiMessage::getMidiNoteInHertz(note);
        double B = 0.00012 * std::pow(2.0, (note - 60) / 24.0);          // inharmonicity grows up the keyboard
        double base = 0.55 + 0.9 * std::pow(2.0, (60 - note) / 18.0);      // seconds of sustain at the fundamental
        double bright = 0.6 + 0.8 * velocity;
        float norm = 0;
        for (int k = 0; k < kH; ++k) {
            int h = k + 1;
            double fr = h * f0 * std::sqrt(1 + B * h * h);
            inc[k] = juce::MathConstants<double>::twoPi * fr / sr;
            inc2[k] = inc[k] * 1.0007;
            ph[k] = ph2[k] = 0;
            float a = fr < sr / 2 - 500 ? (float)(std::pow(h, -1.0 - 0.6 / bright) * (h == 1 ? 1.0 : 0.8)) : 0.0f;
            if (fr > 5000) a *= (float)std::exp(-(fr - 5000) / 3000);
            amp[k] = a;
            norm += a;
            double tau = base / (1 + 0.35 * (h - 1));
            decay[k] = (float)std::exp(-1.0 / (tau * sr));
            lvl[k] = 1.0f;
        }
        if (norm > 0) for (float& a : amp) a /= norm;
        gain = 0.32f * velocity;
        attack = 0;
        attackInc = 1.0f / (float)(0.003 * sr);
        release = 1.0f;
        releaseMul = 1.0f;
        releaseStep = (float)std::exp(-1.0 / (0.12 * sr));
    }
    void stopNote(float, bool allowTail) override {
        if (allowTail) releaseMul = releaseStep;
        else clearCurrentNote();
    }
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(juce::AudioBuffer<float>& out, int start, int num) override {
        if (!isVoiceActive()) return;
        for (int i = 0; i < num; ++i) {
            float s = 0;
            for (int k = 0; k < kH; ++k) {
                if (amp[k] == 0) continue;
                s += amp[k] * lvl[k] * (float)(std::sin(ph[k]) + 0.6 * std::sin(ph2[k]));
                ph[k] += inc[k];
                ph2[k] += inc2[k];
                lvl[k] *= decay[k];
            }
            if (attack < 1) attack = std::min(1.0f, attack + attackInc);
            release *= releaseMul;
            float v = s * gain * attack * release * 0.62f;
            for (int ch = 0; ch < out.getNumChannels(); ++ch) out.addSample(ch, start + i, v);
            if (release < 0.0005f || lvl[0] < 0.0005f) { clearCurrentNote(); break; }
        }
        for (int k = 0; k < kH; ++k) {
            ph[k] = std::fmod(ph[k], juce::MathConstants<double>::twoPi);
            ph2[k] = std::fmod(ph2[k], juce::MathConstants<double>::twoPi);
        }
    }
    static constexpr int kH = 14;
    double inc[kH]{}, inc2[kH]{}, ph[kH]{}, ph2[kH]{};
    float amp[kH]{}, decay[kH]{}, lvl[kH]{};
    float gain = 0.1f, attack = 0, attackInc = 0.01f, release = 1, releaseMul = 1, releaseStep = 0.999f;
};

choir::Style styleFromIndex(int i) {
    return (choir::Style)juce::jlimit(0, (int)choir::Style::Soviet, i);
}

const char* PARAM_IDS[] = {"style", "complexity", "voiceS", "voiceA", "voiceT", "voiceB",
                           "octavists", "texture", "outofmode", "mixed", "choir", "amateur", "input", "variant"};

}  // namespace

// ---------------------------------------------------------------- setup
juce::StringArray ChoirProcessor::choirNames() { return names::choirs(); }
juce::StringArray ChoirProcessor::styleNames() { return names::styles(); }

juce::AudioProcessorValueTreeState::ParameterLayout ChoirProcessor::layout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"style", 1}, "Style",
                                                 styleNames(), 0));
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
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"choir", 1}, "Choir", choirNames(), 0));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{"amateur", 1}, "Amateur ranges", false));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"input", 1}, "Input",
                                                 StringArray{U("Авто"), U("Аккорды"), U("Гармонизовать мелодию")}, 0));
    l.add(std::make_unique<AudioParameterInt>(ParameterID{"variant", 1}, "Variant", 0, 9999, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"sound", 1}, "Preview sound",
                                                 StringArray{U("Пианино"), U("Хор")}, 0));
    return l;
}

ChoirProcessor::ChoirProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "ChoirArranger", layout()) {
    for (auto* id : PARAM_IDS) params.addParameterListener(id, this);
    for (int i = 0; i < 24; ++i) synth_.addVoice(new AahVoice());
    synth_.addSound(new AahSound());
    for (int i = 0; i < 32; ++i) piano_.addVoice(new PianoVoice());
    piano_.addSound(new AahSound());
    params.addParameterListener("sound", this);
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
    params.removeParameterListener("sound", this);
    cancelPendingUpdate();
}

bool ChoirProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ChoirProcessor::prepareToPlay(double sr, int) {
    sampleRate_ = sr;
    synth_.setCurrentPlaybackSampleRate(sr);
    piano_.setCurrentPlaybackSampleRate(sr);
}

juce::AudioProcessorEditor* ChoirProcessor::createEditor() { return new ChoirEditor(*this); }

void ChoirProcessor::parameterChanged(const juce::String& id, float) {
    if (id == "sound") {
        killNotes_ = true;   // the other instrument takes over from the next note
        return;
    }
    triggerAsyncUpdate();
}

// ---------------------------------------------------------------- locks and variants
void ChoirProcessor::toggleLock(const juce::String& part) {
    auto name = part.toStdString();
    if (locks_.count(name)) locks_.erase(name);
    else
        for (auto& p : result_.parts)
            if (p.name == name) locks_[name] = p.notes;
    regenerate();
}

bool ChoirProcessor::isLocked(const juce::String& part) const { return locks_.count(part.toStdString()) > 0; }

static bool sameParts(const choir::Result& a, const choir::Result& b) {
    if (a.parts.size() != b.parts.size()) return false;
    for (size_t i = 0; i < a.parts.size(); ++i) {
        auto &x = a.parts[i].notes, &y = b.parts[i].notes;
        if (x.size() != y.size()) return false;
        for (size_t k = 0; k < x.size(); ++k)
            if (x[k].start != y[k].start || x[k].end != y[k].end || x[k].pitch != y[k].pitch) return false;
    }
    return true;
}

void ChoirProcessor::stepVariant(int dir) {
    int cur = (int)*params.getRawParameterValue("variant");
    int next = cur;
    if (dir < 0) {
        next = std::max(0, cur - 1);
    } else if (!input_.empty()) {
        // skip variants that sound the same as the one on screen
        auto s = currentSettings();
        for (int k = 1; k <= 24; ++k) {
            s.variant = cur + k;
            if (!sameParts(choir::arrange(input_, meta_, s), result_)) { next = cur + k; break; }
            next = cur + k;
        }
    } else {
        next = cur + 1;
    }
    auto* p = params.getParameter("variant");
    p->setValueNotifyingHost(p->convertTo0to1((float)juce::jlimit(0, 9999, next)));
}

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
    s.choir = (choir::Choir)juce::jlimit(0, 4, (int)*params.getRawParameterValue("choir"));
    s.amateur = *params.getRawParameterValue("amateur") > 0.5f;
    int in = (int)*params.getRawParameterValue("input");
    s.input = in == 1 ? choir::InputMode::Chords : in == 2 ? choir::InputMode::HarmonizeMelody : choir::InputMode::Auto;
    s.variant = (int)*params.getRawParameterValue("variant");
    s.locks = locks_;
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
    if (!isMidiData(bytes)) {
        err = U("это не MIDI-файл");
        return false;
    }
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
    locks_.clear();
    input_ = std::move(notes);
    meta_ = meta;
    inputSource_ = source;
    regenerate();
}

void ChoirProcessor::regenerate() {
    // a new style, choir or input mode renames the parts: the locks no longer apply
    int key = (int)*params.getRawParameterValue("style") * 100 + (int)*params.getRawParameterValue("choir") * 10 +
              (int)*params.getRawParameterValue("input");
    if (lockKey_ >= 0 && key != lockKey_) locks_.clear();
    lockKey_ = key;
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

bool ChoirProcessor::isMidiData(const std::vector<unsigned char>& b) {
    return b.size() >= 14 && b[0] == 'M' && b[1] == 'T' && b[2] == 'h' && b[3] == 'd';
}

bool ChoirProcessor::isMidiFile(const juce::File& f) {
    juce::FileInputStream in(f);
    if (!in.openedOk()) return false;
    char h[4]{};
    return in.read(h, 4) == 4 && std::memcmp(h, "MThd", 4) == 0;
}

juce::String ChoirProcessor::scoreTitle() const {
    auto src = inputSource_.upToLastOccurrenceOf(".", false, false);
    if (src.isEmpty() || inputSource_.startsWith(U("захват"))) src = "Choir";
    return src + " - " + juce::String::fromUTF8(result_.styleLabel.c_str());
}

juce::String ChoirProcessor::scoreXml() const {
    return juce::String::fromUTF8(choir::toMusicXml(result_, meta_, scoreTitle().toStdString()).c_str());
}

juce::File ChoirProcessor::writeScoreFile() const {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ChoirArranger");
    dir.createDirectory();
    auto f = dir.getChildFile("Choir Score.musicxml");
    auto x = choir::toMusicXml(result_, meta_, scoreTitle().toStdString());
    f.replaceWithData(x.data(), x.size());
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
        synth_.allNotesOff(0, false);
        piano_.allNotesOff(0, false);
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
    bool usePiano = (int)*params.getRawParameterValue("sound") == 0;
    juce::MidiBuffer empty;
    (usePiano ? piano_ : synth_).renderNextBlock(buffer, synthMidi, 0, n);
    (usePiano ? synth_ : piano_).renderNextBlock(buffer, empty, 0, n);   // lets released notes ring out
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
    auto* lk = xml->createNewChildElement("Locks");
    for (auto& kv : locks_) {
        auto* e = lk->createNewChildElement("Lock");
        e->setAttribute("part", juce::String::fromUTF8(kv.first.c_str()));
        juce::String ns;
        for (auto& n : kv.second) ns << n.start << ' ' << n.end << ' ' << n.pitch << ';';
        e->setAttribute("notes", ns);
    }
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
    locks_.clear();
    if (auto* lk = xml->getChildByName("Locks"))
        for (auto* e : lk->getChildIterator()) {
            std::vector<choir::Note> ns;
            for (auto& tok : juce::StringArray::fromTokens(e->getStringAttribute("notes"), ";", "")) {
                auto f = juce::StringArray::fromTokens(tok, " ", "");
                if (f.size() >= 3) ns.push_back({f[0].getIntValue(), f[1].getIntValue(), f[2].getIntValue(), 80, 0});
            }
            locks_[e->getStringAttribute("part").toStdString()] = ns;
        }
    lockKey_ = -1;   // the restored parameters may arrive after this: keep the restored locks
    triggerAsyncUpdate();   // the host may call this off the message thread
}

#ifndef CHOIR_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChoirProcessor(); }
#endif
