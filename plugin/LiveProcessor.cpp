#include "LiveProcessor.h"
#include "LiveEditor.h"
#include "PianoRoll.h"
#include "Look.h"

namespace {
constexpr double SETTLE_MS = 14.0;   // keys of one chord never land at the same instant
}

juce::AudioProcessorValueTreeState::ParameterLayout LiveProcessor::layout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"style", 1}, "Style", names::styles(), 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"choir", 1}, "Choir", names::choirs(), 0));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{"amateur", 1}, "Amateur ranges", false));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{"topmelody", 1}, "Top note is the melody", true));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"channels", 1}, "Channels",
                                                 StringArray{U("Все голоса в один канал"), U("Каждый голос в свой канал")}, 0));
    l.add(std::make_unique<AudioParameterInt>(ParameterID{"variant", 1}, "Variant", 0, 99, 0));
    return l;
}

LiveProcessor::LiveProcessor()
    : AudioProcessor(BusesProperties()), params(*this, nullptr, "ChoirArrangerLive", layout()) {}

void LiveProcessor::prepareToPlay(double sr, int) {
    sampleRate_ = sr;
}

choir::Settings LiveProcessor::settings() const {
    choir::Settings s;
    s.style = (choir::Style)juce::jlimit(0, (int)choir::Style::Tenore, (int)*params.getRawParameterValue("style"));
    s.choir = (choir::Choir)juce::jlimit(0, 4, (int)*params.getRawParameterValue("choir"));
    s.amateur = *params.getRawParameterValue("amateur") > 0.5f;
    s.variant = (int)*params.getRawParameterValue("variant");
    s.global = 2;
    return s;
}

void LiveProcessor::revoice(juce::MidiBuffer& out, int sample) {
    std::vector<int> keys;
    for (int k = 0; k < 128; ++k)
        if (held_[k] > 0 || sustained_.count(k)) keys.push_back(k);
    bool perVoice = (int)*params.getRawParameterValue("channels") == 1;
    choir::LiveVoicing v;
    if (!keys.empty()) v = voicer_.voice(keys, settings(), *params.getRawParameterValue("topmelody") > 0.5f);
    size_t n = std::max(sounding_.size(), v.pitches.size());
    sounding_.resize(n, -1);
    channels_.resize(n, 1);
    for (size_t i = 0; i < n; ++i) {
        int want = i < v.pitches.size() ? v.pitches[i] : -1;
        int ch = perVoice ? (int)i + 1 : 1;
        if (sounding_[i] == want && channels_[i] == ch) continue;
        if (sounding_[i] >= 0) out.addEvent(juce::MidiMessage::noteOff(channels_[i], sounding_[i]), sample);
        if (want >= 0) out.addEvent(juce::MidiMessage::noteOn(ch, want, (juce::uint8)lastVel_), sample);
        sounding_[i] = want;
        channels_[i] = ch;
    }
    if (keys.empty()) voicer_.reset();
    const juce::SpinLock::ScopedLockType l(snapLock_);
    snap_.names = v.names;
    snap_.pitches = v.pitches;
    snap_.chord = v.chord;
}

void LiveProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    buffer.clear();
    const int n = buffer.getNumSamples() > 0 ? buffer.getNumSamples() : 512;
    const long long settle = (long long)(SETTLE_MS * 0.001 * sampleRate_);
    juce::MidiBuffer out;
    for (const auto m : midi) {
        auto msg = m.getMessage();
        const long long at = clock_ + m.samplePosition;
        if (pendingAt_ >= 0 && at >= pendingAt_) {
            revoice(out, (int)juce::jlimit<long long>(0, n - 1, pendingAt_ - clock_));
            pendingAt_ = -1;
        }
        if (msg.isNoteOn()) {
            bool fresh = true;
            for (int k = 0; k < 128; ++k)
                if (held_[k] > 0) fresh = false;
            if (fresh && pedal_) sustained_.clear();     // a new chord under the pedal replaces the old one
            held_[msg.getNoteNumber()]++;
            lastVel_ = msg.getVelocity();
            pendingAt_ = at + settle;
        } else if (msg.isNoteOff()) {
            int k = msg.getNoteNumber();
            if (held_[k] > 0) held_[k]--;
            if (pedal_ && held_[k] == 0) sustained_.insert(k);
            else pendingAt_ = at + settle;
        } else if (msg.isSustainPedalOn()) {
            pedal_ = true;
        } else if (msg.isSustainPedalOff()) {
            pedal_ = false;
            if (!sustained_.empty()) {
                sustained_.clear();
                pendingAt_ = at + settle;
            }
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            held_.fill(0);
            sustained_.clear();
            pendingAt_ = at;
        } else {
            // controllers, pitch bend, aftertouch: to every channel the voices use
            if (msg.getChannel() > 0 && (int)*params.getRawParameterValue("channels") == 1) {
                for (int ch = 1; ch <= 4; ++ch) {
                    auto c = msg;
                    c.setChannel(ch);
                    out.addEvent(c, m.samplePosition);
                }
            } else {
                out.addEvent(msg, m.samplePosition);
            }
        }
    }
    if (pendingAt_ >= 0 && pendingAt_ < clock_ + n) {
        revoice(out, (int)juce::jlimit<long long>(0, n - 1, pendingAt_ - clock_));
        pendingAt_ = -1;
    }
    clock_ += n;
    midi.swapWith(out);
}

LiveProcessor::Snapshot LiveProcessor::snapshot() const {
    const juce::SpinLock::ScopedLockType l(snapLock_);
    return snap_;
}

void LiveProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto x = params.copyState().createXml();
    copyXmlToBinary(*x, dest);
}

void LiveProcessor::setStateInformation(const void* data, int size) {
    if (auto x = getXmlFromBinary(data, size))
        if (x->hasTagName(params.state.getType())) params.replaceState(juce::ValueTree::fromXml(*x));
}

juce::AudioProcessorEditor* LiveProcessor::createEditor() { return new LiveEditor(*this); }

#ifndef CHOIR_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LiveProcessor(); }
#endif
