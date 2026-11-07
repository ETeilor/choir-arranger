#include "Look.h"

// ---------------------------------------------------------------- look
ChoirLook::ChoirLook() {
    setColour(juce::ResizableWindow::backgroundColourId, ui::bg);
    setColour(juce::ComboBox::backgroundColourId, ui::panelHi);
    setColour(juce::ComboBox::outlineColourId, ui::line);
    setColour(juce::ComboBox::textColourId, ui::text);
    setColour(juce::ComboBox::arrowColourId, ui::textDim);
    setColour(juce::PopupMenu::backgroundColourId, ui::panelHi);
    setColour(juce::PopupMenu::textColourId, ui::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, ui::accent.withAlpha(0.25f));
    setColour(juce::PopupMenu::highlightedTextColourId, ui::text);
    setColour(juce::Label::textColourId, ui::text);
    setColour(juce::TextButton::textColourOffId, ui::text);
    setColour(juce::TextButton::textColourOnId, ui::bg);
    setColour(juce::ToggleButton::textColourId, ui::text);
    setColour(juce::ToggleButton::tickColourId, ui::accent);
    setColour(juce::ToggleButton::tickDisabledColourId, ui::textDim);
    setColour(juce::Slider::textBoxTextColourId, ui::text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void ChoirLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1,
                                 juce::Slider& s) {
    auto b = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(4.0f);
    float r = std::min(b.getWidth(), b.getHeight()) / 2.0f;
    auto c = b.getCentre();
    float lw = r > 30 ? 5.0f : 3.5f;
    juce::Path track, val;
    track.addCentredArc(c.x, c.y, r - lw, r - lw, 0, a0, a1, true);
    g.setColour(ui::line);
    g.strokePath(track, juce::PathStrokeType(lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    bool automatic = s.getMinimum() < 0 && s.getValue() < 0;
    if (!automatic) {
        float a = a0 + pos * (a1 - a0);
        val.addCentredArc(c.x, c.y, r - lw, r - lw, 0, a0, a, true);
        auto col = s.getProperties().contains("colour") ? juce::Colour((juce::uint32)(juce::int64)s.getProperties()["colour"]) : ui::accent;
        g.setColour(col);
        g.strokePath(val, juce::PathStrokeType(lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour(automatic ? ui::textDim : ui::text);
    g.setFont(juce::FontOptions(r > 30 ? 26.0f : 13.0f, r > 30 ? juce::Font::bold : juce::Font::plain));
    g.drawText(automatic ? juce::String("=") : juce::String((int)s.getValue()), b, juce::Justification::centred);
}

void ChoirLook::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    bool on = b.getToggleState();
    auto base = on ? (b.getProperties().contains("danger") ? juce::Colour(0xffd0574b) : ui::accent) : ui::panelHi;
    if (hi) base = base.brighter(0.08f);
    if (down) base = base.darker(0.1f);
    g.setColour(base);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(ui::line);
    if (!on) g.drawRoundedRectangle(r, 6.0f, 1.0f);
}


// ---------------------------------------------------------------- menu names (same order as the engine enums)
juce::StringArray names::choirs() {
    return {U("Смешанный SATB"), U("Женский SSAA"), U("Мужской TTBB"), U("Трёхголосный SAB"), U("Детский SSA")};
}

juce::StringArray names::styles() {
    // same order as choir::Style
    return {U("Хорал Баха"), U("Православный обиход"), U("Григорианский хорал"), U("Знаменный распев (гармонизация)"),
            U("Византийский напев с исоном"), U("Ренессансная полифония"), U("Contemporary (Уитакр, Лауридсен)"),
            U("Госпел"), U("Барбершоп"), U("Кинематографический эпик"), U("Партесный концерт (Бортнянский)"),
            U("Спиричуэл"), U("Англиканское пение"), U("Sacred Harp (фигурные ноты)"), U("Болгарский женский хор"),
            U("Грузинское многоголосие"), U("Тинтиннабули (Пярт)"), U("Джазовый close harmony"), U("A cappella поп"),
            U("Сардинский cantu a tenore"), U("Советская эстрада и кино")};
}
