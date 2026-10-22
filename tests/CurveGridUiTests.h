// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "../Source/gui/CurveGridSettings.h"
#include "../Source/gui/components/TransferCurveEditor.h"
#include "../Source/gui/ModulationPage.h"

namespace curveGridUiTests {
struct PreferenceScope
{
    scrr::gui::CurveGridSettings::Counts saved { scrr::gui::CurveGridSettings::get() };
    ~PreferenceScope() { scrr::gui::CurveGridSettings::set (saved.x, saved.y, false); }
};
inline int snapModifiers()
{
   #if JUCE_MAC
    return juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier;
   #else
    return juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier;
   #endif
}
inline juce::Point<float> position (juce::Rectangle<float> area, float x, float y)
{ return { area.getX() + area.getWidth() * x, area.getBottom() - area.getHeight() * y }; }
inline void drag (juce::Component& graph, juce::Point<float> start, juce::Point<float> end, int modifiers = juce::ModifierKeys::leftButtonModifier)
{
    graph.mouseDown (mouse (graph, start, false, modifiers));
    graph.mouseDrag (mouse (graph, end, true, modifiers));
    graph.mouseUp (mouse (graph, end, true, modifiers));
}
template <typename Points>
void requireIdsAndSpacing (const Points& points, std::initializer_list<uint32_t> expected)
{
    REQUIRE (points.size() == expected.size());
    size_t index = 0;
    for (auto id : expected) REQUIRE (points[index++].id == id);
    for (size_t i = 1; i < points.size(); ++i) REQUIRE (points[i].x - points[i - 1].x >= .001f);
}
}

TEST_CASE ("Dragging Remap points against neighbours preserves saved point IDs and coordinate mappings")
{
    using namespace scrr::params; using namespace curveGridUiTests;
    auto processor = testProcessor(); processor->addModule (1, "FrequencyRemap");
    const auto uid = processor->getModuleChain (1).getChild (0)["uid"].toString();
    TransferCurve original; original.points = { { 0, 0, 0, 1 }, { .3f, .2f, 0, 7 }, { .7f, .8f, 0, 8 }, { 1, 1, 0, 2 } }; original.nextPointId = 9;
    processor->setModuleParameter (1, uid, "remapCurve", original.encode());
    processor->assignMacro (0, 1, uid, curveCoordinateParameter (7, true));
    processor->assignMacro (1, 1, uid, curveCoordinateParameter (8, false));
    scrr::gui::TransferCurveEditor graph; graph.setSize (480, 190);
    int edits = 0;
    graph.onChange = [&] (TransferCurve next)
    {
        ++edits; processor->setModuleParameter (1, uid, "remapCurve", next.encode());
        graph.setCurve (TransferCurve::decode (processor->getModuleChain (1).getChild (0)["remapCurve"].toString()));
    };
    const auto area = graph.getPlotBounds();
    for (const auto target : { juce::Point<float> (1.2f, .2f), juce::Point<float> (-.2f, .8f), juce::Point<float> (0, .2f), juce::Point<float> (1, .8f) })
    {
        graph.setCurve (original);
        const bool left = target.y < .5f;
        drag (graph, position (area, left ? .3f : .7f, left ? .2f : .8f), position (area, target.x, target.y));
        requireIdsAndSpacing (graph.getCurve().points, { 1, 7, 8, 2 });
        REQUIRE (processor->getMacroMappings().size() == 2);
    }
    REQUIRE (edits == 4);
    juce::MemoryBlock state; processor->getStateInformation (state);
    auto restored = testProcessor(); restored->setStateInformation (state.getData(), (int) state.getSize());
    requireIdsAndSpacing (TransferCurve::decode (restored->getModuleChain (1).getChild (0)["remapCurve"].toString()).points, { 1, 7, 8, 2 });
    REQUIRE (restored->getMacroMappings().size() == 2);
}

TEST_CASE ("Pinned Mapping centre survives nearby point drags and unchanged decoder round trips")
{
    using namespace scrr::params; using namespace curveGridUiTests;
    scrr::gui::TransferCurveEditor graph; graph.setSize (480, 190); graph.setConstraints (true, true);
    TransferCurve original; original.points = { { 0, 0, 0, 1 }, { .25f, .2f, 0, 7 }, { .5f, .5f, 0, 9 }, { .75f, .8f, 0, 8 }, { 1, 1, 0, 2 } }; original.nextPointId = 10;
    graph.setCurve (original); int edits = 0;
    graph.onChange = [&] (TransferCurve next) { ++edits; next = TransferCurve::decode (next.encode()); next.sanitise (true, true); graph.setCurve (next); };
    const auto area = graph.getPlotBounds();
    drag (graph, position (area, .25f, .2f), position (area, .6f, .7f));
    requireIdsAndSpacing (graph.getCurve().points, { 1, 7, 9, 8, 2 });
    REQUIRE (graph.getCurve().points[1].x < .5f); REQUIRE (graph.getCurve().points[1].y <= .5f);
    REQUIRE (graph.getCurve().points[2].x == .5f); REQUIRE (graph.getCurve().points[2].y == .5f);
    drag (graph, position (area, .75f, .8f), position (area, .4f, .2f));
    requireIdsAndSpacing (graph.getCurve().points, { 1, 7, 9, 8, 2 });
    REQUIRE (graph.getCurve().points[3].x > .5f); REQUIRE (graph.getCurve().points[3].y >= .5f); REQUIRE (edits == 2);
}

TEST_CASE ("LFO drags preserve neighbouring custom point identities through source state decoding")
{
    using namespace scrr::params; using namespace curveGridUiTests;
    scrr::gui::ModulationCurve graph; graph.setSize (480, 190); graph.source.shape = 5;
    const std::vector<CurvePoint> original { { 0, 0, 0, 1 }, { .3f, .2f, 0, 7 }, { .7f, .8f, 0, 8 }, { 1, 1, 0, 2 } };
    graph.source.nextPointId = 9; int edits = 0;
    graph.changed = [&] (ModulationSource next) { ++edits; graph.source = ModulationSource::read (next.state()); };
    const auto area = graph.getLocalBounds().toFloat().reduced (8);
    for (const bool left : { true, false })
    {
        graph.source.points = original;
        drag (graph, position (area, left ? .3f : .7f, left ? .2f : .8f), position (area, left ? 1.2f : -.2f, left ? .2f : .8f));
        requireIdsAndSpacing (graph.source.points, { 1, 7, 8, 2 });
    }
    REQUIRE (edits == 2);
    // A legacy gap can be legal at .001f yet too small for a two-sided safe
    // clamp. Editing Y must keep all three close points instead of asserting.
    graph.source.points = { { 0, 0, 0, 1 }, { .5f, .2f, 0, 7 }, { .50100005f, .8f, 0, 8 }, { .5020001f, .3f, 0, 9 }, { 1, 1, 0, 2 } };
    drag (graph, position (area, .50100005f, .8f), position (area, .6f, .9f));
    requireIdsAndSpacing (graph.source.points, { 1, 7, 8, 9, 2 });
}

TEST_CASE ("Curve drag modifier snaps both axes to independently configured grid divisions")
{
    using namespace scrr::params; using namespace curveGridUiTests; using Grid = scrr::gui::CurveGridSettings;
    PreferenceScope restore; REQUIRE (Grid::set (4, 5, false));
    scrr::gui::TransferCurveEditor transfer; transfer.setSize (480, 190);
    TransferCurve curve; curve.points = { { 0, 0, 0, 1 }, { .3f, .2f, 0, 7 }, { 1, 1, 0, 2 } }; curve.nextPointId = 8;
    transfer.setCurve (curve); const auto a = transfer.getPlotBounds();
    drag (transfer, position (a, .3f, .2f), position (a, .44f, .74f), snapModifiers());
    REQUIRE (std::abs (transfer.getCurve().points[1].x - .5f) < 1.0e-6f);
    REQUIRE (std::abs (transfer.getCurve().points[1].y - .8f) < 1.0e-6f);
    transfer.setCurve (curve);
    drag (transfer, position (a, .3f, .2f), position (a, .44f, .74f));
    REQUIRE (std::abs (transfer.getCurve().points[1].x - .44f) < 1.0e-6f);
    REQUIRE (std::abs (transfer.getCurve().points[1].y - .74f) < 1.0e-6f);
    scrr::gui::ModulationCurve lfo; lfo.setSize (480, 190); lfo.source.shape = 5; lfo.source.points = curve.points;
    const auto b = lfo.getLocalBounds().toFloat().reduced (8);
    drag (lfo, position (b, .3f, .2f), position (b, .44f, .74f), snapModifiers());
    REQUIRE (std::abs (lfo.source.points[1].x - .5f) < 1.0e-6f); REQUIRE (std::abs (lfo.source.points[1].y - .8f) < 1.0e-6f);
    REQUIRE (lfo.source.points[1].id == 7); REQUIRE (findID (transfer, "curve-grid-settings") != nullptr); REQUIRE (findID (lfo, "curve-grid-settings") != nullptr);
    // If a neighbouring point occupies the closest line, use the closest
    // available line; a dense interval with no legal line keeps its X.
    REQUIRE (std::abs (scrr::gui::CurvePointEditing::snapX (.5f, .25f, 0, .5f, 4) - .25f) < 1.0e-6f);
    REQUIRE (std::abs (scrr::gui::CurvePointEditing::snapX (.5f, .42f, .4f, .45f, 4) - .42f) < 1.0e-6f);
}

TEST_CASE ("Grid controls accept every integer count share editor settings and persist outside sound state")
{
    using namespace curveGridUiTests; using Grid = scrr::gui::CurveGridSettings;
    PreferenceScope restore;
    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("curve-grid-preferences", "");
    struct Cleanup { juce::File path; ~Cleanup() { path.deleteRecursively(); } } cleanup { directory };
    const auto file = directory.getChildFile ("curve-grid.txt");
    REQUIRE (Grid::read (file).x == 8); REQUIRE (Grid::read (file).y == 8);
    REQUIRE (Grid::write ({ 7, 13 }, file)); REQUIRE (Grid::read (file).x == 7); REQUIRE (Grid::read (file).y == 13);
    REQUIRE (file.replaceWithText ("7 invalid")); REQUIRE (Grid::read (file).x == 8); REQUIRE (Grid::read (file).y == 8);
    auto processor = testProcessor(); const auto sound = processor->copyPresetState();
    scrr::gui::CurveGridControls controls; controls.setSize (248, 68);
    auto* x = dynamic_cast<juce::Slider*> (findID (controls, "curve-grid-x"));
    auto* y = dynamic_cast<juce::Slider*> (findID (controls, "curve-grid-y")); REQUIRE (x != nullptr); REQUIRE (y != nullptr);
    x->setValue (7, juce::sendNotificationSync); y->setValue (13, juce::sendNotificationSync);
    REQUIRE (Grid::get().x == 7); REQUIRE (Grid::get().y == 13);
    REQUIRE (Grid::read (Grid::preferenceFile()).x == 7); REQUIRE (Grid::read (Grid::preferenceFile()).y == 13);
    scrr::gui::CurveGridControls second; second.setSize (248, 68);
    REQUIRE (dynamic_cast<juce::Slider*> (findID (second, "curve-grid-x"))->getValue() == 7);
    REQUIRE (dynamic_cast<juce::Slider*> (findID (second, "curve-grid-y"))->getValue() == 13);
    REQUIRE (processor->copyPresetState().isEquivalentTo (sound));
    // The drawing uses precisely the same normalized positions as snapping.
    juce::Image image (juce::Image::RGB, 103, 103, true); juce::Graphics graphics (image); graphics.setColour (juce::Colours::white);
    Grid::drawGrid (graphics, { 1, 1, 100, 100 });
    REQUIRE (image.getPixelAt ((int) (1 + 100.0f * Grid::snap (.42f, 7)), 37) == juce::Colours::white);
    REQUIRE (image.getPixelAt (37, (int) (101 - 100.0f * Grid::snap (.42f, 13))) == juce::Colours::white);
}
