// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "../Source/gui/components/RemapFormulaEditor.h"

namespace formulaUiTests {
inline void foreground (juce::Component& component)
{
   #if JUCE_MAC
    juce::Process::setDockIconVisible (true);
   #endif
    juce::Process::makeForegroundProcess();
    component.addToDesktop (0); component.setVisible (true);
    component.setWantsKeyboardFocus (true); component.toFront (true); component.grabKeyboardFocus();
    // App activation and native key-window acquisition are asynchronous in a
    // fresh CLI process. Opening a popup before both complete lets its focus
    // timer dismiss it, even though direct component snapshots already work.
    int readyDispatches = 0;
    const bool ready = waitForUI ([&]
    {
        auto* peer = component.getPeer();
        if (juce::Process::isForegroundProcess() && peer != nullptr && peer->isFocused()
            && component.isShowing() && component.hasKeyboardFocus (true))
            return ++readyDispatches >= 2;
        readyDispatches = 0;
        if (! juce::Process::isForegroundProcess()) juce::Process::makeForegroundProcess();
        component.toFront (true);
        if (peer != nullptr) peer->grabFocus();
        component.grabKeyboardFocus();
        return false;
    });
    if (! ready)
    {
        auto* peer = component.getPeer();
        std::cerr << "Formula foreground readiness: active=" << juce::Process::isForegroundProcess()
                  << " peer=" << (peer != nullptr) << " focused=" << (peer != nullptr && peer->isFocused())
                  << " keyboard=" << component.hasKeyboardFocus (true) << '\n';
    }
    REQUIRE (ready);
}
inline juce::Component* activeMenu (int requestedResult = 0)
{
    juce::Component* popup = nullptr;
    const auto initialModals = juce::Component::getNumCurrentlyModalComponents();
    const auto findPopup = [&]
    {
        for (int i = 0; i < juce::Component::getNumCurrentlyModalComponents(); ++i)
            if (auto* component = juce::Component::getCurrentlyModalComponent (i))
                if (auto* handler = component->getAccessibilityHandler())
                    if (handler->getRole() == juce::AccessibilityRole::popupMenu && component->isShowing()) { popup = component; return true; }
        return false;
    };
    // showMenuAsync enters modality synchronously. Re-pumping the native loop
    // after inspecting an already open menu can let unrelated focus events
    // dismiss it before chooseMenu selects the item in this synthetic gesture.
    const bool appeared = findPopup() || waitForUI (findPopup);
    if (! appeared)
    {
        const auto* focused = juce::Component::getCurrentlyFocusedComponent();
        std::cerr << "Formula popup timeout: result=" << requestedResult << " initialModals=" << initialModals
                  << " currentModals=" << juce::Component::getNumCurrentlyModalComponents()
                  << " foreground=" << juce::Process::isForegroundProcess()
                  << " focus=" << (focused ? focused->getComponentID().toStdString() : "none") << '\n';
    }
    REQUIRE (appeared);
    return popup;
}
inline bool menuContains (juce::Component& menu, const juce::String& title)
{
    if (auto* accessibility = menu.getAccessibilityHandler()) if (accessibility->getTitle() == title) return true;
    for (auto* child : menu.getChildren()) if (menuContains (*child, title)) return true;
    return false;
}
inline void chooseMenu (int result)
{
    juce::Component::SafePointer<juce::Component> menu (activeMenu (result)); REQUIRE (menu != nullptr);
    menu->exitModalState (result);
    // Editing an inline numeric value can legitimately start a second modal
    // text editor as the popup closes. Wait only for this popup to finish.
    REQUIRE (waitForUI ([&] { return menu == nullptr || ! menu->isCurrentlyModal (false); }));
}
inline void finishInlineEditBeforePointerInput (juce::Component& target)
{
    // Direct mouseDown calls bypass JUCE's normal modal-input dispatch. A
    // slider's inline Label must commit its edit before a click elsewhere can
    // open another menu, just as Label::inputAttemptWhenModal does natively.
    auto* modal = juce::Component::getCurrentlyModalComponent();
    if (auto* label = dynamic_cast<juce::Label*> (modal);
        label != nullptr && label->isBeingEdited() && target.isCurrentlyBlockedByAnotherModalComponent())
    {
        juce::Component::SafePointer<juce::Label> editing (label);
        modal->inputAttemptWhenModal();
        REQUIRE (waitForUI ([&] { return editing == nullptr || (! editing->isBeingEdited() && ! editing->isCurrentlyModal (false)); }));
    }
}
inline void click (scrr::gui::RemapFormulaEditor& editor, juce::Point<float> position, bool popup = false)
{
    auto* canvas = findID (editor, "formula-canvas"); REQUIRE (canvas != nullptr);
    finishInlineEditBeforePointerInput (*canvas);
    const auto mods = popup ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier;
    canvas->mouseDown (mouse (*canvas, position, false, mods));
    if (! popup) canvas->mouseUp (mouse (*canvas, position, false, mods));
}
inline void nodeMenu (scrr::gui::RemapFormulaEditor& editor, int id)
{
    const auto bounds = editor.getNodeBounds (id); REQUIRE (! bounds.isEmpty());
    click (editor, { bounds.getCentreX(), bounds.getY() + 12 * (float) editor.getCanvasZoom() }, true);
}
inline void editText (juce::Component& root, const char* id, const char* value)
{
    auto* field = dynamic_cast<juce::TextEditor*> (findID (root, id)); REQUIRE (field != nullptr);
    finishInlineEditBeforePointerInput (*field);
    field->setText (value, false); field->onReturnKey();
}
inline void pressToolbar (juce::Component& root, const char* id)
{
    auto* button = dynamic_cast<juce::Button*> (findID (root, id)); REQUIRE (button != nullptr);
    REQUIRE (button->isEnabled()); REQUIRE (button->onClick != nullptr);
    if (button->getClickingTogglesState()) button->setToggleState (! button->getToggleState(), juce::dontSendNotification);
    button->onClick();
}
inline juce::String withoutLayout (scrr::params::RemapFormula graph)
{
    for (auto& node : graph.nodes) node.canvasX = node.canvasY = 0;
    return graph.encode();
}
inline scrr::params::RemapFormula ratioGraph()
{
    using namespace scrr::params;
    RemapFormula graph; graph.nodes.front().canvasX = 30; graph.nodes.front().canvasY = 30;
    FormulaNode constant; constant.id = 1; constant.op = FormulaOp::Constant; constant.value = 2; constant.canvasX = 30; constant.canvasY = 230;
    FormulaNode multiply; multiply.id = 2; multiply.op = FormulaOp::Multiply; multiply.canvasX = 350; multiply.canvasY = 80;
    graph.nodes.push_back (constant); graph.nodes.push_back (multiply); return graph;
}
struct Fixture
{
    explicit Fixture (scrr::params::RemapFormula graph = {},
                      juce::String view = "{\"outputX\":680,\"outputY\":90}")
    {
        state.setProperty ("uid", "formula-ui", nullptr); state.setProperty ("remapFormula", graph.encode(), nullptr);
        state.setProperty ("formulaView", std::move (view), nullptr);
        editor.setState (state); editor.setSize (900, editor.getPreferredHeight());
        editor.onChange = [this] (const juce::Identifier& key, const juce::var& value) { state.setProperty (key, value, nullptr); };
        editor.layoutChanged = [this] { editor.setSize (900, editor.getPreferredHeight()); };
        foreground (editor);
    }
    ~Fixture() { juce::PopupMenu::dismissAllActiveMenus(); pump (20); editor.setVisible (false); }
    scrr::params::RemapFormula graph() const { return scrr::params::RemapFormula::decode (state["remapFormula"].toString()); }
    juce::ValueTree state { "module" }; scrr::gui::RemapFormulaEditor editor;
};
}

TEST_CASE ("Formula blank context menu adds inline constants without legacy add buttons")
{
    using namespace scrr::params; using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    Fixture f;
    REQUIRE (findID (f.editor, "formula-add-node") == nullptr); REQUIRE (findID (f.editor, "formula-add-param") == nullptr);
    REQUIRE (findID (f.editor, "formula-delete-node") == nullptr);
    const auto before = f.state["remapFormula"].toString();
    click (f.editor, { 500, 330 }, true); chooseMenu (0); REQUIRE (f.state["remapFormula"].toString() == before);
    click (f.editor, { 500, 330 }, true); REQUIRE (menuContains (*activeMenu(), "Constant")); chooseMenu ((int) FormulaOp::Constant + 1);
    auto graph = f.graph(); REQUIRE (graph.nodes.size() == 2); REQUIRE (graph.nodes.back().op == FormulaOp::Constant);
    const auto id = graph.nodes.back().id;
    const auto fieldId = "formula-constant-" + juce::String (id);
    auto* field = dynamic_cast<juce::TextEditor*> (findID (f.editor, fieldId)); REQUIRE (field != nullptr); REQUIRE (field->isShowing());
    field->setText ("800", false); field->onReturnKey();
    nodeMenu (f.editor, id); chooseMenu (1003);
    const auto program = FormulaProgram::compile (f.graph()); REQUIRE (program.valid);
    std::array<double, RemapFormula::maxParameters> values {};
    REQUIRE (std::abs (program.evaluate (200, values).value - 800) < 1.0e-9);
}

TEST_CASE ("Formula canvas connects math nodes and node context edits preserve unrelated wires")
{
    using namespace scrr::params; using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    Fixture f (ratioGraph());
    click (f.editor, f.editor.getOutputPosition (0)); REQUIRE (f.editor.isEditing());
    click (f.editor, f.editor.getInputPosition (2, 0)); REQUIRE (! f.editor.isEditing());
    click (f.editor, f.editor.getOutputPosition (1)); click (f.editor, f.editor.getInputPosition (2, 1));
    click (f.editor, f.editor.getOutputPosition (2)); click (f.editor, f.editor.getOutputDestination());
    auto graph = f.graph(); REQUIRE (graph.outputNode == 2);
    auto program = FormulaProgram::compile (graph); REQUIRE (program.valid);
    std::array<double, RemapFormula::maxParameters> values {}; REQUIRE (std::abs (program.evaluate (200, values).value - 400) < 1.0e-9);
    nodeMenu (f.editor, 2); chooseMenu (1201);
    graph = f.graph(); REQUIRE (graph.nodes.back().inputs[0] == 0); REQUIRE (graph.nodes.back().inputs[1] == -1);
    REQUIRE (! FormulaProgram::compile (graph).valid);
    auto* status = dynamic_cast<juce::Label*> (findID (f.editor, "formula-status")); REQUIRE (status != nullptr); REQUIRE (status->getText().isNotEmpty());
    nodeMenu (f.editor, 2); chooseMenu (1002);
    graph = f.graph(); REQUIRE (graph.nodes.size() == 2); REQUIRE (graph.outputNode == -1);
}

TEST_CASE ("Formula wire context insertion preserves the source and rewires only the selected destination")
{
    using namespace scrr::params; using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto graph = ratioGraph(); graph.nodes.back().inputs = {{ 0, 1, -1 }}; graph.outputNode = 2;
    Fixture f (graph); const auto before = f.state["remapFormula"].toString();
    click (f.editor, f.editor.getWireMidpoint (-1, 0), true); chooseMenu (0);
    REQUIRE (f.state["remapFormula"].toString() == before);
    click (f.editor, f.editor.getWireMidpoint (-1, 0), true);
    REQUIRE (menuContains (*activeMenu(), "Negate")); REQUIRE (! menuContains (*activeMenu(), "Constant"));
    chooseMenu ((int) FormulaOp::Negate + 1);
    graph = f.graph(); REQUIRE (graph.nodes.size() == 4);
    const auto inserted = graph.nodes.back(); REQUIRE (inserted.op == FormulaOp::Negate); REQUIRE (inserted.inputs[0] == 2);
    REQUIRE (graph.outputNode == inserted.id); REQUIRE (graph.nodes[2].inputs[0] == 0); REQUIRE (graph.nodes[2].inputs[1] == 1);
    auto program = FormulaProgram::compile (graph); REQUIRE (program.valid);
    std::array<double, RemapFormula::maxParameters> values {}; REQUIRE (std::abs (program.evaluate (200, values).value + 400) < 1.0e-9);
    click (f.editor, f.editor.getWireMidpoint (2, 1), true); chooseMenu (1300);
    graph = f.graph(); REQUIRE (graph.nodes[2].inputs[1] == -1); REQUIRE (graph.nodes[2].inputs[0] == 0);
    REQUIRE (graph.outputNode == inserted.id);
}

TEST_CASE ("Formula wire insertion supplies neutral operands for Add Multiply and Power")
{
    using namespace scrr::params; using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    for (const auto operation : { FormulaOp::Add, FormulaOp::Multiply, FormulaOp::Power })
    {
        auto graph = ratioGraph(); graph.nodes.back().inputs = {{ 0, 1, -1 }}; graph.outputNode = 2;
        Fixture f (graph);
        click (f.editor, f.editor.getWireMidpoint (-1, 0), true); chooseMenu ((int) operation + 1);
        graph = f.graph(); REQUIRE (graph.nodes.size() == 5);
        const auto inserted = std::find_if (graph.nodes.begin(), graph.nodes.end(), [&] (const FormulaNode& node) { return node.id == graph.outputNode; });
        REQUIRE (inserted != graph.nodes.end()); REQUIRE (inserted->op == operation); REQUIRE (inserted->inputs[0] == 2);
        const auto program = FormulaProgram::compile (graph); REQUIRE (program.valid);
        std::array<double, RemapFormula::maxParameters> values {};
        REQUIRE (std::abs (program.evaluate (200, values).value - 400) < 1.0e-9);
    }
}

TEST_CASE ("Formula inline parameters retain stable mapping IDs through node Details and reference deletion")
{
    using namespace scrr::params; using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    Fixture f; int changes = 0; f.editor.onParametersChanged = [&] { ++changes; };
    click (f.editor, { 340, 270 }, true); REQUIRE (menuContains (*activeMenu(), "New parameter")); chooseMenu (500);
    auto graph = f.graph(); REQUIRE (graph.parameters.size() == 1); REQUIRE (graph.nodes.size() == 2);
    const auto property = graph.parameters.front().property(); const auto parameterNode = graph.nodes.back().id;
    auto sliders = f.editor.getParameterSliders(); REQUIRE (sliders.size() == 1);
    REQUIRE (sliders.front()->getComponentID() == property.toString()); REQUIRE (sliders.front()->isShowing());
    auto* canvas = findID (f.editor, "formula-canvas"); REQUIRE (canvas != nullptr); REQUIRE (canvas->isParentOf (sliders.front()));
    juce::Identifier mapped;
    f.editor.mappingMenu = [&] (juce::Component& target, const juce::Identifier& key) { REQUIRE (&target == sliders.front()); mapped = key; };
    finishInlineEditBeforePointerInput (*sliders.front());
    sliders.front()->mouseDown (mouse (*sliders.front(), { 10, 10 }, false, juce::ModifierKeys::rightButtonModifier)); REQUIRE (mapped == property);
    nodeMenu (f.editor, parameterNode); chooseMenu (1001);
    editText (f.editor, "formula-param-name", "Offset"); editText (f.editor, "formula-param-max", "2"); editText (f.editor, "formula-param-step", "0.25");
    graph = f.graph(); REQUIRE (graph.parameters.front().name == "Offset"); REQUIRE (graph.parameters.front().property() == property);
    REQUIRE (std::abs (graph.parameters.front().maximum - 2) < .001); REQUIRE (std::abs (graph.parameters.front().step - .25) < .001);
    sliders = f.editor.getParameterSliders(); REQUIRE (sliders.size() == 1);
    sliders.front()->setValue (1.25, juce::sendNotificationSync); REQUIRE (std::abs ((double) f.state[property] - 1.25) < .001);
    REQUIRE (changes >= 4);
    const juce::Point<float> blank { (float) canvas->getWidth() - 30, (float) canvas->getHeight() - 30 };
    REQUIRE (canvas->getLocalBounds().toFloat().contains (blank));
    click (f.editor, blank, true); chooseMenu (100);
    graph = f.graph(); REQUIRE (graph.nodes.size() == 3); REQUIRE (graph.parameters.size() == 1);
    REQUIRE (graph.nodes.back().parameterId == graph.parameters.front().id);
    nodeMenu (f.editor, parameterNode); chooseMenu (1004);
    graph = f.graph(); REQUIRE (graph.parameters.empty()); REQUIRE (graph.nodes.size() == 1); REQUIRE (f.editor.getParameterSliders().empty());
}

TEST_CASE ("Formula settings retain separate display state and normalized coordinates")
{
    using namespace scrr::params; using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    Fixture f;
    auto* settings = dynamic_cast<juce::Button*> (findID (f.editor, "formula-details")); REQUIRE (settings != nullptr);
    settings->setToggleState (true, juce::dontSendNotification); settings->onClick();
    const auto graphBefore = f.state["remapFormula"].toString(); editText (f.editor, "formula-view-x-max", "10000");
    REQUIRE (f.state["remapFormula"].toString() == graphBefore);
    auto view = juce::JSON::parse (f.state["formulaView"].toString()); REQUIRE (std::abs ((double) view["xMax"] - 10000) < .001);
    for (const auto* id : { "formula-node-language", "formula-node-display", "formula-probe-mode" })
    {
        auto* choice = dynamic_cast<juce::ComboBox*> (findID (f.editor, id)); REQUIRE (choice != nullptr);
        choice->setSelectedId (1, juce::sendNotificationSync);
        const auto beforeView = f.state["formulaView"].toString(); choice->setSelectedId (2, juce::sendNotificationSync);
        REQUIRE (f.state["remapFormula"].toString() == graphBefore); REQUIRE (f.state["formulaView"].toString() != beforeView);
    }
    view = juce::JSON::parse (f.state["formulaView"].toString());
    REQUIRE ((bool) view["chinese"]); REQUIRE ((bool) view["symbolic"]); REQUIRE ((bool) view["followMouse"]);
    auto* units = dynamic_cast<juce::ComboBox*> (findID (f.editor, "formula-units")); REQUIRE (units != nullptr); units->setSelectedId (2, juce::sendNotificationSync);
    REQUIRE (f.graph().unit == FormulaUnit::Normalized);
    view = juce::JSON::parse (f.state["formulaView"].toString()); REQUIRE (std::abs ((double) view["xMax"] - 1) < .001); REQUIRE (! (bool) view["logX"]);
    f.editor.effectiveValue = [] (const juce::Identifier&, double base) { return base; }; f.editor.refreshPreview();
}

TEST_CASE ("Formula invalid destination policy saves independently from graph and display settings")
{
    using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    Fixture f;
    auto* settings = dynamic_cast<juce::Button*> (findID (f.editor, "formula-details")); REQUIRE (settings != nullptr);
    settings->setToggleState (true, juce::dontSendNotification); settings->onClick();
    auto* policy = dynamic_cast<juce::ComboBox*> (findID (f.editor, "formula-invalid-policy")); REQUIRE (policy != nullptr);
    REQUIRE (policy->isShowing()); REQUIRE (policy->getSelectedId() == 1);
    const auto graphBefore = f.state["remapFormula"].toString(); const auto viewBefore = f.state["formulaView"].toString();
    policy->setSelectedId (2, juce::sendNotificationSync);
    REQUIRE ((int) f.state["remapInvalidPolicy"] == 1);
    REQUIRE (f.state["remapFormula"].toString() == graphBefore); REQUIRE (f.state["formulaView"].toString() == viewBefore);
    scrr::gui::RemapFormulaEditor restored; restored.setState (f.state.createCopy());
    auto* restoredPolicy = dynamic_cast<juce::ComboBox*> (findID (restored, "formula-invalid-policy")); REQUIRE (restoredPolicy != nullptr);
    REQUIRE (restoredPolicy->getSelectedId() == 2);
    policy->setSelectedId (1, juce::sendNotificationSync); REQUIRE ((int) f.state["remapInvalidPolicy"] == 0);
    REQUIRE (f.state["remapFormula"].toString() == graphBefore); REQUIRE (f.state["formulaView"].toString() == viewBefore);
}

TEST_CASE ("Formula wheel zoom changes only the view and keeps socket hit testing aligned")
{
    using namespace scrr::params; using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    Fixture f;
    auto* canvas = findID (f.editor, "formula-canvas"); REQUIRE (canvas != nullptr);
    const auto graphBefore = f.state["remapFormula"].toString();
    const auto before = f.editor.getNodeBounds (0); const double zoomBefore = f.editor.getCanvasZoom();
    juce::MouseWheelDetails wheel; wheel.deltaY = .4f;
    canvas->mouseWheelMove (mouse (*canvas, { 450, 200 }, false, juce::ModifierKeys::noModifiers), wheel);
    const double zoomAfter = f.editor.getCanvasZoom(); REQUIRE (zoomAfter > zoomBefore);
    const auto after = f.editor.getNodeBounds (0);
    REQUIRE (std::abs (after.getWidth() / before.getWidth() - zoomAfter / zoomBefore) < .02);
    REQUIRE (f.state["remapFormula"].toString() == graphBefore);
    REQUIRE (std::abs ((double) juce::JSON::parse (f.state["formulaView"].toString())["zoom"] - zoomAfter) < 1.0e-6);
    click (f.editor, f.editor.getWireMidpoint (-1, 0), true); chooseMenu (1300); REQUIRE (f.graph().outputNode == -1);
    click (f.editor, f.editor.getOutputPosition (0)); click (f.editor, f.editor.getOutputDestination());
    REQUIRE (FormulaProgram::compile (f.graph()).valid); REQUIRE (f.graph().outputNode == 0);
    const auto connected = f.state["remapFormula"].toString(); wheel.deltaY = -100.0f;
    for (int repeat = 0; repeat < 5; ++repeat) canvas->mouseWheelMove (mouse (*canvas, { 450, 200 }, false, juce::ModifierKeys::noModifiers), wheel);
    REQUIRE (std::isfinite (f.editor.getCanvasZoom())); REQUIRE (f.editor.getCanvasZoom() > 0);
    REQUIRE (f.editor.getCanvasZoom() < zoomAfter); REQUIRE (f.state["remapFormula"].toString() == connected);
    f.state.setProperty ("remapFormula", ratioGraph().encode(), nullptr);
    f.state.setProperty ("formulaView", "{\"outputX\":680,\"outputY\":90,\"zoom\":0.5,\"panX\":0,\"panY\":0}", nullptr); f.editor.setState (f.state);
    click (f.editor, f.editor.getOutputPosition (1)); click (f.editor, f.editor.getInputPosition (2, 1));
    REQUIRE (f.graph().nodes[2].inputs[0] == -1); REQUIRE (f.graph().nodes[2].inputs[1] == 1);
    click (f.editor, f.editor.getOutputPosition (0)); click (f.editor, f.editor.getInputPosition (2, 0));
    click (f.editor, f.editor.getOutputPosition (2)); click (f.editor, f.editor.getOutputDestination());
    const auto smallProgram = FormulaProgram::compile (f.graph()); REQUIRE (smallProgram.valid);
    REQUIRE (std::abs (smallProgram.evaluate (200, {}).value - 400) < 1.0e-9);
}

TEST_CASE ("Formula bring offscreen preserves executable data and leaves partly visible nodes in place")
{
    using namespace scrr::params; using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    Fixture f ({}, "{\"outputX\":-8000,\"outputY\":-6000,\"zoom\":1,\"panX\":0,\"panY\":0}");
    auto* canvas = findID (f.editor, "formula-canvas"); REQUIRE (canvas != nullptr);
    const auto visible = canvas->getLocalBounds().toFloat();
    auto graph = ratioGraph(); graph.nodes[1].canvasX = -1250000.25; graph.nodes[1].canvasY = 3400000.5;
    graph.nodes[2].inputs = {{ 0, 1, -1 }}; graph.nodes[2].canvasX = visible.getRight() - 35; graph.outputNode = 2;
    FormulaParameter parameter; parameter.id = "kept_ratio"; parameter.name = "Ratio"; parameter.maximum = 5; parameter.defaultValue = 2;
    graph.parameters.push_back (parameter);
    FormulaNode reference; reference.id = 3; reference.op = FormulaOp::Parameter; reference.parameterId = parameter.id;
    reference.canvasX = 6000; reference.canvasY = -9000; graph.nodes.push_back (reference);
    f.state.setProperty ("remapFormula", graph.encode(), nullptr); f.state.setProperty (parameter.property(), 3.75, nullptr); f.editor.setState (f.state);
    REQUIRE (visible.intersects (f.editor.getNodeBounds (2))); REQUIRE (! visible.contains (f.editor.getNodeBounds (2)));
    REQUIRE (! visible.intersects (f.editor.getNodeBounds (1))); REQUIRE (! visible.intersects (f.editor.getNodeBounds (3)));
    REQUIRE (! visible.contains (f.editor.getOutputDestination()));
    const auto before = f.graph();
    pressToolbar (f.editor, "formula-bring-offscreen");
    const auto after = f.graph(); REQUIRE (withoutLayout (after) == withoutLayout (before));
    REQUIRE (FormulaProgram::compile (after).sameEvaluationAs (FormulaProgram::compile (before)));
    for (const int unchanged : { 0, 2 })
    {
        REQUIRE (formulaExactEqual (after.nodes[(size_t) unchanged].canvasX, before.nodes[(size_t) unchanged].canvasX));
        REQUIRE (formulaExactEqual (after.nodes[(size_t) unchanged].canvasY, before.nodes[(size_t) unchanged].canvasY));
    }
    for (const int brought : { 1, 3 }) REQUIRE (visible.intersects (f.editor.getNodeBounds (brought)));
    REQUIRE (visible.contains (f.editor.getOutputDestination()));
    REQUIRE (std::abs ((double) f.state[parameter.property()] - 3.75) < 1.0e-12);
    REQUIRE (std::abs (FormulaProgram::compile (after).evaluate (200, {}).value - 400) < 1.0e-9);
}

TEST_CASE ("Formula clear offscreen deletes only fully hidden nodes and restores connections with one undo")
{
    using namespace scrr::params; using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto processor = testProcessor(); processor->addModule (1, "FrequencyRemap");
    const auto uid = processor->getModuleChain (1).getChild (0)["uid"].toString();
    Fixture f ({}, "{\"outputX\":6000,\"outputY\":5000,\"zoom\":1,\"panX\":0,\"panY\":0}");
    auto* canvas = findID (f.editor, "formula-canvas"); REQUIRE (canvas != nullptr);
    const auto visible = canvas->getLocalBounds().toFloat();
    auto graph = ratioGraph(); graph.nodes[1].canvasX = -5000; graph.nodes[1].canvasY = 30;
    graph.nodes[2].inputs = {{ 0, 1, -1 }}; graph.nodes[2].canvasX = visible.getRight() - 30; graph.outputNode = 1;
    FormulaParameter parameter; parameter.id = "retained_control"; parameter.name = "Control"; parameter.maximum = 5; parameter.defaultValue = 2;
    graph.parameters.push_back (parameter);
    FormulaNode reference; reference.id = 3; reference.op = FormulaOp::Parameter; reference.parameterId = parameter.id;
    reference.canvasX = 100; reference.canvasY = 5000; graph.nodes.push_back (reference);
    for (const auto placement : { juce::Point<double> (visible.getRight() + 1, 40), juce::Point<double> (300, -5000),
                                 juce::Point<double> (300, -45), juce::Point<double> (540, visible.getBottom() - 25) })
    {
        FormulaNode literal; literal.id = (int) graph.nodes.size(); literal.op = FormulaOp::Constant; literal.value = literal.id + .25;
        literal.canvasX = placement.x; literal.canvasY = placement.y; graph.nodes.push_back (literal);
    }
    processor->setModuleParameter (1, uid, "remapFormula", graph.encode());
    processor->setModuleParameter (1, uid, "formulaView", f.state["formulaView"]);
    processor->setModuleParameter (1, uid, parameter.property(), 3.25); processor->assignMacro (0, 1, uid, parameter.property());
    f.state = processor->getModuleChain (1).getChild (0).createCopy(); f.editor.setState (f.state);
    for (const int partlyVisible : { 2, 6, 7 })
    { REQUIRE (visible.intersects (f.editor.getNodeBounds (partlyVisible))); REQUIRE (! visible.contains (f.editor.getNodeBounds (partlyVisible))); }
    for (const int fullyHidden : { 1, 3, 4, 5 }) REQUIRE (! visible.intersects (f.editor.getNodeBounds (fullyHidden)));
    const auto graphBefore = f.state["remapFormula"].toString(), viewBefore = f.state["formulaView"].toString();
    int graphWrites = 0;
    f.editor.onChange = [&] (const juce::Identifier& key, const juce::var& value)
    {
        if (key == juce::Identifier ("remapFormula")) ++graphWrites;
        processor->setModuleParameter (1, uid, key, value); f.state = processor->getModuleChain (1).getChild (0).createCopy();
    };
    pressToolbar (f.editor, "formula-clear-offscreen");
    const auto cleared = f.graph(); REQUIRE (cleared.nodes.size() == 4); REQUIRE (graphWrites == 1);
    for (const int retained : { 0, 2, 6, 7 })
        REQUIRE (std::any_of (cleared.nodes.begin(), cleared.nodes.end(), [retained] (const auto& node) { return node.id == retained; }));
    const auto multiply = std::find_if (cleared.nodes.begin(), cleared.nodes.end(), [] (const auto& node) { return node.id == 2; });
    REQUIRE (multiply != cleared.nodes.end()); REQUIRE (multiply->inputs[0] == 0); REQUIRE (multiply->inputs[1] == -1);
    REQUIRE (cleared.outputNode == -1); REQUIRE (cleared.parameters.size() == 1);
    REQUIRE (f.state["formulaView"].toString() == viewBefore); REQUIRE (! visible.contains (f.editor.getOutputDestination()));
    REQUIRE (processor->getMacroMappings().size() == 1); REQUIRE (std::abs ((double) f.state[parameter.property()] - 3.25) < 1.0e-12);
    const auto graphAfter = f.state["remapFormula"].toString();
    REQUIRE (processor->undo());
    auto restored = processor->getModuleChain (1).getChild (0); REQUIRE (restored["remapFormula"].toString() == graphBefore);
    REQUIRE (restored["formulaView"].toString() == viewBefore); REQUIRE (processor->getMacroMappings().size() == 1);
    REQUIRE (std::abs ((double) restored[parameter.property()] - 3.25) < 1.0e-12);
    REQUIRE (processor->redo()); REQUIRE (processor->getModuleChain (1).getChild (0)["remapFormula"].toString() == graphAfter);
    processor->setModuleParameter (1, uid, "formulaView", "{\"panX\":50000,\"panY\":50000,\"zoom\":1,\"outputX\":6000,\"outputY\":5000}");
    f.state = processor->getModuleChain (1).getChild (0).createCopy(); f.editor.setState (f.state);
    pressToolbar (f.editor, "formula-clear-offscreen");
    REQUIRE (f.graph().nodes.empty()); REQUIRE (f.graph().outputNode == -1); REQUIRE (! FormulaProgram::compile (f.graph()).valid);
    REQUIRE (f.graph().parameters.size() == 1); REQUIRE (processor->getMacroMappings().size() == 1);
    REQUIRE (processor->undo()); REQUIRE (processor->getModuleChain (1).getChild (0)["remapFormula"].toString() == graphAfter);
}

TEST_CASE ("Formula random button preserves custom controls and atomically restores graph and view with undo")
{
    using namespace scrr::params; using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto processor = testProcessor(); processor->addModule (1, "FrequencyRemap");
    const auto uid = processor->getModuleChain (1).getChild (0)["uid"].toString();
    auto formula = ratioGraph(); formula.nodes[2].inputs = {{ 0, 1, -1 }}; formula.outputNode = 2;
    FormulaParameter parameter; parameter.id = "preserved_random_control"; parameter.name = "Ratio"; parameter.maximum = 5; parameter.defaultValue = 2;
    formula.parameters.push_back (parameter);
    processor->setModuleParameter (1, uid, "remapFormula", formula.encode());
    processor->setModuleParameter (1, uid, "formulaView", "{\"outputX\":680,\"outputY\":90,\"panX\":-222,\"panY\":333,\"zoom\":1.4,\"complexity\":17}");
    processor->setModuleParameter (1, uid, parameter.property(), 3.5); processor->assignMacro (0, 1, uid, parameter.property());
    const auto route = processor->getMacroMappings().front().routeUid;
    Fixture f; f.state = processor->getModuleChain (1).getChild (0).createCopy(); f.editor.setState (f.state);
    int atomicWrites = 0;
    f.editor.onChange = [&] (const juce::Identifier& key, const juce::var& value)
    { processor->setModuleParameter (1, uid, key, value); f.state = processor->getModuleChain (1).getChild (0).createCopy(); };
    f.editor.onGraphAndViewChange = [&] (const juce::String& graph, const juce::String& view)
    { ++atomicWrites; processor->setRemapFormulaState (1, uid, graph, view); f.state = processor->getModuleChain (1).getChild (0).createCopy(); };
    const auto beforeGraph = f.state["remapFormula"].toString(), beforeView = f.state["formulaView"].toString();
    pressToolbar (f.editor, "formula-random");
    const auto afterGraph = f.state["remapFormula"].toString(), afterView = f.state["formulaView"].toString();
    REQUIRE (atomicWrites == 1); REQUIRE (afterGraph != beforeGraph); REQUIRE (afterView != beforeView);
    const auto generated = f.graph(); REQUIRE (FormulaProgram::compile (generated).valid);
    REQUIRE (generated.parameters.size() == 1); REQUIRE (generated.parameters.front().id == parameter.id);
    REQUIRE (std::abs ((double) f.state[parameter.property()] - 3.5) < 1.0e-12);
    REQUIRE (processor->getMacroMappings().size() == 1); REQUIRE (processor->getMacroMappings().front().routeUid == route);
    REQUIRE (processor->undo());
    auto restored = processor->getModuleChain (1).getChild (0);
    REQUIRE (restored["remapFormula"].toString() == beforeGraph); REQUIRE (restored["formulaView"].toString() == beforeView);
    REQUIRE (std::abs ((double) restored[parameter.property()] - 3.5) < 1.0e-12);
    REQUIRE (processor->getMacroMappings().size() == 1); REQUIRE (processor->getMacroMappings().front().routeUid == route);
    REQUIRE (processor->redo()); restored = processor->getModuleChain (1).getChild (0);
    REQUIRE (restored["remapFormula"].toString() == afterGraph); REQUIRE (restored["formulaView"].toString() == afterView);
}

TEST_CASE ("Formula input wire pickup moves or disconnects one destination in a single undo step")
{
    using namespace scrr::params; using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    auto processor = testProcessor(); processor->addModule (1, "FrequencyRemap");
    const auto uid = processor->getModuleChain (1).getChild (0)["uid"].toString();
    auto graph = ratioGraph(); graph.nodes[2].inputs = {{ 0, 1, -1 }};
    FormulaNode destination; destination.id = 3; destination.op = FormulaOp::Multiply; destination.inputs = {{ 1, 1, -1 }};
    destination.canvasX = 600; destination.canvasY = 190; graph.nodes.push_back (destination); graph.outputNode = 3;
    processor->setModuleParameter (1, uid, "remapFormula", graph.encode());
    processor->setModuleParameter (1, uid, "formulaView", "{\"outputX\":780,\"outputY\":30,\"zoom\":0.8,\"panX\":0,\"panY\":0}");
    Fixture f;
    f.state = processor->getModuleChain (1).getChild (0).createCopy(); f.editor.setState (f.state);
    auto* canvas = findID (f.editor, "formula-canvas"); REQUIRE (canvas != nullptr);
    const auto before = f.state["remapFormula"].toString(); int writes = 0;
    f.editor.onChange = [&] (const juce::Identifier& key, const juce::var& value)
    {
        if (key == juce::Identifier ("remapFormula")) ++writes;
        processor->setModuleParameter (1, uid, key, value); f.state = processor->getModuleChain (1).getChild (0).createCopy();
    };
    const auto beforePickup = canvas->createComponentSnapshot (canvas->getLocalBounds());
    click (f.editor, f.editor.getInputPosition (2, 0)); REQUIRE (f.editor.isEditing());
    click (f.editor, { 8, (float) canvas->getHeight() - 8 }); REQUIRE (! f.editor.isEditing());
    canvas->mouseExit (mouse (*canvas, { 0, 0 }, false, juce::ModifierKeys::noModifiers));
    REQUIRE (f.state["remapFormula"].toString() == before); REQUIRE (writes == 0);
    REQUIRE (samePixels (beforePickup, canvas->createComponentSnapshot (canvas->getLocalBounds())));
    const auto moveWire = [&] (juce::Point<float> drop)
    {
        canvas->mouseDown (mouse (*canvas, f.editor.getInputPosition (2, 0)));
        REQUIRE (f.editor.isEditing()); REQUIRE (f.state["remapFormula"].toString() == before);
        canvas->mouseDrag (mouse (*canvas, drop, true)); canvas->mouseUp (mouse (*canvas, drop, true));
        REQUIRE (! f.editor.isEditing());
    };
    moveWire (f.editor.getInputPosition (3, 0));
    auto changed = f.graph(); REQUIRE (changed.nodes.size() == 4); REQUIRE (writes == 1);
    REQUIRE (changed.nodes[2].inputs[0] == -1); REQUIRE (changed.nodes[2].inputs[1] == 1);
    REQUIRE (changed.nodes[3].inputs[0] == 0); REQUIRE (changed.nodes[3].inputs[1] == 1); REQUIRE (changed.outputNode == 3);
    REQUIRE (std::abs (FormulaProgram::compile (changed).evaluate (200, {}).value - 400) < 1.0e-9);
    REQUIRE (processor->undo()); f.state = processor->getModuleChain (1).getChild (0).createCopy();
    REQUIRE (f.state["remapFormula"].toString() == before); f.editor.setState (f.state); writes = 0;
    moveWire ({ 8, (float) canvas->getHeight() - 8 });
    changed = f.graph(); REQUIRE (changed.nodes.size() == 4); REQUIRE (writes == 1);
    REQUIRE (changed.nodes[2].inputs[0] == -1); REQUIRE (changed.nodes[2].inputs[1] == 1);
    REQUIRE (changed.nodes[3].inputs[0] == 1); REQUIRE (changed.nodes[3].inputs[1] == 1); REQUIRE (changed.outputNode == 3);
    const auto disconnected = f.state["remapFormula"].toString();
    REQUIRE (processor->undo()); REQUIRE (processor->getModuleChain (1).getChild (0)["remapFormula"].toString() == before);
    REQUIRE (processor->redo()); REQUIRE (processor->getModuleChain (1).getChild (0)["remapFormula"].toString() == disconnected);
}

TEST_CASE ("Formula home resets pan and zoom without moving nodes or changing graph settings")
{
    using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    Fixture f (ratioGraph(), "{\"outputX\":-1400,\"outputY\":2500,\"zoom\":1.7,\"panX\":-940,\"panY\":730,\"xMin\":30,\"xMax\":9000,\"symbolic\":true,\"chinese\":true,\"probeX\":431}");
    const auto graphBefore = f.state["remapFormula"].toString();
    int graphWrites = 0;
    f.editor.onChange = [&] (const juce::Identifier& key, const juce::var& value)
    { if (key == juce::Identifier ("remapFormula")) ++graphWrites; f.state.setProperty (key, value, nullptr); };
    pressToolbar (f.editor, "formula-home");
    const auto view = juce::JSON::parse (f.state["formulaView"].toString());
    REQUIRE (std::abs ((double) view["panX"]) < 1.0e-12); REQUIRE (std::abs ((double) view["panY"]) < 1.0e-12);
    REQUIRE (std::abs (f.editor.getCanvasZoom() - 1) < 1.0e-12); REQUIRE (std::abs ((double) view["zoom"] - 1) < 1.0e-12);
    REQUIRE (f.state["remapFormula"].toString() == graphBefore); REQUIRE (graphWrites == 0);
    REQUIRE (std::abs ((double) view["outputX"] + 1400) < 1.0e-12); REQUIRE (std::abs ((double) view["outputY"] - 2500) < 1.0e-12);
    REQUIRE (std::abs ((double) view["xMin"] - 30) < 1.0e-12); REQUIRE (std::abs ((double) view["xMax"] - 9000) < 1.0e-12);
    REQUIRE ((bool) view["symbolic"]); REQUIRE ((bool) view["chinese"]); REQUIRE (std::abs ((double) view["probeX"] - 431) < 1.0e-12);
}

TEST_CASE ("Formula blank canvas drag pans beyond the old bounds in both directions without editing the graph")
{
    using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    juce::Component scaledParent;
    Fixture f;
    f.editor.removeFromDesktop(); scaledParent.addAndMakeVisible (f.editor);
    f.editor.setTransform (juce::AffineTransform::scale (1.5f));
    scaledParent.setSize (1350, juce::roundToInt ((float) f.editor.getHeight() * 1.5f)); foreground (scaledParent);
    auto* canvas = findID (f.editor, "formula-canvas"); REQUIRE (canvas != nullptr);
    const auto before = f.state["remapFormula"].toString(); const double zoom = f.editor.getCanvasZoom();
    const juce::Point<float> start { 8, (float) canvas->getHeight() - 8 };
    const auto panBy = [&] (juce::Point<float> delta)
    {
        canvas->mouseDown (mouse (*canvas, start)); REQUIRE (f.editor.isEditing());
        canvas->mouseDrag (mouse (*canvas, start + delta, true)); canvas->mouseUp (mouse (*canvas, start + delta, true));
        REQUIRE (! f.editor.isEditing()); REQUIRE (f.state["remapFormula"].toString() == before);
        REQUIRE (std::abs (f.editor.getCanvasZoom() - zoom) < 1.0e-12);
        return juce::JSON::parse (f.state["formulaView"].toString());
    };
    auto view = panBy ({ 16000, 12000 });
    REQUIRE (std::abs ((double) view["panX"] + 16000 / zoom) < 1.0e-9); REQUIRE (std::abs ((double) view["panY"] + 12000 / zoom) < 1.0e-9);
    view = panBy ({ -32000, -24000 });
    REQUIRE (std::abs ((double) view["panX"] - 16000 / zoom) < 1.0e-9); REQUIRE (std::abs ((double) view["panY"] - 12000 / zoom) < 1.0e-9);
    pressToolbar (f.editor, "formula-home"); REQUIRE (f.state["remapFormula"].toString() == before);
    REQUIRE (canvas->getLocalBounds().toFloat().intersects (f.editor.getNodeBounds (0)));
}

TEST_CASE ("Formula manual and pointer-follow probes change numerical previews without changing audio state")
{
    using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    Fixture f;
    pressToolbar (f.editor, "formula-probe-menu");
    auto* settings = dynamic_cast<juce::Button*> (findID (f.editor, "formula-details")); REQUIRE (settings != nullptr);
    settings->setToggleState (true, juce::dontSendNotification); settings->onClick();
    auto* mode = dynamic_cast<juce::ComboBox*> (findID (f.editor, "formula-probe-mode")); REQUIRE (mode != nullptr);
    mode->setSelectedId (1, juce::sendNotificationSync); editText (f.editor, "formula-probe-x", "400");
    auto view = juce::JSON::parse (f.state["formulaView"].toString()); REQUIRE (! (bool) view["followMouse"]); REQUIRE (std::abs ((double) view["probeX"] - 400) < .001);
    auto* canvas = findID (f.editor, "formula-canvas"); auto* preview = findID (f.editor, "formula-preview"); REQUIRE (canvas != nullptr); REQUIRE (preview != nullptr);
    const auto graphBefore = f.state["remapFormula"].toString();
    const auto manual = canvas->createComponentSnapshot (canvas->getLocalBounds());
    mode->setSelectedId (2, juce::sendNotificationSync);
    preview->mouseMove (mouse (*preview, { (float) preview->getWidth() * .8f, (float) preview->getHeight() * .5f }, false, juce::ModifierKeys::noModifiers));
    f.editor.refreshPreview();
    const auto following = canvas->createComponentSnapshot (canvas->getLocalBounds());
    REQUIRE (! samePixels (manual, following)); REQUIRE (f.state["remapFormula"].toString() == graphBefore);
    view = juce::JSON::parse (f.state["formulaView"].toString()); REQUIRE ((bool) view["followMouse"]);
    REQUIRE (std::abs ((double) view["probeX"] - 400) < .001);
    mode->setSelectedId (1, juce::sendNotificationSync); f.editor.refreshPreview();
    REQUIRE (samePixels (manual, canvas->createComponentSnapshot (canvas->getLocalBounds())));
}

TEST_CASE ("Formula node language menu contains correctly decoded Chinese text")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    formulaUiTests::Fixture f;
    auto* language = dynamic_cast<juce::ComboBox*> (findID (f.editor, "formula-node-language")); REQUIRE (language != nullptr);
    REQUIRE (language->getItemText (1) == juce::String::fromUTF8 ("简体中文"));
}

TEST_CASE ("Formula same module restore cancels an old wire before accepting another click")
{
    using namespace scrr::params; using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    Fixture f (ratioGraph());
    click (f.editor, f.editor.getOutputPosition (2)); REQUIRE (f.editor.isEditing());
    RemapFormula restored;
    FormulaNode constant; constant.id = 9; constant.op = FormulaOp::Constant; constant.value = 800; constant.canvasX = 330; constant.canvasY = 80;
    restored.nodes.push_back (constant); restored.outputNode = 9;
    f.state.setProperty ("remapFormula", restored.encode(), nullptr); f.editor.setState (f.state);
    REQUIRE (f.editor.getModuleUid() == "formula-ui"); REQUIRE (! f.editor.isEditing());
    const auto expected = f.state["remapFormula"].toString();
    click (f.editor, f.editor.getOutputDestination());
    REQUIRE (f.state["remapFormula"].toString() == expected); REQUIRE (f.graph().outputNode == 9);
    const auto program = FormulaProgram::compile (f.graph()); REQUIRE (program.valid);
    std::array<double, RemapFormula::maxParameters> values {};
    REQUIRE (std::abs (program.evaluate (200, values).value - 800) < 1.0e-9);
}

TEST_CASE ("Formula module switches and replaced graphs reject stale context menu actions")
{
    using namespace scrr::params; using namespace formulaUiTests;
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;
    Fixture f;
    click (f.editor, f.editor.getOutputPosition (0)); REQUIRE (f.editor.isEditing());
    f.state.setProperty ("uid", "next-formula", nullptr); f.editor.setState (f.state);
    REQUIRE (f.editor.getModuleUid() == "next-formula"); REQUIRE (! f.editor.isEditing());
    for (bool replaceUid : { false, true })
    {
        click (f.editor, { 500, 330 }, true); juce::Component::SafePointer<juce::Component> popup (activeMenu());
        RemapFormula replacement; replacement.nodes.front().canvasX = 73;
        f.state.setProperty ("remapFormula", replacement.encode(), nullptr);
        if (replaceUid) f.state.setProperty ("uid", "third-formula", nullptr);
        f.editor.setState (f.state); const auto expected = f.state["remapFormula"].toString();
        if (popup != nullptr) popup->exitModalState ((int) FormulaOp::Constant + 1);
        pump (60); REQUIRE (f.state["remapFormula"].toString() == expected); REQUIRE (f.graph().nodes.size() == 1);
    }
}

inline int renderRemapFormulaUi (const juce::File& directory)
{
    using namespace scrr::params;
    if (directory.createDirectory().failed() || juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return 1;
    RemapFormula formula; formula.nodes.front().canvasX = 24; formula.nodes.front().canvasY = 25;
    FormulaParameter parameter; parameter.id = "ratio"; parameter.name = "Ratio"; parameter.minimum = .25; parameter.maximum = 4; parameter.defaultValue = 1.5; parameter.step = .01; parameter.logarithmic = true;
    formula.parameters.push_back (parameter);
    FormulaNode parameterNode; parameterNode.id = 1; parameterNode.op = FormulaOp::Parameter; parameterNode.parameterId = parameter.id; parameterNode.canvasX = 24; parameterNode.canvasY = 180;
    FormulaNode multiply; multiply.id = 2; multiply.op = FormulaOp::Multiply; multiply.inputs = {{ 0, 1, -1 }}; multiply.canvasX = 300; multiply.canvasY = 60;
    formula.nodes.push_back (parameterNode); formula.nodes.push_back (multiply); formula.outputNode = 2;
    juce::ValueTree state ("module"); state.setProperty ("uid", "formula-render", nullptr); state.setProperty ("remapFormula", formula.encode(), nullptr);
    state.setProperty ("formulaView", "{\"outputX\":580,\"outputY\":60,\"zoom\":0.65}", nullptr); state.setProperty (parameter.property(), 1.5, nullptr);
    scrr::gui::InstrumentLookAndFeel look;
    scrr::gui::RemapFormulaEditor editor; editor.setLookAndFeel (&look); editor.setState (state); editor.setSize (520, editor.getPreferredHeight());
    editor.layoutChanged = [&] { editor.setSize (520, editor.getPreferredHeight()); };
    editor.onChange = [&] (const juce::Identifier& key, const juce::var& value) { state.setProperty (key, value, nullptr); };
    formulaUiTests::foreground (editor);
    const auto writeImage = [] (const juce::Image& image, const juce::File& file)
    {
        juce::MemoryOutputStream bytes;
        return juce::PNGImageFormat().writeImageToStream (image, bytes)
            && file.replaceWithData (bytes.getData(), bytes.getDataSize());
    };
    auto save = [&] (const char* name)
    {
        const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
        return writeImage (image, directory.getChildFile (name));
    };
    bool success = save ("formula-canvas.png");
    auto* canvas = findID (editor, "formula-canvas"); REQUIRE (canvas != nullptr);
    const auto parameterBounds = editor.getNodeBounds (1);
    canvas->mouseDoubleClick (mouse (*canvas, { parameterBounds.getCentreX(), parameterBounds.getY() + 12 * (float) editor.getCanvasZoom() }));
    pump (30); success = save ("formula-details.png") && success;
    formulaUiTests::pressToolbar (editor, "formula-probe-menu");
    if (auto* settings = dynamic_cast<juce::Button*> (findID (editor, "formula-details")))
    { settings->setToggleState (true, juce::dontSendNotification); settings->onClick(); }
    success = save ("formula-settings.png") && success;
    auto power = formula; power.nodes.back().op = FormulaOp::Power;
    auto numericPower = state.createCopy(); numericPower.setProperty ("uid", "formula-render-power", nullptr); numericPower.setProperty ("remapFormula", power.encode(), nullptr);
    numericPower.setProperty ("formulaView", "{\"outputX\":580,\"outputY\":60,\"zoom\":0.65,\"panX\":0,\"panY\":0}", nullptr);
    editor.setState (numericPower); success = save ("formula-numeric-power.png") && success;
    auto symbolicState = state.createCopy(); symbolicState.setProperty ("uid", "formula-render-symbolic", nullptr); symbolicState.setProperty ("remapFormula", power.encode(), nullptr);
    symbolicState.setProperty ("formulaView", "{\"outputX\":580,\"outputY\":60,\"zoom\":0.65,\"symbolic\":true}", nullptr);
    editor.setState (symbolicState); success = save ("formula-symbolic-power.png") && success;
    symbolicState.setProperty ("formulaView", "{\"outputX\":580,\"outputY\":60,\"zoom\":0.65,\"symbolic\":true,\"chinese\":true}", nullptr);
    editor.setState (symbolicState); success = save ("formula-symbolic-zh.png") && success;
    formula.nodes.back().inputs[1] = -1; state.setProperty ("remapFormula", formula.encode(), nullptr);
    state.setProperty ("formulaView", "{\"outputX\":580,\"outputY\":60,\"zoom\":0.65,\"panX\":0,\"panY\":0}", nullptr); editor.setState (state);
    success = save ("formula-error.png") && success; editor.setVisible (false); editor.setLookAndFeel (nullptr);
    // Exercise the real inspector viewport as well as the isolated component.
    formula.nodes.back().inputs[1] = 1;
    auto processor = testProcessor(); processor->addModule (1, "FrequencyRemap");
    const auto uid = processor->getModuleChain (1).getChild (0)["uid"].toString();
    processor->setModuleParameter (1, uid, "remapFormula", formula.encode());
    processor->setModuleParameter (1, uid, "formulaView", "{\"outputX\":580,\"outputY\":60,\"zoom\":0.65}");
    processor->setModuleParameter (1, uid, parameter.property(), 1.5);
    processor->setModuleParameter (1, uid, "remapMode", 1); processor->prepareToPlay (48000, 128);
    auto fullEditor = std::unique_ptr<juce::AudioProcessorEditor> (processor->createEditor()); fullEditor->addToDesktop (0); fullEditor->setVisible (true);
    for (const auto size : { juce::Point<int> (1040, 780), juce::Point<int> (1180, 980) })
    {
        fullEditor->setSize (size.x, size.y);
        success = waitForUI ([&] { auto* card = findID (*fullEditor, "effect-card"); return card != nullptr && card->getAlpha() >= .999f; }) && success;
        if (auto* guide = findID (*fullEditor, "quick-guide")) guide->setVisible (false);
        const auto screenshot = fullEditor->createComponentSnapshot (fullEditor->getLocalBounds(), true, 1.0f);
        success = writeImage (screenshot, directory.getChildFile ("formula-full-" + juce::String (size.x) + "x" + juce::String (size.y) + ".png")) && success;
    }
    return success ? 0 : 1;
}
