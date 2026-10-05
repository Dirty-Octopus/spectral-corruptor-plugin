// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "../Theme.h"
#include "../AssignableSlider.h"
#include "../FrequencyScale.h"
#include "../../parameters/MacroMapping.h"
#include <optional>
#include "../../modules/ModuleSpecs.h"

namespace scrr::gui {
// Controls hold local values, never references into the processor's mutable tree.
class ParameterControl : public juce::Component
{
public:
    ParameterControl (const scrr::dsp::ParamSpec& spec, juce::var value,
                      std::function<void(const juce::var&)> callback)
    {
        baseLabel = spec.label;
        setComponentID (spec.id);
        label.setText (spec.label, juce::dontSendNotification);
        label.setFont (Theme::font (12.0f)); label.setColour (juce::Label::textColourId, Theme::dim());
        addAndMakeVisible (label);
        if (spec.type == scrr::dsp::ParamSpec::Bool)
        {
            toggle.setButtonText (spec.label); toggle.setToggleState ((bool) value, juce::dontSendNotification);
            toggle.onClick = [this, callback] { callback (toggle.getToggleState()); };
            addAndMakeVisible (toggle); label.setVisible (false);
        }
        else if (spec.type == scrr::dsp::ParamSpec::Choice)
        {
            combo.addItemList (spec.choices, 1); combo.setSelectedItemIndex ((int) value, juce::dontSendNotification);
            combo.onChange = [this, callback] { callback (combo.getSelectedItemIndex()); };
            addAndMakeVisible (combo);
        }
        else
        {
            numeric = true;
            setupSlider (slider, spec.minVal, spec.maxVal, spec.step, spec.defaultVal);
            if (spec.unit.trim() == "Hz") setupFrequencyScale (slider);
            else if (spec.logScale && spec.minVal > 0) slider.setSkewFactorFromMidPoint (std::sqrt ((double) spec.minVal * spec.maxVal));
            slider.setTextValueSuffix (spec.unit);
            if (spec.id == "rangeHigh")
            {
                slider.setTextValueSuffix ({});
                slider.textFromValueFunction = [maximum = spec.maxVal] (double v) { return v >= maximum ? juce::String ("FULL") : juce::String ((int) v) + " Hz"; };
                slider.valueFromTextFunction = [maximum = spec.maxVal] (const juce::String& t) { return t.containsIgnoreCase ("full") ? (double) maximum : t.getDoubleValue(); };
            }
            slider.setValue ((double) value, juce::dontSendNotification);
            slider.onValueChange = [this, callback, fullRange = spec.id == "rangeHigh"]
            {
                // Preserve the legacy FULL sentinel; changing the global cap
                // must not destructively clamp saved module values.
                callback (fullRange && slider.getValue() >= slider.getMaximum() ? 96000.0 : slider.getValue());
            };
            slider.setTooltip (spec.label + ": drag or type a value. Alt/Option-drag: fine. Cmd/Ctrl-click or double-click: reset.");
            addAndMakeVisible (slider);
        }
    }
    AssignableSlider* getAssignableSlider() { return numeric ? &slider : nullptr; }
    void setMacroMappings (const std::vector<scrr::params::MacroMapping>& mappings) { slider.setMappings (mappings); }
    int preferredHeight() const { return 46 + slider.modulationHeight(); }
    void refreshMacro (const scrr::params::ModulationValues& values) { slider.refreshModulation (values); }
    void setAccent (juce::Colour colour)
    {
        slider.setColour (juce::Slider::trackColourId, colour);
        combo.setColour (juce::ComboBox::arrowColourId, colour);
        toggle.setColour (juce::ToggleButton::tickColourId, colour);
    }
    void resized() override
    {
        auto area = getLocalBounds(); label.setBounds (area.removeFromTop (18));
        slider.setBounds (area); combo.setBounds (area.reduced (2, 1)); toggle.setBounds (getLocalBounds());
    }
private:
    juce::String baseLabel;
    bool numeric {};
    juce::Label label; AssignableSlider slider; juce::ComboBox combo; juce::ToggleButton toggle;
};

inline juce::String moduleDescription (const juce::String& type)
{
    if (type == "Notes") return "Markdown preset instructions. Text only; does not process audio.";
    if (type == "Reverb") return "Per-channel algorithmic reverb after spectral processing. Plate, Hall and Room use differently voiced delay networks.";
    if (type == "Delay") return "Per-channel delay. Tap mode adds three echoes; Ping-Pong alternates left and right. Feedback is limited below self-oscillation.";
    if (type == "Compressor") return "Stereo-linked peak compressor with soft knee, attack, release and parallel mix.";
    if (type == "Distortion") return "Tube, Overdrive, Sin Fold or Lin Fold. Drive shapes the wave; Tone filters the result. Use oversampling for smoother high frequencies.";
    if (type == "SpectralMirror") return "Reflect frequencies around Mirror Centre: output = 2 x centre - input. Frequencies outside the spectrum are removed.";
    if (type == "SpectralBloom") return "Spread spectral energy into neighbouring frequencies. Spread sets the radius in Hz; Amount blends the power envelopes.";
    if (type == "SpectralComb") return "A repeating spectral gate. Spacing sets the tooth period in Hz; Offset moves the pattern; Width opens each tooth.";
    if (type == "HarmonicMatch") return "Match a harmonic envelope. Scale reshapes existing partials; Resynth fills from the fundamental. Best on single notes.";
    if (type == "HarmonicSculpt") return "Track a single note and shape its odd and even harmonics. Turn Auto Track off to tune the fundamental manually.";
    if (type == "SpectralContrast") return "Positive: focus on strong partials. Negative: bring out quiet detail. Overall spectral energy is compensated.";
    if (type == "FrequencyShift") return "Move all selected frequencies by a fixed number of Hz. Positive rises; negative falls. Creates inharmonic metallic tones.";
    if (type == "Utility") return "Stereo / gain utility on this channel after spectral processing.";
    if (type == "TemporalSmear") return "Average spectral frames for lingering textures. Length is measured in FFT frames.";
    if (type == "SpectralFilter") return "Filter the spectrum with a selectable shape, cutoff and slope.";
    if (type == "BinHold" || type == "FrameHold") return "Capture and repeat spectral material. Rate and Length are measured in FFT frames.";
    return "Process the selected frequency range. Reverse Low / High to affect frequencies outside the range. Mix blends this stage.";
}
} // namespace scrr::gui
