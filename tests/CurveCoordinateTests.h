// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

TEST_CASE ("Custom logarithmic parameter mappings support finite double ranges without ratio overflow")
{
    scrr::params::MacroMapping mapping; mapping.minimum = 1.0e-300; mapping.maximum = 1.0e300; mapping.logarithmic = true;
    REQUIRE (std::abs (mapping.normalise (1) - .5) < 1.0e-12);
    REQUIRE (std::abs (mapping.denormalise (.5) - 1) < 1.0e-12);
    REQUIRE (std::abs (mapping.normalise (1.0e150) - .75) < 1.0e-12);
    mapping.step = 1.0e-300;
    REQUIRE (std::isfinite (mapping.denormalise (.75)));
    REQUIRE (std::abs (mapping.denormalise (.75) / 1.0e150 - 1) < 1.0e-12);
    mapping.relative = false; mapping.low = mapping.minimum; mapping.high = mapping.maximum;
    REQUIRE (std::abs (mapping.value (.75f) / 1.0e150 - 1) < 1.0e-12);
    mapping.logarithmic = false; mapping.minimum = 0; mapping.maximum = 1.0e-20; mapping.step = 0;
    REQUIRE (std::abs (mapping.normalise (5.0e-21) - .5) < 1.0e-12);
}

TEST_CASE ("Curve point identities survive serialization insertion deletion and reset")
{
    using scrr::params::TransferCurve;
    auto curve = TransferCurve::decode ("1;0,0,0;0.4,0.2,0;1,1,0");
    REQUIRE (curve.points.size() == 3);
    const auto retained = curve.points[1].id;
    const auto inserted = curve.allocatePointId();
    curve.points.insert (curve.points.begin() + 1, { .2f, .1f, 0, inserted });
    curve.sanitise();
    auto restored = TransferCurve::decode (curve.encode());
    REQUIRE (restored.sameStateAs (curve)); REQUIRE (restored.pointIndex (retained) == 2);
    restored.points.erase (restored.points.begin() + 1);
    const auto replacement = restored.allocatePointId();
    REQUIRE (replacement > inserted); REQUIRE (replacement != retained);
    restored.reset();
    REQUIRE (restored.pointIndex (retained) == -1);
    REQUIRE (restored.points.front().id > replacement);
    REQUIRE (! restored.canMapCoordinate (restored.points.front().id, true));
    REQUIRE (restored.canMapCoordinate (restored.points.front().id, false));
    REQUIRE (! restored.canMapCoordinate (restored.points.front().id, false, true));
    auto pinned = curve; pinned.sanitise (true, true);
    for (const auto& point : pinned.points) if (std::abs (point.x - .5f) < 1.0e-6f)
    { REQUIRE (! pinned.canMapCoordinate (point.id, true, true, true)); REQUIRE (! pinned.canMapCoordinate (point.id, false, true, true)); }
    scrr::params::ModulationSource source; source.shape = 5; source.points = curve.points; source.nextPointId = curve.nextPointId;
    const auto sourceCopy = scrr::params::ModulationSource::read (source.state());
    REQUIRE (sourceCopy.nextPointId == source.nextPointId);
    REQUIRE (sourceCopy.points[2].id == retained);
}

TEST_CASE ("Remap coordinate assignments retain stable targets through state copy deletion and Undo")
{
    using namespace scrr::params;
    auto p = testProcessor(); p->addModule (1, "FrequencyRemap");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    TransferCurve curve; const auto point = curve.allocatePointId();
    curve.points.insert (curve.points.begin() + 1, { .4f, .3f, 0, point });
    p->setModuleParameter (1, uid, "remapCurve", curve.encode());
    const auto x = curveCoordinateParameter (point, true), y = curveCoordinateParameter (point, false);
    const auto random = p->addModulator (false, true);
    p->assignMacro (0, 1, uid, x); p->assignModulator (random, 1, uid, y);
    REQUIRE (p->getMacroMappings().size() == 2);
    p->assignMacro (1, 1, uid, curveCoordinateParameter (curve.points.front().id, true));
    REQUIRE (p->getMacroMappings().size() == 2);
    curve.points.insert (curve.points.begin() + 1, { .2f, .2f, 0, curve.allocatePointId() });
    p->setModuleParameter (1, uid, "remapCurve", curve.encode());
    REQUIRE (p->getMacroMappings()[0].parameter == x);
    const auto duplicated = p->transferModule (1, uid, 2, 0, true);
    REQUIRE (duplicated.isNotEmpty()); REQUIRE (p->getMacroMappings().size() == 4);
    REQUIRE (p->getMacroMappings()[0].routeUid != p->getMacroMappings()[2].routeUid);
    auto restored = testProcessor(); REQUIRE (restored->applyPreset (p->copyPresetState()));
    REQUIRE (restored->getMacroMappings().size() == 4);
    auto pasted = testProcessor(); REQUIRE (pasted->pasteModule (3, p->copyModule (1, uid)).isNotEmpty());
    REQUIRE (pasted->getMacroMappings().size() == 2);
    REQUIRE (pasted->getMacroMappings()[0].parameter == x);
    REQUIRE (pasted->getMacroMappings()[1].sourceUid == random);
    curve.points.erase (curve.points.begin() + curve.pointIndex (point));
    p->setModuleParameter (1, uid, "remapCurve", curve.encode());
    REQUIRE (p->getMacroMappings().size() == 2); // Only the independently duplicated module remains.
    REQUIRE (p->undo()); REQUIRE (p->getMacroMappings().size() == 4);
    REQUIRE (p->redo()); REQUIRE (p->getMacroMappings().size() == 2);
}

TEST_CASE ("Response node bindings duplicate independently and cannot recursively bind coordinate routes")
{
    using namespace scrr::params;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->assignMacro (0, 1, uid, "shift");
    auto owner = p->getMacroMappings()[0];
    const auto point = owner.responseCurve.allocatePointId();
    owner.responseCurve.points.insert (owner.responseCurve.points.begin() + 1, { .5f, .25f, 0, point });
    p->updateMacroMapping (owner);
    p->assignMacro (1, MacroMapping::responseChannel, owner.routeUid, curveCoordinateParameter (point, false));
    REQUIRE (p->getMacroMappings().size() == 2);
    auto coordinate = p->getMacroMappings()[1]; const auto nestedPoint = coordinate.responseCurve.allocatePointId();
    coordinate.responseCurve.points.insert (coordinate.responseCurve.points.begin() + 1, { .5f, .25f, 0, nestedPoint });
    p->updateMacroMapping (coordinate);
    p->assignMacro (2, MacroMapping::responseChannel, coordinate.routeUid, curveCoordinateParameter (nestedPoint, false));
    REQUIRE (p->getMacroMappings().size() == 2);
    REQUIRE (p->transferModule (1, uid, 2, 0, true).isNotEmpty());
    const auto routes = p->getMacroMappings(); REQUIRE (routes.size() == 4);
    REQUIRE (routes[2].routeUid != owner.routeUid); REQUIRE (routes[3].uid == routes[2].routeUid);
    auto pasted = testProcessor(); REQUIRE (pasted->pasteModule (3, p->copyModule (1, uid)).isNotEmpty());
    const auto copies = pasted->getMacroMappings(); REQUIRE (copies.size() == 2);
    REQUIRE (copies[0].routeUid != owner.routeUid); REQUIRE (copies[1].uid == copies[0].routeUid);
    auto restored = testProcessor(); REQUIRE (restored->applyPreset (p->copyPresetState()));
    const auto restoredRoutes = restored->getMacroMappings(); REQUIRE (restoredRoutes.size() == 4);
    for (const auto& binding : restoredRoutes) if (binding.channel == MacroMapping::responseChannel)
        REQUIRE (std::any_of (restoredRoutes.begin(), restoredRoutes.end(), [&] (const auto& parent)
            { return parent.channel != MacroMapping::responseChannel && parent.routeUid == binding.uid; }));
    p->removeMacroMapping (1, uid, "shift", 0);
    REQUIRE (p->getMacroMappings().size() == 2);
}

TEST_CASE ("Response coordinate modulation reaches audio in one pass without altering saved points")
{
    using namespace scrr::params;
    auto p = testProcessor(); setParam (*p, id::oversample, 0); setParam (*p, id::drywet, 0);
    p->assignMacro (0, 0, {}, juce::Identifier (id::outputGain));
    auto owner = p->getMacroMappings()[0]; owner.setDepth (.1, false);
    const auto point = owner.responseCurve.allocatePointId();
    owner.responseCurve.points.insert (owner.responseCurve.points.begin() + 1, { .5f, .25f, 0, point });
    p->updateMacroMapping (owner);
    const auto target = curveCoordinateParameter (point, false);
    p->assignMacro (1, MacroMapping::responseChannel, owner.routeUid, target);
    auto binding = p->getMacroMappings()[1]; binding.setDepth (.5, false); p->updateMacroMapping (binding);
    setParam (*p, id::macros[0], 50); setParam (*p, id::macros[1], 100);
    p->prepareToPlay (48000, 128); juce::AudioBuffer<float> audio (2, 128); juce::MidiBuffer midi;
    for (int block = 0; block < 96; ++block)
    {
        for (int channel = 0; channel < 2; ++channel) for (int i = 0; i < 128; ++i) audio.setSample (channel, i, .05f);
        p->processBlock (audio, midi);
    }
    const double expected = .05 * juce::Decibels::decibelsToGain (4.5);
    REQUIRE (std::abs (audio.getSample (0, 127) - expected) < expected * .001);
    REQUIRE (std::abs (p->getEffectiveValue (MacroMapping::responseChannel, owner.routeUid, target, .25) - .75) < 1.0e-6);
    REQUIRE (std::abs (p->getMacroMappings()[0].responseCurve.points[1].y - .25f) < 1.0e-6f);
}

TEST_CASE ("Ambiguous saved route identities discard response bindings on restore and paste")
{
    using namespace scrr::params;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->assignMacro (0, 1, uid, "shift"); auto owner = p->getMacroMappings()[0];
    const auto point = owner.responseCurve.allocatePointId();
    owner.responseCurve.points.insert (owner.responseCurve.points.begin() + 1, { .5f, .25f, 0, point }); p->updateMacroMapping (owner);
    p->assignMacro (1, MacroMapping::responseChannel, owner.routeUid, curveCoordinateParameter (point, false));
    const auto duplicateOwner = [&] (juce::ValueTree mappings)
    {
        auto duplicate = mappings.getChildWithName ("MAP").createCopy(); duplicate.setProperty ("macro", 2, nullptr); mappings.appendChild (duplicate, nullptr);
    };
    auto state = p->copyPresetState(); duplicateOwner (state.getChildWithName ("macroMappings"));
    auto restored = testProcessor(); REQUIRE (restored->applyPreset (state));
    const auto routes = restored->getMacroMappings(); REQUIRE (routes.size() == 2);
    REQUIRE (routes[0].routeUid != routes[1].routeUid);
    for (const auto& route : routes) REQUIRE (route.channel != MacroMapping::responseChannel);
    const auto xml = juce::parseXML (p->copyModule (1, uid)); REQUIRE (xml != nullptr);
    auto clipboard = juce::ValueTree::fromXml (*xml); duplicateOwner (clipboard);
    auto pasted = testProcessor(); REQUIRE (pasted->pasteModule (2, clipboard.toXmlString()).isNotEmpty());
    REQUIRE (pasted->getMacroMappings().size() == 2);
    for (const auto& route : pasted->getMacroMappings()) REQUIRE (route.channel != MacroMapping::responseChannel);
}

TEST_CASE ("Undo preserves interleaved response routes and current mapped base values for Redo")
{
    using namespace scrr::params;
    auto p = testProcessor(); p->addModule (1, "FrequencyShift");
    const auto uid = p->getModuleChain (1).getChild (0)["uid"].toString();
    p->assignMacro (0, 1, uid, "shift"); auto owner = p->getMacroMappings()[0];
    const auto point = owner.responseCurve.allocatePointId();
    owner.responseCurve.points.insert (owner.responseCurve.points.begin() + 1, { .5f, .25f, 0, point }); p->updateMacroMapping (owner);
    p->assignMacro (1, MacroMapping::responseChannel, owner.routeUid, curveCoordinateParameter (point, false));
    p->assignMacro (2, 1, uid, "shift");
    p->setModuleParameter (1, uid, "shift", 125.0);
    const auto saved = p->copyPresetState();
    p->setModuleParameter (1, uid, "shift", 250.0);
    REQUIRE (p->undo()); REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["shift"] - 125) < 1.0e-6);
    const auto restored = p->copyPresetState();
    REQUIRE (restored.getChildWithName ("macroMappings").isEquivalentTo (saved.getChildWithName ("macroMappings")));
    REQUIRE (p->redo()); REQUIRE (std::abs ((double) p->getModuleChain (1).getChild (0)["shift"] - 250) < 1.0e-6);
}

TEST_CASE ("LFO coordinate dependencies process current values in DAG order and reject feedback")
{
    using namespace scrr::params;
    auto p = testProcessor(); const auto a = p->addModulator (false), b = p->addModulator (false);
    for (auto source : p->getModulators()) { source.shape = 5; p->updateModulator (source); }
    const auto target = curveCoordinateParameter (2, false);
    REQUIRE (! p->canAssignModulator (a, MacroMapping::modulationChannel, a, target));
    REQUIRE (p->canAssignModulator (a, MacroMapping::modulationChannel, b, target));
    p->assignModulator (a, MacroMapping::modulationChannel, b, target);
    REQUIRE (! p->canAssignModulator (b, MacroMapping::modulationChannel, a, target));
    p->assignModulator (b, MacroMapping::modulationChannel, a, target);
    REQUIRE (p->getMacroMappings().size() == 1);
    REQUIRE (p->moveModulator (b, 0)); REQUIRE (p->getMacroMappings()[0].sourceIndex == 9);
    auto restored = testProcessor(); REQUIRE (restored->applyPreset (p->copyPresetState()));
    REQUIRE (restored->getMacroMappings().size() == 1);

    ModulationSource upstream; upstream.uid = "upstream"; upstream.shape = 2; upstream.phase = 180;
    ModulationSource downstream; downstream.uid = "downstream"; downstream.shape = 5; downstream.phase = 180; downstream.points[1].y = .2f;
    MacroMapping route; route.channel = MacroMapping::modulationChannel; route.uid = downstream.uid;
    route.sourceUid = upstream.uid; route.sourceIndex = 9; route.parameter = target; route.setDepth (.5, false);
    scrr::dsp::ModulationEngine engine; engine.configure ({ downstream, upstream }, { route }); engine.prepare (48000);
    juce::AudioBuffer<float> audio (1, 1); audio.clear();
    engine.process (audio, 0, 0, {}, 120, false, 0, false);
    REQUIRE (std::abs (engine.value (0) - .45f) < 1.0e-6f);
    route.sourceIndex = 8; engine.configure ({ upstream, downstream }, { route });
    engine.process (audio, 0, 0, {}, 120, false, 0, false);
    REQUIRE (std::abs (engine.value (1) - .45f) < 1.0e-6f);
}

TEST_CASE ("Simultaneous coordinate motion preserves node order and the bipolar neutral point")
{
    using namespace scrr::params;
    MacroMapping owner; owner.routeUid = "owner"; owner.mode = MacroMapping::bipolarLeftLow;
    owner.startOffset = -.4; owner.endOffset = .8;
    owner.responseCurve.points = { {0,0,0,1}, {.2f,.1f,0,2}, {.5f,.5f,0,3}, {.8f,.9f,0,4}, {1,1,0,5} };
    owner.responseCurve.sanitise (true, true);
    std::vector<MacroMapping> routes;
    for (const auto point : { 2u, 4u }) for (bool x : { true, false })
    {
        MacroMapping route; route.channel = MacroMapping::responseChannel; route.uid = owner.routeUid;
        route.parameter = curveCoordinateParameter (point, x); route.setDepth (point == 2 ? 1 : -1, false);
        routes.push_back (route);
    }
    owner.bindResponseMappings (routes); ModulationValues values {}; values[0] = 1;
    REQUIRE (std::abs (owner.offset (.5f, &values)) < 1.0e-12);
    for (int i = 0; i <= 100; ++i) REQUIRE (std::isfinite (owner.offset ((float) i / 100, &values)));
    TransferCurve curve; curve.points = { {0,.5f,0,1}, {.99f,.2f,0,2}, {.01f,.8f,0,3}, {1,.5f,0,4} };
    TransferCurve::constrainCoordinates (curve.points.data(), curve.points.size());
    REQUIRE (curve.points[1].x < curve.points[2].x);
    REQUIRE (curve.points[1].id == 2); REQUIRE (curve.points[2].id == 3);
}

TEST_CASE ("Coordinate projection keeps unassigned nodes fixed and resolves adjacent moving groups")
{
    using scrr::params::TransferCurve;
    TransferCurve base; base.points = { {0,0,0,1}, {.2f,.2f,0,2}, {.4f,.4f,0,3}, {.7f,.7f,0,4}, {1,1,0,5} };
    auto moving = base; moving.points[1].x = .9f;
    TransferCurve::constrainCoordinates (moving.points.data(), moving.points.size(), false, false, base.points.data());
    REQUIRE (moving.points[1].x < .4f); REQUIRE (moving.points[1].x > .398f);
    REQUIRE (std::abs (moving.points[2].x - .4f) < 1.0e-7f);
    REQUIRE (std::abs (moving.points[3].x - .7f) < 1.0e-7f);
    moving = base; moving.points[1].x = .85f; moving.points[2].x = .1f;
    TransferCurve::constrainCoordinates (moving.points.data(), moving.points.size(), false, false, base.points.data());
    REQUIRE (moving.points[1].x > .4f); // Both moving nodes share their nearest fixed anchors.
    REQUIRE (moving.points[1].x < moving.points[2].x); REQUIRE (moving.points[2].x < .7f);
    REQUIRE (std::abs (moving.points[3].x - .7f) < 1.0e-7f);
    for (size_t i = 0; i < moving.points.size(); ++i) REQUIRE (moving.points[i].id == base.points[i].id);
}

TEST_CASE ("Remap node modulation matches a static curve in audio and unmapping restores its base")
{
    using namespace scrr::params;
    auto mapped = testProcessor(), reference = testProcessor();
    TransferCurve base; const auto point = base.allocatePointId();
    base.points.insert (base.points.begin() + 1, { .45f, .45f, 0, point });
    auto effective = base; effective.points[1].y = .65f;
    juce::String mappedUid, referenceUid;
    for (auto* processor : { mapped.get(), reference.get() })
    {
        setParam (*processor, id::oversample, 0); setParam (*processor, id::fftSizeExtended, 4);
        processor->addModule (1, "FrequencyRemap");
        const auto uid = processor->getModuleChain (1).getChild (0)["uid"].toString();
        processor->setModuleParameter (1, uid, "remapCurve", processor == mapped.get() ? base.encode() : effective.encode());
        (processor == mapped.get() ? mappedUid : referenceUid) = uid;
    }
    const auto y = curveCoordinateParameter (point, false);
    mapped->assignMacro (0, 1, mappedUid, y); auto route = mapped->getMacroMappings()[0]; route.setDepth (.2, false); mapped->updateMacroMapping (route);
    setParam (*mapped, id::macros[0], 100);
    mapped->prepareToPlay (48000, 128); reference->prepareToPlay (48000, 128);
    juce::AudioBuffer<float> a (2, 128), b (2, 128); juce::MidiBuffer midi;
    double difference = 0, energy = 0;
    const auto render = [&] (int startBlock)
    {
        for (int block = startBlock; block < startBlock + 100; ++block)
        {
            for (int ch = 0; ch < 2; ++ch) for (int sample = 0; sample < 128; ++sample)
            {
                const float value = .1f * (float) std::sin (juce::MathConstants<double>::twoPi * 440 * (block * 128 + sample) / 48000.0);
                a.setSample (ch, sample, value); b.setSample (ch, sample, value);
            }
            mapped->processBlock (a, midi); reference->processBlock (b, midi);
            if (block >= startBlock + 80) for (int sample = 0; sample < 128; ++sample)
            { const double delta = a.getSample (0, sample) - b.getSample (0, sample); difference += delta * delta; energy += (double) b.getSample (0, sample) * b.getSample (0, sample); }
        }
    };
    render (0); REQUIRE (energy > .01); REQUIRE (difference < energy * .0001);
    REQUIRE (TransferCurve::decode (mapped->getModuleChain (1).getChild (0)["remapCurve"].toString()).sameStateAs (base));
    mapped->removeMacroMapping (1, mappedUid, y, 0);
    reference->setModuleParameter (1, referenceUid, "remapCurve", base.encode());
    difference = energy = 0; render (100);
    REQUIRE (energy > .01); REQUIRE (difference < energy * .0001);
}
