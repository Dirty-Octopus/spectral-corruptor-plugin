// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "../Source/gui/components/RemapFormulaEditor.h"

namespace formulaPopoutTests {
using Preference = scrr::gui::FormulaWindowPreferences;
struct PreferenceScope
{
    explicit PreferenceScope (Preference::Mode mode) : previous (Preference::get()) { Preference::set (mode); }
    ~PreferenceScope() { Preference::set (previous); }
    Preference::Mode previous;
};
struct Fixture
{
    Fixture()
    {
        using namespace scrr::params;
        RemapFormula graph;
        FormulaNode constant; constant.id = 1; constant.op = FormulaOp::Constant; constant.value = 2; constant.canvasX = 280; constant.canvasY = 40;
        FormulaParameter parameter; parameter.id = "popout-control"; parameter.name = "Ratio"; parameter.maximum = 8; parameter.defaultValue = 2;
        FormulaNode control; control.id = 2; control.op = FormulaOp::Parameter; control.parameterId = parameter.id; control.canvasX = 40; control.canvasY = 230;
        graph.nodes.push_back (constant); graph.nodes.push_back (control); graph.parameters.push_back (parameter); graph.outputNode = 0;
        state.setProperty ("uid", "popout-module", nullptr); state.setProperty ("remapFormula", graph.encode(), nullptr);
        state.setProperty (parameter.property(), 2.0, nullptr);
        host.setSize (1180, 900); host.addAndMakeVisible (editor); editor.setBounds (700, 40, 450, editor.getPreferredHeight()); editor.setState (state);
        editor.onChange = [this] (const juce::Identifier& key, const juce::var& value) { state.setProperty (key, value, nullptr); };
        editor.layoutChanged = [this] { editor.setSize (450, editor.getPreferredHeight()); };
        host.addToDesktop (0); host.setVisible (true); pump (30);
    }
    ~Fixture() { editor.closePopout(); host.setVisible (false); }
    juce::ValueTree state { "module" };
    juce::Component host;
    scrr::gui::RemapFormulaEditor editor;
};
inline bool displayAvailable()
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr) return true;
    std::cout << "[SKIP Formula popout native subsection: no display]\n"; return false;
}
}

TEST_CASE ("Formula window preference defaults to native and persists only the presentation choice")
{
    using P = formulaPopoutTests::Preference;
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("spectral-formula-window-test", "", false);
    auto file = folder.getChildFile ("formula-window.txt");
    REQUIRE (P::read (file) == P::Mode::native);
    REQUIRE (P::write (P::Mode::contained, file)); REQUIRE (P::read (file) == P::Mode::contained);
    REQUIRE (P::write (P::Mode::native, file)); REQUIRE (P::read (file) == P::Mode::native);
    REQUIRE (file.replaceWithText ("unknown")); REQUIRE (P::read (file) == P::Mode::native);
    REQUIRE (file.replaceWithText (juce::String::repeatedString ("contained", 8))); REQUIRE (P::read (file) == P::Mode::native);
    REQUIRE (folder.deleteRecursively());
}

TEST_CASE ("Formula popout moves one editable workspace and grows its actual graph in both window modes")
{
    using namespace formulaPopoutTests;
    if (! displayAvailable()) return;
    for (auto mode : { Preference::Mode::native, Preference::Mode::contained })
    {
        PreferenceScope preference (mode); Fixture f;
        auto* canvas = findID (f.editor, "formula-canvas"); REQUIRE (canvas != nullptr);
        auto* slider = f.editor.getParameterSliders().front();
        const auto initial = f.state.createCopy(); const auto inlineSize = canvas->getBounds();
        auto* launch = dynamic_cast<juce::Button*> (findID (f.editor, "formula-popout")); REQUIRE (launch != nullptr); launch->onClick();
        REQUIRE (f.editor.isPopoutOpen());
        auto* window = f.editor.getPopoutWindow(); REQUIRE (window != nullptr);
        // Let AppKit finish applying the initial on-screen frame before taking
        // a geometry baseline. CI displays can be smaller than the default.
        pump (40);
        REQUIRE (window->isUsingNativeTitleBar() == (mode == Preference::Mode::native));
        REQUIRE (window->isOnDesktop() == (mode == Preference::Mode::native));
        REQUIRE (window->isDraggable()); REQUIRE (window->isResizable());
        REQUIRE (findID (*window, "formula-canvas") == canvas); REQUIRE (f.editor.getParameterSliders().front() == slider);
        REQUIRE (canvas->getWidth() > inlineSize.getWidth()); REQUIRE (canvas->getHeight() > inlineSize.getHeight());
        REQUIRE (f.state.isEquivalentTo (initial));
        const int originalWindowHeight = window->getHeight(), originalCanvasHeight = canvas->getHeight();
        const auto expectResize = [&] (int requestedHeight)
        {
            const int beforeWindowHeight = window->getHeight(), beforeCanvasHeight = canvas->getHeight();
            window->setSize (window->getWidth(), requestedHeight);
            const bool laidOut = waitForUI ([&]
            {
                return window->getHeight() == requestedHeight
                    && canvas->getHeight() - beforeCanvasHeight == window->getHeight() - beforeWindowHeight;
            });
            if (! laidOut)
                std::cerr << "Formula resize: mode=" << (mode == Preference::Mode::native ? "native" : "contained")
                          << " requested=" << requestedHeight << " window=" << beforeWindowHeight << "->" << window->getHeight()
                          << " canvas=" << beforeCanvasHeight << "->" << canvas->getHeight() << '\n';
            REQUIRE (laidOut); REQUIRE (canvas->getHeight() != beforeCanvasHeight);
        };
        // Shrink and restore the known displayed size instead of requiring a
        // native window to grow beyond the runner's available screen height.
        expectResize (originalWindowHeight - 80);
        REQUIRE (canvas->getHeight() == originalCanvasHeight - 80);
        expectResize (originalWindowHeight);
        REQUIRE (canvas->getHeight() == originalCanvasHeight);
        slider->setValue (3.25, juce::sendNotificationSync);
        REQUIRE (std::abs ((double) f.state["formulaParam_popout-control"] - 3.25) < 1.0e-12);
        auto* field = dynamic_cast<juce::TextEditor*> (findID (*window, "formula-constant-1")); REQUIRE (field != nullptr);
        field->setText ("7.5", false); field->onReturnKey();
        const auto graph = scrr::params::RemapFormula::decode (f.state["remapFormula"].toString());
        REQUIRE (std::abs (graph.nodes[1].value - 7.5) < 1.0e-12);
        if (mode == Preference::Mode::contained)
        {
            f.host.setSize (1000, 760);
            REQUIRE (f.host.getLocalBounds().contains (window->getBounds()));
            REQUIRE (findID (*window, "formula-canvas") == canvas);
        }
        f.editor.closePopout(); REQUIRE (! f.editor.isPopoutOpen());
        REQUIRE (findID (f.editor, "formula-canvas") == canvas); REQUIRE (f.editor.getParameterSliders().front() == slider);
        REQUIRE (canvas->getHeight() == inlineSize.getHeight()); REQUIRE (std::abs (slider->getValue() - 3.25) < 1.0e-12);
        f.editor.openPopout(); REQUIRE (f.editor.isPopoutOpen());
        f.editor.getPopoutWindow()->closeButtonPressed();
        REQUIRE (waitForUI ([&] { return ! f.editor.isPopoutOpen(); }));
        REQUIRE (findID (f.editor, "formula-canvas") == canvas);
    }
}

TEST_CASE ("Formula popout closes safely on selection changes and ignores an old deferred close")
{
    using namespace formulaPopoutTests;
    if (! displayAvailable()) return;
    PreferenceScope preference (Preference::Mode::native); Fixture f;
    f.editor.openPopout(); juce::Component::SafePointer<juce::DocumentWindow> old (f.editor.getPopoutWindow()); REQUIRE (old != nullptr);
    old->closeButtonPressed();
    auto next = f.state.createCopy(); next.setProperty ("uid", "next-formula-module", nullptr);
    f.editor.setState (next); REQUIRE (! f.editor.isPopoutOpen()); REQUIRE (old == nullptr);
    f.editor.openPopout(); REQUIRE (f.editor.isPopoutOpen());
    pump (40); REQUIRE (f.editor.isPopoutOpen()); REQUIRE (f.editor.getModuleUid() == "next-formula-module");
    f.editor.setVisible (false); REQUIRE (waitForUI ([&] { return ! f.editor.isPopoutOpen(); }));
}

TEST_CASE ("Formula popout keeps Settings reachable in a narrow 640 by 480 window")
{
    using namespace formulaPopoutTests;
    if (! displayAvailable()) return;
    for (auto mode : { Preference::Mode::native, Preference::Mode::contained })
    {
        PreferenceScope preference (mode); Fixture f; f.editor.openPopout();
        auto* window = f.editor.getPopoutWindow(); REQUIRE (window != nullptr); window->setSize (640, 480);
        auto* details = dynamic_cast<juce::Button*> (findID (*window, "formula-details")); REQUIRE (details != nullptr);
        details->setToggleState (true, juce::dontSendNotification); details->onClick();
        auto* surface = findID (*window, "formula-surface"); REQUIRE (surface != nullptr);
        for (const auto* id : { "formula-view-x-min", "formula-view-y-max", "formula-view-samples", "formula-node-display", "formula-node-language", "formula-invalid-policy", "formula-random-complexity" })
        {
            auto* control = findID (*window, id); REQUIRE (control != nullptr); REQUIRE (control->isShowing());
            REQUIRE (surface->getLocalBounds().contains (surface->getLocalArea (control, control->getLocalBounds())));
        }
        auto* canvas = findID (*window, "formula-canvas"); REQUIRE (canvas != nullptr); REQUIRE (canvas->getHeight() >= 120);
    }
}

TEST_CASE ("Contained Formula windows retain logical scaling through nested Standalone style wrappers")
{
    using namespace formulaPopoutTests;
    if (! displayAvailable()) return;
    PreferenceScope preference (Preference::Mode::contained);
    for (const bool identifiedContent : { true, false })
    {
        Fixture f; juce::Component standaloneContent, pluginShell, logicalContent, inspector;
        f.host.addAndMakeVisible (standaloneContent); standaloneContent.setBounds (0, 0, 620, 500);
        standaloneContent.addAndMakeVisible (pluginShell); pluginShell.setBounds (10, 40, 590, 450);
        pluginShell.addAndMakeVisible (logicalContent); logicalContent.setBounds (0, 0, 1180, 900);
        if (identifiedContent) logicalContent.setComponentID ("editor-content");
        else logicalContent.setTransform (juce::AffineTransform::scale (.5f));
        logicalContent.addAndMakeVisible (inspector); inspector.setBounds (logicalContent.getLocalBounds());
        inspector.addAndMakeVisible (f.editor);
        const auto initial = f.state.createCopy(); auto* parameter = f.editor.getParameterSliders().front();
        f.editor.openPopout(); auto* window = f.editor.getPopoutWindow(); REQUIRE (window != nullptr);
        REQUIRE (window->getParentComponent() == &logicalContent); REQUIRE (! window->isOnDesktop());
        REQUIRE (logicalContent.getLocalBounds().contains (window->getBounds()));
        // The named surface must remain the parent when scaling changes from
        // identity after opening, rather than being fixed to an outer wrapper.
        logicalContent.setTransform (juce::AffineTransform::scale (.5f));
        REQUIRE (std::abs (window->getScreenBounds().getWidth() * 2 - window->getWidth()) <= 2);
        logicalContent.setTransform (juce::AffineTransform::scale (1.5f));
        REQUIRE (std::abs ((double) window->getScreenBounds().getWidth() - window->getWidth() * 1.5) <= 2);
        logicalContent.setSize (1000, 760); REQUIRE (logicalContent.getLocalBounds().contains (window->getBounds()));
        REQUIRE (f.editor.getParameterSliders().front() == parameter); REQUIRE (f.state.isEquivalentTo (initial));
        f.editor.closePopout(); f.host.addAndMakeVisible (f.editor);
        REQUIRE (f.editor.getParameterSliders().front() == parameter); REQUIRE (f.state.isEquivalentTo (initial));
    }
}

TEST_CASE ("Destroying a Formula editor with a pending popout close releases the window once")
{
    using namespace formulaPopoutTests;
    if (! displayAvailable()) return;
    PreferenceScope preference (Preference::Mode::native);
    auto fixture = std::make_unique<Fixture>(); fixture->editor.openPopout();
    juce::Component::SafePointer<juce::DocumentWindow> window (fixture->editor.getPopoutWindow()); REQUIRE (window != nullptr);
    window->closeButtonPressed(); fixture.reset(); REQUIRE (window == nullptr); pump (40);
}
