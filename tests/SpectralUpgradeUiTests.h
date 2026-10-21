// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

TEST_CASE ("Partial effects remain ordinary rack stages with editable split bypass state")
{
    using namespace scrr::gui;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    EffectBrowser library; library.setSize (600, 400);
    REQUIRE (findID (library, "add-HarmonicMatch") != nullptr);
    REQUIRE (findID (library, "add-PartialStretcher") == nullptr);
    auto* partialFolder = findButton (library, "PARTIAL"); REQUIRE (partialFolder != nullptr);
    partialFolder->onClick();
    REQUIRE (findID (library, "add-HarmonicMatch") == nullptr);
    REQUIRE (findID (library, "add-PartialMetal") == nullptr);
    for (const auto* type : { "PartialStretcher", "PartialStiffString", "PartialCentroid", "PartialPGrid", "PartialFGrid" })
    { REQUIRE (effectFolder (type) == "PARTIAL"); REQUIRE (findID (library, "add-" + juce::String (type)) != nullptr); }
    auto p = testProcessor(); p->addModule (1, "PartialStretcher");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor());
    editor->setSize (1040, 780); editor->addToDesktop (0); editor->setVisible (true); pump (100);
    auto* status = dynamic_cast<juce::Label*> (findID (*editor, "effect-processing-status")); REQUIRE (status != nullptr);
    REQUIRE (status->isVisible()); REQUIRE (status->getText().contains ("BYPASSED"));
    REQUIRE (findID (*editor, "rangeLow") == nullptr); REQUIRE (findID (*editor, "rangeHigh") == nullptr); REQUIRE (findID (*editor, "moduleMix") == nullptr);
    for (const auto* key : { "mode", "anchor", "reference", "amount" }) REQUIRE (findID (*editor, key) != nullptr);
    auto* amount = dynamic_cast<ParameterControl*> (findID (*editor, "amount")); REQUIRE (amount != nullptr);
    REQUIRE (amount->getAssignableSlider() != nullptr); REQUIRE (amount->getAssignableSlider()->isEnabled());
    amount->getAssignableSlider()->setValue (9, juce::sendNotificationSync);
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["amount"] - 9) < .001);
    setParam (*p, scrr::params::id::freqSplitNumSplits, 8);
    REQUIRE (waitForUI ([&] { return status->getText().contains ("8 SLICES / ACTIVE"); }));
    setParam (*p, scrr::params::id::freqSplitSolo, 1);
    REQUIRE (waitForUI ([&] { return status->getText().contains ("ENABLE POST SPLIT"); }));
    auto* row = dynamic_cast<ModuleRow*> (findID (*editor, "stage-" + uid)); REQUIRE (row != nullptr);
    REQUIRE (row->getStatusText().contains ("SOLO / BYPASS")); REQUIRE (amount->getAssignableSlider()->isEnabled());
    setParam (*p, scrr::params::id::freqSplitPostPreview, 1);
    REQUIRE (waitForUI ([&] { return status->getText().contains ("8 SLICES / ACTIVE"); }));
    setParam (*p, scrr::params::id::freqSplitSolo, 0);
    auto* mode = findID (*editor, "mode"); REQUIRE (mode != nullptr); juce::ComboBox* modeChoice {};
    for (auto* child : mode->getChildren()) if (auto* combo = dynamic_cast<juce::ComboBox*> (child)) modeChoice = combo;
    REQUIRE (modeChoice != nullptr); modeChoice->setSelectedItemIndex (1, juce::sendNotificationSync);
    REQUIRE ((int) p->getModuleChain (1).getChild (0)["mode"] == 1);
    const auto source = p->addModulator (false);
    auto* reference = dynamic_cast<ParameterControl*> (findID (*editor, "reference")); REQUIRE (reference != nullptr);
    REQUIRE (reference->getAssignableSlider()->dropModulationSource (source));
    const auto routes = p->getMacroMappings(); REQUIRE (routes.size() == 1);
    REQUIRE (routes.front().uid == uid); REQUIRE (routes.front().sourceUid == source); REQUIRE (routes.front().parameter.toString() == "reference");
    setParam (*p, scrr::params::id::freqSplitNumSplits, 1);
    REQUIRE (waitForUI ([&] { return status->getText().contains ("BYPASSED"); }));
}

TEST_CASE ("Resource-limited stage rows distinguish actual bypass from user disable")
{
    using scrr::gui::ModuleRow;
    ModuleRow spectral ("SPECTRAL DISPERSION", "dispersion", 0, true, true);
    spectral.setResourceLimited (true); REQUIRE (spectral.getStatusText() == "RESOURCE LIMIT");
    spectral.setResourceLimited (false); REQUIRE (spectral.getStatusText() == "FFT / ON");
    ModuleRow partial ("PARTIAL STRETCHER", "stretcher", 1, true, false); partial.partial = true;
    partial.setPartialStatus (true, false); partial.setResourceLimited (true); REQUIRE (partial.getStatusText() == "RESOURCE LIMIT");
    partial.setResourceLimited (false); REQUIRE (partial.getStatusText() == "PARTIAL / ON");
    partial.setPartialStatus (true, true); REQUIRE (partial.getStatusText() == "PARTIAL / SOLO / BYPASS");
    ModuleRow disabled ("SPECTRAL DISPERSION", "disabled", 2, false, false);
    disabled.setResourceLimited (true); REQUIRE (disabled.getStatusText() == "FFT / OFF");
}

TEST_CASE ("Centroid and frequency grid inspectors omit an inert reference control")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    for (const auto* type : { "PartialCentroid", "PartialFGrid" })
    {
        auto p = testProcessor(); p->addModule (1, type);
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1040, 780);
        editor->addToDesktop (0); editor->setVisible (true); pump (80);
        REQUIRE (findID (*editor, "reference") == nullptr);
        for (const auto* key : { "mode", "anchor", "amount" }) REQUIRE (findID (*editor, key) != nullptr);
        REQUIRE (findID (*editor, juce::String (type) == "PartialCentroid" ? "target" : "pitch") != nullptr);
    }
}

TEST_CASE ("Partial waveform choice is available only for Resynth and survives saved state")
{
    using namespace scrr::gui;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto processor = testProcessor(); processor->addModule (1, "PartialStretcher");
    const auto uid = processor->getModuleChain (1).getChild (0)["uid"].toString();
    REQUIRE ((int) processor->getModuleChain (1).getChild (0)["shape"] == 0);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (processor->createEditor()); editor->setSize (1040, 780); editor->addToDesktop (0); editor->setVisible (true); pump (80);
    auto* shape = findID (*editor, "shape"); REQUIRE (shape != nullptr); REQUIRE (! shape->isEnabled());
    processor->setModuleParameter (1, uid, "mode", 1);
    REQUIRE (waitForUI ([&] { auto* control = findID (*editor, "shape"); return control != nullptr && control->isEnabled(); }));
    shape = findID (*editor, "shape"); juce::ComboBox* choice = nullptr;
    for (auto* child : shape->getChildren()) if (auto* candidate = dynamic_cast<juce::ComboBox*> (child)) choice = candidate;
    REQUIRE (choice != nullptr); REQUIRE (choice->getNumItems() == 4);
    REQUIRE (choice->getItemText (0) == "Sine"); REQUIRE (choice->getItemText (1) == "Triangle"); REQUIRE (choice->getItemText (2) == "Saw"); REQUIRE (choice->getItemText (3) == "Square");
    choice->setSelectedItemIndex (3, juce::sendNotificationSync);
    REQUIRE ((int) processor->getModuleChain (1).getChild (0)["shape"] == 3);
    const auto preset = processor->copyPresetState(); auto restored = testProcessor(); REQUIRE (restored->applyPreset (preset));
    REQUIRE ((int) restored->getModuleChain (1).getChild (0)["shape"] == 3);
    juce::MemoryBlock state; processor->getStateInformation (state); restored->setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE ((int) restored->getModuleChain (1).getChild (0)["shape"] == 3);
    processor->setModuleParameter (1, uid, "mode", 0);
    REQUIRE (waitForUI ([&] { auto* control = findID (*editor, "shape"); return control != nullptr && ! control->isEnabled(); }));
    REQUIRE ((int) processor->getModuleChain (1).getChild (0)["shape"] == 3);
    auto legacy = preset.createCopy();
    // Use a real module subtree regardless of the surrounding preset version.
    auto removeShape = [&] (auto&& self, juce::ValueTree tree) -> void
    {
        if (tree.getProperty ("uid").toString() == uid) tree.removeProperty ("shape", nullptr);
        for (int i = 0; i < tree.getNumChildren(); ++i) self (self, tree.getChild (i));
    };
    removeShape (removeShape, legacy); REQUIRE (restored->applyPreset (legacy));
    REQUIRE ((int) restored->getModuleChain (1).getChild (0)["shape"] == 0);
}

inline std::unique_ptr<SpectralCrrptProcessor> spectralUpgradeResourceProcessor()
{
    auto p = testProcessor(); using namespace scrr::params;
    setParam (*p, id::oversample, 0); setParam (*p, id::fftSizeExtended, 9); setParam (*p, id::freqSplitNumSplits, 64);
    p->addModule (1, "PartialStretcher"); p->addModule (1, "SpectralDispersion"); p->addModule (1, "SpectralFilter");
    p->prepareToPlay (48000, 128);
    juce::AudioBuffer<float> audio (2, 128); audio.clear(); juce::MidiBuffer midi; p->processBlock (audio, midi);
    return p;
}

TEST_CASE ("Resource bypass names only the affected stage and recovers without locking controls")
{
    using namespace scrr::gui;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = spectralUpgradeResourceProcessor(); const auto chain = p->getModuleChain (1);
    const auto partialUid = chain.getChild (0)["uid"].toString(), dispersionUid = chain.getChild (1)["uid"].toString(), filterUid = chain.getChild (2)["uid"].toString();
    REQUIRE (p->getProcessor().getModuleResourceStatus (1, dispersionUid).blocked);
    REQUIRE (! p->getProcessor().getModuleResourceStatus (1, partialUid).blocked);
    REQUIRE (! p->getProcessor().getModuleResourceStatus (1, filterUid).blocked);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1040, 780);
    editor->addToDesktop (0); editor->setVisible (true); pump (100);
    auto* row = dynamic_cast<ModuleRow*> (findID (*editor, "stage-" + dispersionUid)); REQUIRE (row != nullptr);
    REQUIRE (row->getStatusText() == "RESOURCE LIMIT");
    row->mouseDown (mouse (*row, { 50, 20 })); row->mouseUp (mouse (*row, { 50, 20 })); pump (100);
    auto* status = dynamic_cast<juce::Label*> (findID (*editor, "effect-processing-status")); REQUIRE (status != nullptr);
    REQUIRE (status->isVisible()); REQUIRE (status->getText().contains ("BYPASSED / SPECTRAL DISPERSION"));
    REQUIRE (status->getText().contains ("NEEDS")); REQUIRE (status->getText().contains ("AVAILABLE")); REQUIRE (status->getText().contains ("MiB"));
    REQUIRE (status->getHeight() >= 40);
    auto* amount = dynamic_cast<ParameterControl*> (findID (*editor, "amount")); REQUIRE (amount != nullptr);
    REQUIRE (amount->getAssignableSlider()->isEnabled()); amount->getAssignableSlider()->setValue (15, juce::sendNotificationSync);
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (1)["amount"] - 15) < .001);
    setParam (*p, scrr::params::id::freqSplitNumSplits, 4);
    juce::AudioBuffer<float> audio (2, 128); audio.clear(); juce::MidiBuffer midi; p->processBlock (audio, midi);
    REQUIRE (waitForUI ([&] { return ! status->isVisible(); }));
    auto* restored = dynamic_cast<ModuleRow*> (findID (*editor, "stage-" + dispersionUid)); REQUIRE (restored != nullptr);
    REQUIRE (restored->getStatusText() == "FFT / ON");
}

TEST_CASE ("Overlap control reports the actual integer-hop factor at minimum editor size")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); setParam (*p, scrr::params::id::oversample, 0); setParam (*p, scrr::params::id::fftSizeExtended, 6);
    p->prepareToPlay (48000, 128);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1040, 780);
    editor->addToDesktop (0); editor->setVisible (true); pump (80);
    auto* control = dynamic_cast<juce::Slider*> (findID (*editor, "fft-overlap"));
    auto* actual = dynamic_cast<juce::Label*> (findID (*editor, "fft-overlap-actual")); REQUIRE (control && actual);
    REQUIRE (std::abs (control->getMinimum() - 2) < .001); REQUIRE (std::abs (control->getMaximum() - 16) < .001);
    control->setValue (7.35, juce::sendNotificationSync);
    REQUIRE (std::abs (p->getAPVTS().getRawParameterValue (scrr::params::id::overlapFactor)->load() - 7.35) < .001);
    juce::AudioBuffer<float> audio (2, 128); audio.clear(); juce::MidiBuffer midi; p->processBlock (audio, midi);
    const int hop = p->getProcessor().getHopSize(); const double factor = (double) p->getProcessor().getFFTSize() / hop;
    const double overlapPercent = (1.0 - (double) hop / p->getProcessor().getFFTSize()) * 100.0;
    REQUIRE (waitForUI ([&] { return actual->getText().contains (juce::String (factor, 3) + "x")
        && actual->getText().contains (juce::String (overlapPercent, 2) + "%") && actual->getText().contains ("H" + juce::String (hop)); }));
    REQUIRE (control->getWidth() > 120); REQUIRE (actual->getWidth() >= control->getWidth());
    REQUIRE (juce::GlyphArrangement::getStringWidth (actual->getFont(), actual->getText())
        <= (float) (actual->getWidth() - actual->getBorderSize().getLeftAndRight()));
}

inline int renderSpectralUpgradeUi (const juce::File& directory)
{
    if (directory.createDirectory().failed() || juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return 1;
    auto p = testProcessor(); using namespace scrr::params;
    setParam (*p, id::oversample, 0); setParam (*p, id::fftSizeExtended, 6); setParam (*p, id::windowTypeExtended, 7);
    setParam (*p, id::overlapFactor, 6.75); setParam (*p, id::freqSplitNumSplits, 8);
    p->addModule (1, "FrequencyRemap");
    const auto remapUid = p->getModuleChain (1).getChild (0)["uid"].toString();
    const double logRange = std::log (juce::jmin (24000.0, (double) p->getFrequencyLimit()) / 20.0);
    const float input200 = (float) (std::log (10.0) / logRange), output800 = (float) (std::log (40.0) / logRange);
    TransferCurve curve; curve.points = { { 0, 0, -0.3f }, { input200, output800, 0.4f }, { 0.65f, 0.82f, -0.2f }, { 1, 1, 0 } };
    p->setModuleParameter (1, remapUid, "remapCurve", curve.encode());
    for (const auto* type : { "PartialStretcher", "PartialStiffString", "PartialCentroid", "PartialPGrid", "PartialFGrid" })
    {
        p->addModule (1, type); const auto chain = p->getModuleChain (1); const auto uid = chain.getChild (chain.getNumChildren() - 1)["uid"].toString();
        p->setModuleParameter (1, uid, "amount", juce::String (type) == "PartialStretcher" ? 12 : 65);
        p->setModuleParameter (1, uid, "anchor", 1);
    }
    p->prepareToPlay (48000, 128);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1180, 980);
    editor->addToDesktop (0); editor->setVisible (true); editor->toFront (true); pump (250);
    if (auto* guide = findID (*editor, "quick-guide")) guide->setVisible (false);
    if (auto* graph = dynamic_cast<scrr::gui::TransferCurveEditor*> (findID (*editor, "frequency-remap-curve")))
    {
        const auto area = graph->getPlotBounds();
        graph->mouseMove (mouse (*graph, { area.getX() + input200 * area.getWidth(), area.getBottom() - output800 * area.getHeight() }));
    }
    juce::AudioBuffer<float> audio (2, 128); juce::MidiBuffer midi; int sample = 0;
    const auto advance = [&]
    {
        for (int block = 0; block < 30; ++block)
        {
            for (int i = 0; i < 128; ++i, ++sample)
            {
                const float input = .08f * (float) std::sin (juce::MathConstants<double>::twoPi * sample * 220 / 48000)
                                  + .04f * (float) std::sin (juce::MathConstants<double>::twoPi * sample * 660 / 48000);
                audio.setSample (0, i, input); audio.setSample (1, i, input);
            }
            p->processBlock (audio, midi); pump (5);
        }
        pump (180);
    };
    const auto capture = [&] (const juce::String& name)
    {
        advance();
        if (name.startsWith ("frequency-remap"))
            if (auto* graph = dynamic_cast<scrr::gui::TransferCurveEditor*> (findID (*editor, "frequency-remap-curve")))
            {
                const auto area = graph->getPlotBounds();
                graph->mouseMove (mouse (*graph, { area.getX() + input200 * area.getWidth(), area.getBottom() - output800 * area.getHeight() }));
            }
        const auto file = directory.getChildFile (name + ".png");
        artworkMotionTests::writePng (editor->createComponentSnapshot (editor->getLocalBounds()), file);
        std::cout << file.getFullPathName() << '\n';
    };
    capture ("frequency-remap-tall"); editor->setSize (1040, 780); capture ("frequency-remap-minimum");
    editor->setSize (1180, 820);
    const auto chain = p->getModuleChain (1);
    for (int index : { 1, 2, 3, 4, 5 })
    {
        const auto module = chain.getChild (index); const auto uid = module["uid"].toString();
        if (index == 3) p->setModuleParameter (1, uid, "mode", 1);
        auto* row = findID (*editor, "stage-" + uid); if (row == nullptr) return 1;
        row->mouseDown (mouse (*row, { 50, 20 })); row->mouseUp (mouse (*row, { 50, 20 })); pump (100);
        capture (module["type"].toString().toLowerCase() + (index == 3 ? "-resynth" : "-scale"));
    }
    setParam (*p, id::freqSplitNumSplits, 1); editor->setSize (1040, 780); capture ("partial-split-disabled-minimum");
    editor->setSize (1180, 820);
    if (! renderMappingCurves (*p, *editor, directory)) return 1;
    auto resourceProcessor = spectralUpgradeResourceProcessor();
    const auto resourceUid = resourceProcessor->getModuleChain (1).getChild (1)["uid"].toString();
    auto resourceEditor = std::unique_ptr<juce::AudioProcessorEditor> (resourceProcessor->createEditor());
    resourceEditor->setSize (1040, 780); resourceEditor->addToDesktop (0); resourceEditor->setVisible (true); resourceEditor->toFront (true); pump (180);
    if (auto* guide = findID (*resourceEditor, "quick-guide")) guide->setVisible (false);
    if (auto* row = findID (*resourceEditor, "stage-" + resourceUid))
    { row->mouseDown (mouse (*row, { 50, 20 })); row->mouseUp (mouse (*row, { 50, 20 })); pump (350); }
    else return 1;
    const auto resourceFile = directory.getChildFile ("resource-limit-dispersion-minimum.png");
    artworkMotionTests::writePng (resourceEditor->createComponentSnapshot (resourceEditor->getLocalBounds()), resourceFile);
    std::cout << resourceFile.getFullPathName() << '\n';
    return 0;
}
