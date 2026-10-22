// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

TEST_CASE ("Undo and redo compare saved contents and preserve the future until a real edit")
{
    auto p = testProcessor(); REQUIRE (! p->canUndo()); REQUIRE (! p->canRedo());
    p->addModule (1, "FrequencyShift"); REQUIRE (p->canUndo()); REQUIRE (! p->canRedo());
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->setModuleParameter (1, uid, "shift", 100.0); p->setModuleParameter (1, uid, "shift", 250.0);
    REQUIRE (p->undo()); REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["shift"] - 100) < .001);
    REQUIRE (p->canUndo()); REQUIRE (p->canRedo());
    REQUIRE (p->redo()); REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["shift"] - 250) < .001);
    REQUIRE (! p->redo());
    REQUIRE (p->undo()); REQUIRE (p->undo());
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["shift"]) < .001);
    REQUIRE (p->redo());
    p->setModuleParameter (1, uid, "shift", 175.0);
    REQUIRE (p->canUndo()); REQUIRE (! p->canRedo());
    REQUIRE (! p->redo()); REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["shift"] - 175) < .001);
    REQUIRE (p->undo()); REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["shift"] - 100) < .001);
}

TEST_CASE ("The first native edit enables Undo and a fresh branch disables Redo")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); REQUIRE (! p->canUndo()); REQUIRE (! p->canRedo());
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor());
    auto* undo = findButton (*editor, "UNDO"), *redo = findButton (*editor, "REDO"); REQUIRE (undo && redo);
    // Constructor state must be correct even before the first 30 Hz timer tick.
    REQUIRE (! undo->isEnabled()); REQUIRE (! redo->isEnabled());
    editor->addToDesktop (0); editor->setVisible (true);
    REQUIRE (waitForUI ([&] { return ! undo->isEnabled() && ! redo->isEnabled(); }));
    REQUIRE (! p->canUndo()); REQUIRE (! p->canRedo());
    p->addModule (1, "FrequencyShift"); REQUIRE (waitForUI ([&] { return undo->isEnabled(); }));
    undo->triggerClick(); REQUIRE (waitForUI ([&] { return redo->isEnabled() && p->getModuleChain (1).getNumChildren() == 0; }));
    REQUIRE (! undo->isEnabled());
    p->addModule (1, "SpectralContrast"); REQUIRE (waitForUI ([&] { return undo->isEnabled() && ! redo->isEnabled(); }));
    auto globals = testProcessor();
    setParam (*globals, scrr::params::id::inputGain, 6.0f); REQUIRE (globals->canUndo());
    REQUIRE (globals->undo()); REQUIRE (! globals->canUndo()); REQUIRE (globals->canRedo());
    REQUIRE (std::abs (globals->getAPVTS().getRawParameterValue (scrr::params::id::inputGain)->load()) < .001f);
}

TEST_CASE ("Modulator management reorder preserves routing identities, preset order and undo")
{
    auto p = testProcessor(); p->addModule (1, "SpectralContrast");
    const auto effect = p->getModuleChain (1).getChild (0)["uid"].toString();
    const auto lfo = p->addModulator (false), follower = p->addModulator (true), random = p->addModulator (false, true);
    p->assignModulator (lfo, 1, effect, "amount"); p->assignModulator (follower, 1, effect, "amount");
    p->assignMacro (0, scrr::params::MacroMapping::modulationChannel, lfo, "rate");
    const auto chain = p->getModuleChain (1).toXmlString();
    const auto revision = p->getModulatorRevision();
    REQUIRE (p->moveModulator (lfo, 3));
    REQUIRE (p->getModulators()[0].uid == follower); REQUIRE (p->getModulators()[1].uid == random); REQUIRE (p->getModulators()[2].uid == lfo);
    REQUIRE (p->getModulatorRevision() == revision + 1); REQUIRE (p->getModuleChain (1).toXmlString() == chain);
    REQUIRE (p->getMacroMappings().size() == 3);
    REQUIRE (p->getMacroMappings()[0].sourceUid == lfo); REQUIRE (p->getMacroMappings()[0].valueIndex() == 10);
    REQUIRE (p->getMacroMappings()[1].sourceUid == follower); REQUIRE (p->getMacroMappings()[1].valueIndex() == 8);
    REQUIRE (p->getMacroMappings()[2].uid == lfo);
    REQUIRE (! p->moveModulator (lfo, 2)); REQUIRE (! p->moveModulator (lfo, 3)); REQUIRE (! p->moveModulator ("missing", 0));
    REQUIRE (p->getModulatorRevision() == revision + 1);
    auto restored = testProcessor(); REQUIRE (restored->applyPreset (p->copyPresetState()));
    REQUIRE (restored->getModulators()[0].uid == follower); REQUIRE (restored->getModulators()[2].uid == lfo);
    REQUIRE (restored->getMacroMappings()[0].valueIndex() == 10);
    REQUIRE (p->undo()); REQUIRE (p->getModulators()[0].uid == lfo); REQUIRE (p->getMacroMappings()[0].valueIndex() == 8);
    REQUIRE (p->redo()); REQUIRE (p->getModulators()[2].uid == lfo); REQUIRE (p->getMacroMappings()[0].valueIndex() == 10);
}

TEST_CASE ("Reordering independent sources preserves LFO phase, follower state and random smoothing")
{
    scrr::params::ModulationSource lfo, follower, random;
    lfo.uid = "phase"; lfo.shape = 2; lfo.rate = .37f;
    follower.uid = "follower"; follower.envelope = true; follower.attack = 120;
    random.uid = "random"; random.random = true; random.seed = 71; random.rate = 24; random.smooth = 180;
    scrr::dsp::ModulationEngine reordered, unchanged;
    reordered.configure ({ lfo, follower, random }); unchanged.configure ({ lfo, follower, random });
    reordered.prepare (48000); unchanged.prepare (48000);
    juce::AudioBuffer<float> input (2, 64);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < 64; ++i) input.setSample (c, i, .7f);
    for (int block = 0; block < 120; ++block)
    {
        if (block == 40)
        {
            reordered.configure ({ random, lfo, follower });
            for (int i = 0; i < 3; ++i)
            {
                const int next = i == 0 ? 1 : i == 1 ? 2 : 0;
                REQUIRE (std::abs (reordered.value (next) - unchanged.value (i)) < 1.0e-6f);
                REQUIRE (std::abs (reordered.phase (next) - unchanged.phase (i)) < 1.0e-6f);
            }
        }
        reordered.process (input, 0, 64, {}, 120, false, 0, false);
        unchanged.process (input, 0, 64, {}, 120, false, 0, false);
        for (int i = 0; i < 3; ++i)
        {
            const int reorderedIndex = block < 40 ? i : i == 0 ? 1 : i == 1 ? 2 : 0;
            REQUIRE (std::abs (reordered.value (reorderedIndex) - unchanged.value (i)) < 1.0e-6f);
            REQUIRE (std::abs (reordered.phase (reorderedIndex) - unchanged.phase (i)) < 1.0e-6f);
        }
    }
}

TEST_CASE ("Modulation management list selects details and commits row reorder only on release")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); const auto lfo = p->addModulator (false), follower = p->addModulator (true);
    scrr::gui::ModulationPage page (*p); page.setSize (810, 410); page.open();
    page.addToDesktop (0); page.setVisible (true); pump();
    auto* first = findID (page, "modulator-row-" + lfo); auto* second = findID (page, "modulator-row-" + follower);
    auto* firstDetail = findID (page, "modulator-" + lfo); auto* secondDetail = findID (page, "modulator-" + follower);
    REQUIRE (first && second && firstDetail && secondDetail);
    REQUIRE (first->getX() == second->getX()); REQUIRE (second->getY() > first->getY());
    REQUIRE (firstDetail->isVisible()); REQUIRE (! secondDetail->isVisible());
    second->mouseUp (mouse (*second, { 40, 20 }));
    REQUIRE (page.selectedSource() == follower); REQUIRE (! firstDetail->isVisible()); REQUIRE (secondDetail->isVisible());
    const auto end = first->getLocalPoint (second, juce::Point<int> (40, 53)).toFloat();
    first->mouseDown (mouse (*first, { 40, 20 })); first->mouseDrag (mouse (*first, end, true));
    REQUIRE (p->getModulators()[0].uid == lfo);
    first->mouseUp (mouse (*first, end, true)); pump();
    REQUIRE (p->getModulators()[0].uid == follower); REQUIRE (page.selectedSource() == follower);
    first = findID (page, "modulator-row-" + lfo); REQUIRE (first);
    const auto outside = juce::Point<float> (-200, -200);
    first->mouseDown (mouse (*first, { 40, 20 })); first->mouseDrag (mouse (*first, outside, true)); first->mouseUp (mouse (*first, outside, true)); pump();
    REQUIRE (p->getModulators()[0].uid == follower); REQUIRE (p->getModulators().size() == 2);
    REQUIRE (findButton (page, "+ AUDIO FOLLOWER") != nullptr);
}

TEST_CASE ("Output drag is distinct from source controls and assigns the existing modulation route")
{
    auto p = testProcessor(); p->addModule (1, "SpectralContrast");
    const auto effect = p->getModuleChain (1).getChild (0)["uid"].toString(); const auto lfo = p->addModulator (false);
    scrr::gui::ModulationPage page (*p); page.setSize (810, 410); page.open();
    scrr::gui::AssignableSlider target; target.setSize (200, 28);
    REQUIRE (! target.canReceiveModulationSource()); REQUIRE (! target.dropModulationSource (lfo));
    target.receiveModulationSource = [&] (const juce::String& source) { p->assignModulator (source, 1, effect, "amount"); };
    REQUIRE (target.canReceiveModulationSource());
    target.setModulationDropHighlight (true); REQUIRE (target.isModulationDropHighlighted());
    int movements = 0, releases = 0;
    page.dragOutput = [&] (const juce::String& source, juce::Point<int>, bool finish)
    {
        REQUIRE (source == lfo);
        if (finish) { ++releases; REQUIRE (target.dropModulationSource (source)); }
        else ++movements;
    };
    auto* sourceControl = dynamic_cast<scrr::gui::AssignableSlider*> (findID (page, "modulation-rate"));
    REQUIRE (sourceControl != nullptr); REQUIRE (! sourceControl->canReceiveModulationSource());
    auto* handle = findID (page, "modulation-output-" + lfo); REQUIRE (handle);
    handle->mouseDown (mouse (*handle, { 20, 20 })); handle->mouseDrag (mouse (*handle, { 90, 60 }, true));
    REQUIRE (movements == 1); REQUIRE (page.isOutputDragActive()); REQUIRE (p->getMacroMappings().empty());
    page.refresh(); REQUIRE (findID (page, "modulation-output-" + lfo) == handle);
    handle->mouseUp (mouse (*handle, { 90, 60 }, true));
    REQUIRE (releases == 1); REQUIRE (! page.isOutputDragActive()); REQUIRE (p->getMacroMappings().size() == 1);
    REQUIRE (p->getMacroMappings()[0].sourceUid == lfo); REQUIRE (p->getMacroMappings()[0].uid == effect);
    REQUIRE (target.dropModulationSource (lfo)); REQUIRE (p->getMacroMappings().size() == 1);
    target.setEnabled (false); REQUIRE (! target.canReceiveModulationSource()); REQUIRE (! target.dropModulationSource (lfo));
    target.setModulationDropHighlight (false); REQUIRE (! target.isModulationDropHighlighted());
}

TEST_CASE ("Cross-page output hover previews an effect without touching Solo and Escape cancels the whole gesture")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift"); p->addModule (2, "SpectralContrast"); p->addModule (2, "SpectralComb");
    const auto targetUid = p->getModuleChain (2).getChild (1)["uid"].toString(); const auto lfo = p->addModulator (false);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1180, 1000);
    editor->addToDesktop (0); editor->setVisible (true); pump (320);
    auto* content = dynamic_cast<scrr::gui::EditorContent*> (editor->getChildComponent (0)); REQUIRE (content);
    findButton (*editor, "MODULATION")->triggerClick(); pump (50);
    auto* page = dynamic_cast<scrr::gui::ModulationPage*> (findID (*editor, "modulation-page")); REQUIRE (page);
    auto* handle = findID (*page, "modulation-output-" + lfo); REQUIRE (handle);
    juce::Component::SafePointer<juce::Component> origin (handle);
    auto* tab2 = findButton (*editor, "CH 2"); REQUIRE (tab2);
    const auto overTab = handle->getLocalPoint (tab2, juce::Point<int> (40, 15)).toFloat();
    handle->mouseDown (mouse (*handle, { 20, 20 })); handle->mouseDrag (mouse (*handle, overTab, true));
    REQUIRE (waitForUI ([&] { return tab2->getToggleState() && page->getAlpha() < .1f; }));
    REQUIRE (origin); REQUIRE (p->getProcessor().getSoloChannel() == 0); REQUIRE (p->getMacroMappings().empty());
    auto* targetRow = findID (*editor, "stage-" + targetUid); REQUIRE (targetRow);
    const auto overRow = handle->getLocalPoint (targetRow, juce::Point<int> (45, 20)).toFloat();
    handle->mouseDrag (mouse (*handle, overRow, true));
    REQUIRE (waitForUI ([&] { return findID (*editor, "spacing") != nullptr; }));
    REQUIRE (origin); REQUIRE (p->getProcessor().getSoloChannel() == 0);
    REQUIRE (content->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey))); pump (60);
    REQUIRE (findButton (*editor, "CH 1")->getToggleState()); REQUIRE (page->getAlpha() > .9f);
    REQUIRE (p->getProcessor().getSoloChannel() == 0); REQUIRE (p->getMacroMappings().empty());
    handle->mouseDrag (mouse (*handle, overTab, true)); pump (320);
    REQUIRE (findButton (*editor, "CH 1")->getToggleState());
    handle->mouseUp (mouse (*handle, overTab, true)); REQUIRE (! page->isOutputDragActive());

    handle->mouseDown (mouse (*handle, { 20, 20 })); handle->mouseDrag (mouse (*handle, overTab, true));
    REQUIRE (waitForUI ([&] { return tab2->getToggleState() && page->getAlpha() < .1f; }));
    targetRow = findID (*editor, "stage-" + targetUid); REQUIRE (targetRow);
    handle->mouseDrag (mouse (*handle, handle->getLocalPoint (targetRow, juce::Point<int> (45, 20)).toFloat(), true));
    REQUIRE (waitForUI ([&] { return findID (*editor, "spacing") != nullptr; }));
    auto* control = dynamic_cast<scrr::gui::ParameterControl*> (findID (*editor, "moduleMix")); REQUIRE (control);
    auto* slider = control->getAssignableSlider(); REQUIRE (slider);
    const auto overParameter = handle->getLocalPoint (slider, slider->getLocalBounds().getCentre()).toFloat();
    handle->mouseDrag (mouse (*handle, overParameter, true)); REQUIRE (slider->isModulationDropHighlighted());
    REQUIRE (p->getMacroMappings().empty()); REQUIRE (p->getProcessor().getSoloChannel() == 0);
    handle->mouseUp (mouse (*handle, overParameter, true)); pump (80);
    REQUIRE (p->getMacroMappings().size() == 1); REQUIRE (p->getMacroMappings()[0].uid == targetUid);
    REQUIRE (p->getMacroMappings()[0].sourceUid == lfo); REQUIRE (p->getMacroMappings()[0].channel == 2);
    REQUIRE (p->getProcessor().getSoloChannel() == 1); REQUIRE (! page->isVisible());
}

TEST_CASE ("Stage preview retains its mouse source through cancel and transfers only after release")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift"); p->addModule (2, "SpectralContrast");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1180, 1000);
    editor->addToDesktop (0); editor->setVisible (true); pump (320);
    auto* content = dynamic_cast<scrr::gui::EditorContent*> (editor->getChildComponent (0)); REQUIRE (content);
    auto* row = findID (*editor, "stage-" + uid); REQUIRE (row);
    juce::Component::SafePointer<juce::Component> origin (row);
    auto* tab2 = findButton (*editor, "CH 2"); REQUIRE (tab2);
    auto overTab = row->getLocalPoint (tab2, juce::Point<int> (40, 15)).toFloat();
    row->mouseDown (mouse (*row, { 40, 20 })); row->mouseDrag (mouse (*row, overTab, true));
    REQUIRE (waitForUI ([&] { return tab2->getToggleState(); }));
    REQUIRE (origin); REQUIRE (p->getModuleChain (1).getNumChildren() == 1); REQUIRE (p->getModuleChain (2).getNumChildren() == 1);
    REQUIRE (p->getProcessor().getSoloChannel() == 0);
    REQUIRE (content->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey))); pump (60);
    REQUIRE (origin); REQUIRE (findButton (*editor, "CH 1")->getToggleState());
    row->mouseDrag (mouse (*row, overTab, true)); pump (320);
    REQUIRE (findButton (*editor, "CH 1")->getToggleState()); REQUIRE (p->getProcessor().getSoloChannel() == 0);
    row->mouseUp (mouse (*row, overTab, true)); pump (60);
    REQUIRE (! origin); REQUIRE (p->getModuleChain (1).getNumChildren() == 1);
    row = findID (*editor, "stage-" + uid); REQUIRE (row);
    overTab = row->getLocalPoint (tab2, juce::Point<int> (40, 15)).toFloat();
    row->mouseDown (mouse (*row, { 40, 20 })); row->mouseDrag (mouse (*row, overTab, true));
    REQUIRE (waitForUI ([&] { return tab2->getToggleState(); }));
    REQUIRE (p->getProcessor().getSoloChannel() == 0); REQUIRE (p->getModuleChain (1).getNumChildren() == 1);
    row->mouseUp (mouse (*row, overTab, true)); REQUIRE (p->getModuleChain (1).getNumChildren() == 1);
    REQUIRE (waitForUI ([&] { return p->getModuleChain (1).getNumChildren() == 0; }));
    REQUIRE (p->getModuleChain (2).getNumChildren() == 2); REQUIRE (p->getProcessor().getSoloChannel() == 1);
}

TEST_CASE ("Host structural changes cancel a pending preview instead of committing stale targets")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->addToDesktop (0); editor->setVisible (true); pump (320);
    auto* row = findID (*editor, "stage-" + uid); auto* tab2 = findButton (*editor, "CH 2"); REQUIRE (row && tab2);
    juce::Component::SafePointer<juce::Component> origin (row);
    const auto overTab = row->getLocalPoint (tab2, juce::Point<int> (40, 15)).toFloat();
    row->mouseDown (mouse (*row, { 40, 20 })); row->mouseDrag (mouse (*row, overTab, true));
    p->addModule (3, "SpectralBloom"); pump (350);
    REQUIRE (origin); REQUIRE (findButton (*editor, "CH 1")->getToggleState());
    row->mouseUp (mouse (*row, overTab, true)); pump (60);
    REQUIRE (p->getModuleChain (1).getNumChildren() == 1); REQUIRE (p->getModuleChain (2).getNumChildren() == 0);
    REQUIRE (p->getModuleChain (3).getNumChildren() == 1); REQUIRE (p->getProcessor().getSoloChannel() == 0);
}

TEST_CASE ("EQ graph node edits update the selected band controls and controls update the graph")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); p->addModule (1, "SpectralEqualizer");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1180, 1000);
    editor->addToDesktop (0); editor->setVisible (true); pump (320);
    auto* graph = dynamic_cast<scrr::gui::SpectralEqEditor*> (findID (*editor, "spectral-eq-editor")); REQUIRE (graph && graph->isVisible());
    const auto start = graph->getBandPosition (3);
    graph->mouseDown (mouse (*graph, start)); pump (60);
    REQUIRE (findID (*editor, "band4Freq") != nullptr); REQUIRE (findID (*editor, "band1Freq") == nullptr);
    graph->mouseDrag (mouse (*graph, start + juce::Point<float> (32, -19), true));
    auto* frequency = dynamic_cast<scrr::gui::ParameterControl*> (findID (*editor, "band4Freq")); REQUIRE (frequency);
    auto* gain = dynamic_cast<scrr::gui::ParameterControl*> (findID (*editor, "band4Gain")); REQUIRE (gain);
    const auto edited = p->getModuleChain (1).getChild (0);
    REQUIRE (std::abs (frequency->getAssignableSlider()->getValue() - (double) edited["band4Freq"]) < 1.0);
    REQUIRE (std::abs (gain->getAssignableSlider()->getValue() - (double) edited["band4Gain"]) < .11);
    graph->mouseUp (mouse (*graph, start + juce::Point<float> (32, -19), true));
    const auto moved = graph->getBandPosition (3); REQUIRE (moved.x > start.x); REQUIRE (moved.y < start.y);
    frequency->getAssignableSlider()->setValue (2000, juce::sendNotificationSync);
    REQUIRE (waitForUI ([&] { return graph->getBandPosition (3).x > moved.x; }));
    const auto fifth = graph->getBandPosition (4);
    juce::MouseWheelDetails wheel {}; wheel.deltaY = .2f;
    graph->mouseWheelMove (mouse (*graph, fifth), wheel);
    REQUIRE (waitForUI ([&] { return findID (*editor, "band5Q") != nullptr; }));
    REQUIRE (findID (*editor, "band4Q") == nullptr);
}

TEST_CASE ("EQ switching modules cancels pending node gestures and stale band menus")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); p->addModule (1, "SpectralEqualizer"); p->addModule (1, "SpectralEqualizer");
    const auto a = p->getModuleChain (1).getChild (0), b = p->getModuleChain (1).getChild (1);
    scrr::gui::SpectralEqEditor graph; graph.setSize (600, 230); graph.setState (a);
    graph.addToDesktop (0); graph.setVisible (true);
    int changes = 0;
    graph.onParameterChange = [&] (const juce::String&, const juce::var&) { ++changes; };
    const auto start = graph.getBandPosition (2);
    graph.mouseDown (mouse (graph, start)); graph.setState (b);
    graph.mouseDrag (mouse (graph, start + juce::Point<float> (40, -20), true));
    graph.mouseUp (mouse (graph, start, true)); REQUIRE (changes == 0);

    graph.setState (a);
    graph.mouseDown (mouse (graph, graph.getBandPosition (2), false, juce::ModifierKeys::rightButtonModifier));
    auto* menu = juce::Component::getCurrentlyModalComponent(); REQUIRE (menu);
    graph.setState (b);
    menu->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    menu->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    menu->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); pump (60);
    REQUIRE (changes == 0); REQUIRE (graph.getModuleUid() == b["uid"].toString());

    // Re-entrant host updates must not redirect the second half of a two-value drag.
    graph.setState (a);
    graph.onParameterChange = [&] (const juce::String&, const juce::var&) { ++changes; graph.setState (b); };
    graph.mouseDown (mouse (graph, graph.getBandPosition (2)));
    graph.mouseDrag (mouse (graph, start + juce::Point<float> (40, -20), true));
    REQUIRE (changes == 1); REQUIRE (graph.getModuleUid() == b["uid"].toString());
}

TEST_CASE ("The EQ host callback rejects a stale graph UID or a non-EQ current selection")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto p = testProcessor(); p->addModule (1, "SpectralEqualizer"); p->addModule (1, "FrequencyShift");
    const auto eq = p->getModuleChain (1).getChild (0).createCopy();
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1180, 1000);
    editor->addToDesktop (0); editor->setVisible (true); pump (320);
    auto* graph = dynamic_cast<scrr::gui::SpectralEqEditor*> (findID (*editor, "spectral-eq-editor")); REQUIRE (graph);
    auto stale = eq.createCopy(); stale.setProperty ("uid", "previous-module", nullptr); graph->setState (stale);
    graph->onParameterChange ("band1Gain", 19.0);
    REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["band1Gain"]) < .0001);
    const auto otherUid = p->getModuleChain (1).getChild (1)["uid"].toString();
    auto* row = findID (*editor, "stage-" + otherUid); REQUIRE (row);
    row->mouseUp (mouse (*row, { 40, 20 })); pump (60);
    graph->onParameterChange ("band1Gain", 19.0);
    REQUIRE (! p->getModuleChain (1).getChild (1).hasProperty ("band1Gain"));
}

inline int renderV020Ui (const juce::File& directory)
{
    if (directory.createDirectory().failed() || juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return 1;
    auto p = testProcessor(); p->prepareToPlay (48000, 128);
    p->addModule (1, "SpectralEqualizer"); p->addModule (1, "SpectralClipper"); p->addModule (2, "SpectralContrast"); p->addModule (2, "SpectralComb");
    const auto eq = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->setModuleParameter (1, eq, "band1Type", 3); p->setModuleParameter (1, eq, "band3Gain", 6.0); p->setModuleParameter (1, eq, "band5Gain", -8.0);
    const auto lfo = p->addModulator (false), follower = p->addModulator (true); p->addModulator (false, true);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); editor->setSize (1180, 820);
    editor->addToDesktop (0); editor->setVisible (true); pump (400);
    auto capture = [&] (const juce::String& name)
    {
        juce::PNGImageFormat png; const auto pixels = editor->createComponentSnapshot (editor->getLocalBounds());
        const auto file = directory.getChildFile (name + ".png"); juce::MemoryOutputStream stream;
        if (! png.writeImageToStream (pixels, stream) || ! file.replaceWithData (stream.getData(), stream.getDataSize()))
            throw std::runtime_error ("UI screenshot write failed");
        std::cout << file.getFullPathName() << '\n';
    };
    capture ("v020-equalizer-default"); editor->setSize (1180, 1000); pump (100); capture ("v020-equalizer-tall");
    findButton (*editor, "MODULATION")->triggerClick(); pump (100); capture ("v020-modulation-lfo");
    auto* page = dynamic_cast<scrr::gui::ModulationPage*> (findID (*editor, "modulation-page"));
    if (! page) return 1;
    page->selectSource (follower); pump (100); capture ("v020-modulation-audio-follower");
    page->selectSource (lfo);
    auto* handle = findID (*page, "modulation-output-" + lfo); auto* target = findButton (*editor, "CH 2");
    if (! handle || ! target) return 1;
    const auto overTab = handle->getLocalPoint (target, juce::Point<int> (40, 15)).toFloat();
    handle->mouseDown (mouse (*handle, { 20, 20 })); handle->mouseDrag (mouse (*handle, overTab, true));
    if (! waitForUI ([&] { return target->getToggleState(); })) return 1;
    pump (300); capture ("v020-output-channel-preview");
    handle->mouseUp (mouse (*handle, { -500, -500 }, true)); pump (100);
    scrr::gui::PromptLanguage::set (scrr::gui::PromptLanguage::Language::chinese, false); pump (100); capture ("v020-modulation-chinese");
    scrr::gui::PromptLanguage::set (scrr::gui::PromptLanguage::Language::english, false);
    findButton (*editor, "MODULATION")->triggerClick(); pump (50); editor->setSize (1180, 820);
    auto* settings = dynamic_cast<scrr::gui::SettingsPanel*> (findID (*editor, "settings-panel"));
    auto* guide = dynamic_cast<scrr::gui::QuickGuide*> (findID (*editor, "quick-guide"));
    if (! settings || ! guide) return 1;
    for (bool chinese : { false, true })
    {
        scrr::gui::PromptLanguage::set (chinese ? scrr::gui::PromptLanguage::Language::chinese : scrr::gui::PromptLanguage::Language::english, false);
        pump (100); settings->setVisible (true); settings->toFront (true);
        const juce::String language = chinese ? "chinese" : "english";
        for (int section = 0; section < 3; ++section)
        { settings->showPage (section); pump (60); capture ("v020-settings-" + juce::String (section + 1) + "-" + language); }
        settings->setVisible (false); guide->open();
        for (int section = 0; section < 4; ++section)
        {
            pump (60); capture ("v020-guide-" + juce::String (section + 1) + "-" + language);
            if (section < 3) findButton (*guide, chinese ? juce::String::fromUTF8 (u8"下一步") : juce::String ("NEXT"))->triggerClick();
        }
        guide->setVisible (false);
    }
    scrr::gui::PromptLanguage::set (scrr::gui::PromptLanguage::Language::english, false);
    return 0;
}

TEST_CASE ("Quick Guide repeats until suppressed and Settings can re-enable its startup display")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    const auto preference = scrr::gui::QuickGuide::preference();
    const auto original = preference.loadFileAsString();
    struct RestoreGuide { juce::File file; juce::String text; ~RestoreGuide() { file.replaceWithText (text); } } restore { preference, original };
    REQUIRE (preference.replaceWithText ("show"));
    auto p = testProcessor();
    auto open = [&]
    {
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor());
        editor->addToDesktop (0); editor->setVisible (true);
        REQUIRE (waitForUI ([&] { return editor->isShowing(); }));
        return editor;
    };
    auto editor = open(); auto* guide = findID (*editor, "quick-guide"); REQUIRE (guide);
    REQUIRE (waitForUI ([&] { return guide->isVisible(); }));
    findButton (*guide, "GOT IT")->triggerClick();
    REQUIRE (waitForUI ([&] { return ! guide->isVisible(); })); REQUIRE (scrr::gui::QuickGuide::shouldShow());
    editor.reset(); editor = open(); guide = findID (*editor, "quick-guide"); REQUIRE (guide);
    REQUIRE (waitForUI ([&] { return guide->isVisible(); }));
    auto* never = findButton (*guide, "Do not show again"); REQUIRE (never);
    never->setToggleState (true, juce::dontSendNotification); findButton (*guide, "GOT IT")->triggerClick();
    REQUIRE (waitForUI ([&] { return ! guide->isVisible(); }));
    REQUIRE (! scrr::gui::QuickGuide::shouldShow());
    editor.reset(); editor = open(); guide = findID (*editor, "quick-guide"); REQUIRE (guide);
    // The guide starts hidden before its startup timer runs. Give the visible
    // editor's 30 Hz timer a real dispatch window before checking suppression.
    auto startupDispatched = std::make_shared<bool> (false);
    juce::Timer::callAfterDelay (120, [startupDispatched] { *startupDispatched = true; });
    REQUIRE (waitForUI ([&] { return *startupDispatched; }));
    REQUIRE (! guide->isVisible());
    auto* settingsButton = dynamic_cast<juce::Button*> (findID (*editor, "settings-button")); REQUIRE (settingsButton);
    settingsButton->triggerClick();
    auto* settings = findID (*editor, "settings-panel"); REQUIRE (settings);
    REQUIRE (waitForUI ([&] { return settings->isVisible(); }));
    auto* openGuide = dynamic_cast<juce::Button*> (findID (*editor, "open-quick-guide")); REQUIRE (openGuide);
    openGuide->triggerClick(); REQUIRE (waitForUI ([&] { return guide->isVisible(); }));
    never = findButton (*guide, "Do not show again"); REQUIRE (never && never->getToggleState());
    never->setToggleState (false, juce::dontSendNotification); findButton (*guide, "GOT IT")->triggerClick();
    REQUIRE (waitForUI ([&] { return ! guide->isVisible(); }));
    REQUIRE (scrr::gui::QuickGuide::shouldShow());
    editor.reset(); editor = open(); guide = findID (*editor, "quick-guide"); REQUIRE (guide);
    REQUIRE (waitForUI ([&] { return guide->isVisible(); }));
}
