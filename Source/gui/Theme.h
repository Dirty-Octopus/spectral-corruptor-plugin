// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <vector>

namespace scrr::gui {
struct Theme
{
    static juce::Colour black() { return juce::Colour (0xff101010); }
    static juce::Colour panel() { return juce::Colour (0xff202020); }
    static juce::Colour field() { return juce::Colour (0xff080808); }
    static juce::Colour line() { return juce::Colour (0xff575757); }
    static juce::Colour white() { return juce::Colour (0xfff6f6ef); }
    static juce::Colour dim() { return juce::Colour (0xffb8b8b0); }
    static juce::Colour inactive() { return juce::Colour (0xff979797); }
    static juce::Colour red() { return juce::Colour (0xffff392b); }
    static juce::Colour yellow() { return juce::Colour (0xffffdf00); }
    static juce::Colour blue() { return juce::Colour (0xff164cff); }
    static juce::Colour channelColour (int channel)
    { return channel % 4 == 0 ? red() : channel % 4 == 1 ? yellow() : channel % 4 == 2 ? blue() : white(); }
    static juce::Font displayFont (float size)
    { return juce::Font (juce::FontOptions ("Impact", size, juce::Font::plain)); }
    static juce::Font font (float size = 13.0f, bool bold = false)
    { return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)); }
    static void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> area,
                      float size, juce::Colour colour, bool bold = false,
                      juce::Justification align = juce::Justification::centredLeft)
    { g.setColour (colour); g.setFont (font (size, bold)); g.drawText (s, area, align); }
};

class InstrumentLookAndFeel : public juce::LookAndFeel_V4, private juce::Timer
{
public:
    InstrumentLookAndFeel()
    {
        setColour (juce::Slider::thumbColourId, Theme::yellow());
        setColour (juce::Slider::trackColourId, Theme::yellow());
        setColour (juce::Slider::backgroundColourId, Theme::line());
        setColour (juce::Slider::textBoxTextColourId, Theme::white());
        setColour (juce::Slider::textBoxBackgroundColourId, Theme::field());
        setColour (juce::Slider::textBoxOutlineColourId, Theme::line());
        setColour (juce::Label::textColourId, Theme::white());
        setColour (juce::TextButton::buttonColourId, Theme::panel());
        setColour (juce::TextButton::buttonOnColourId, Theme::yellow());
        setColour (juce::TextButton::textColourOffId, Theme::white());
        setColour (juce::TextButton::textColourOnId, Theme::black());
        setColour (juce::ToggleButton::textColourId, Theme::white());
        setColour (juce::ToggleButton::tickColourId, Theme::yellow());
        setColour (juce::ComboBox::backgroundColourId, Theme::field());
        setColour (juce::ComboBox::textColourId, Theme::white());
        setColour (juce::ComboBox::outlineColourId, Theme::line());
        setColour (juce::ComboBox::arrowColourId, Theme::yellow());
        setColour (juce::PopupMenu::backgroundColourId, Theme::black());
        setColour (juce::PopupMenu::textColourId, Theme::white());
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::blue());
        setColour (juce::PopupMenu::highlightedTextColourId, Theme::white());
        setColour (juce::PopupMenu::headerTextColourId, Theme::yellow());
        setColour (juce::TextEditor::textColourId, Theme::white());
        setColour (juce::TextEditor::backgroundColourId, Theme::field());
        setColour (juce::TextEditor::outlineColourId, Theme::line());
        setColour (juce::TextEditor::focusedOutlineColourId, Theme::yellow());
        setColour (juce::AlertWindow::backgroundColourId, Theme::panel());
        setColour (juce::AlertWindow::textColourId, Theme::white());
        setColour (juce::ScrollBar::thumbColourId, Theme::line());
    }
    ~InstrumentLookAndFeel() override { stopTimer(); }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& colour,
                               bool over, bool down) override
    {
        const bool interactive = b.isEnabled() && ! b.isCurrentlyBlockedByAnotherModalComponent();
        over = over && interactive; down = down && interactive;
        const float hover = buttonHover (b, over);
        g.setColour (b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : down ? Theme::blue() : colour.interpolatedWith (Theme::white(), hover * .12f));
        g.fillRect (b.getLocalBounds());
        g.setColour (over ? Theme::white() : Theme::line());
        g.drawRect (b.getLocalBounds());
        if (hover > .01f) { g.setColour (Theme::white()); g.fillRect (0, b.getHeight() - 2, (int) ((float) b.getWidth() * hover), 2); }
    }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return Theme::font (juce::jmin (14.0f, (float) h * .45f), true); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::font (13.0f); }
    juce::Font getPopupMenuFont() override { return Theme::font (14.0f); }
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        g.fillAll (Theme::field()); g.setColour (box.hasKeyboardFocus (true) ? Theme::yellow() : Theme::line());
        g.drawRect (0, 0, w, h); Theme::text (g, "v", { w - 24, 0, 20, h }, 12, box.findColour (juce::ComboBox::arrowColourId));
    }
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        auto box = juce::Rectangle<int> (2, (b.getHeight() - 14) / 2, 14, 14);
        g.setColour (b.getToggleState() ? b.findColour (juce::ToggleButton::tickColourId) : Theme::field()); g.fillRect (box);
        g.setColour (over ? Theme::white() : Theme::line()); g.drawRect (box);
        if (b.getToggleState()) { g.setColour (Theme::black()); g.fillRect (box.reduced (4)); }
        Theme::text (g, b.getButtonText(), { 24, 0, b.getWidth() - 24, b.getHeight() }, 13, Theme::white());
    }
    juce::Slider::SliderLayout getSliderLayout (juce::Slider& slider) override
    {
        auto layout = juce::LookAndFeel_V4::getSliderLayout (slider);
        const int height = (int) slider.getProperties().getWithDefault ("modulationHeight", 0);
        if (height > 0)
        {
            layout.sliderBounds = layout.sliderBounds.withTrimmedTop (height);
            if (slider.getTextBoxPosition() == juce::Slider::TextBoxAbove)
            {
                layout.textBoxBounds.translate (0, height);
            }
            else if (slider.getTextBoxPosition() != juce::Slider::TextBoxBelow)
            {
                layout.textBoxBounds.setY (height + (slider.getHeight() - height - layout.textBoxBounds.getHeight()) / 2);
            }
        }
        return layout;
    }
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        const float cy = (float) y + (float) h * .5f;
        g.setColour (slider.findColour (juce::Slider::backgroundColourId)); g.fillRect ((float) x, cy - 2, (float) w, 4.0f);
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRect ((float) x, cy - 2, juce::jmax (0.0f, pos - (float) x), 4.0f);
        g.setColour (slider.findColour (juce::Slider::thumbColourId)); g.fillRect (pos - 3, cy - 7, 6.0f, 14.0f);
    }
private:
    struct ButtonMotion
    {
        juce::Component::SafePointer<juce::Button> button;
        double started {};
        float from {}, value {};
        bool over {};

        void advance (bool target, double now)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (float) ((now - started) / 140.0));
            value = t >= 1 ? (over ? 1.0f : 0.0f)
                          : from + ((over ? 1.0f : 0.0f) - from) * (1 - std::pow (1 - t, 3.0f));
            if (target != over)
            {
                // Reverse from the current point in time, even between paint callbacks.
                from = value; started = now; over = target;
            }
        }
    };
    std::vector<ButtonMotion> buttonMotions;

    float buttonHover (juce::Button& button, bool over)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (auto& motion : buttonMotions)
            if (motion.button == &button) { motion.advance (over, now); return motion.value; }
        if (! over) return 0;
        buttonMotions.push_back ({ &button, now, 0, 0, true });
        if (! isTimerRunning()) startTimerHz (60);
        return 0;
    }
    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (auto it = buttonMotions.begin(); it != buttonMotions.end();)
        {
            auto* button = it->button.getComponent();
            if (button == nullptr || &button->getLookAndFeel() != this || ! button->isShowing())
            {
                // A hidden/replaced/deleted editor must not retain an animation or a raw pointer.
                if (button != nullptr) button->repaint();
                it = buttonMotions.erase (it);
                continue;
            }
            const float previous = it->value;
            it->advance (button->isOver() && button->isEnabled()
                         && ! button->isCurrentlyBlockedByAnotherModalComponent(), now);
            if (std::abs (it->value - previous) > .00001f) button->repaint();
            if (! it->over && it->value <= 0)
            {
                // Explicitly schedule the final idle frame: no unrelated repaint should be needed.
                button->repaint(); it = buttonMotions.erase (it);
            }
            else ++it;
        }
        if (buttonMotions.empty()) stopTimer();
    }
};

inline juce::ModifierKeys resetModifier()
{
   #if JUCE_MAC
    return juce::ModifierKeys (juce::ModifierKeys::commandModifier);
   #else
    return juce::ModifierKeys (juce::ModifierKeys::ctrlModifier);
   #endif
}
inline void setupParameterGestures (juce::Slider& slider)
{
    slider.setVelocityModeParameters (1.0, 1, 0.0, true, juce::ModifierKeys::altModifier);
    slider.setDoubleClickReturnValue (slider.isDoubleClickReturnEnabled(), slider.getDoubleClickReturnValue(), resetModifier());
}
inline void setupSlider (juce::Slider& slider, double min, double max, double step, double def)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 24);
    slider.setColour (juce::Slider::textBoxTextColourId, Theme::white());
    slider.setColour (juce::Slider::textBoxBackgroundColourId, Theme::field());
    slider.setColour (juce::Slider::textBoxOutlineColourId, Theme::line());
    slider.setColour (juce::Slider::thumbColourId, Theme::white());
    slider.setRange (min, max, step);
    slider.setDoubleClickReturnValue (true, def, resetModifier());
    setupParameterGestures (slider);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.setScrollWheelEnabled (false);
}
} // namespace scrr::gui
