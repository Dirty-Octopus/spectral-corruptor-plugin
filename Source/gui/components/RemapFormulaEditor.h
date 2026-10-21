// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "../Theme.h"
#include "../AssignableSlider.h"
#include "../../parameters/RemapFormula.h"
#include <functional>
#include <cstdlib>
#include <limits>

namespace scrr::gui {

/** A detached native graph editor. Formula state is executable; formulaView is
    presentation only. Callbacks always refer to the currently selected UID. */
class RemapFormulaEditor : public juce::Component
{
    using Formula = scrr::params::RemapFormula;
    using Node = scrr::params::FormulaNode;
    using Parameter = scrr::params::FormulaParameter;
    using Op = scrr::params::FormulaOp;
    using Unit = scrr::params::FormulaUnit;
    using Program = scrr::params::FormulaProgram;

    struct Entry : juce::Component
    {
        Entry (juce::String title, juce::String id) : caption (std::move (title))
        {
            addAndMakeVisible (editor); editor.setComponentID (std::move (id));
            editor.onReturnKey = [this] { commit(); editor.giveAwayKeyboardFocus(); };
            editor.onFocusLost = [this] { commit(); };
        }
        void commit() { if (editor.getText() != lastText && onCommit) { lastText = editor.getText(); onCommit(); } }
        void set (const juce::String& text) { lastText = text; editor.setText (text, false); }
        void set (double value)
        {
            if ((std::abs (value) > 0 && std::abs (value) < 1.0e-6) || std::abs (value) >= 1.0e9) set (juce::String (value, 8, true));
            else set (juce::String (value, 8).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."));
        }
        double number() const
        {
            const auto ownedText = editor.getText().trim(); const auto text = ownedText.toUTF8();
            char* end = nullptr; const double value = std::strtod (text.getAddress(), &end);
            return end != text.getAddress() && end != nullptr && *end == 0 ? value : std::numeric_limits<double>::quiet_NaN();
        }
        void paint (juce::Graphics& g) override { Theme::text (g, caption, { 0, 0, getWidth(), 16 }, 10, Theme::dim(), true); }
        void resized() override { editor.setBounds (0, 17, getWidth(), 23); }
        juce::String caption, lastText; juce::TextEditor editor; std::function<void()> onCommit;
    };
    struct ParameterRow : juce::Component
    {
        ParameterRow (RemapFormulaEditor& editor, const Parameter& parameter) : owner (editor), id (parameter.id)
        {
            setComponentID ("formula-row-" + id); addAndMakeVisible (label); addAndMakeVisible (slider); addAndMakeVisible (details);
            slider.setComponentID (parameter.property().toString()); details.setButtonText ("DETAILS"); details.setComponentID ("formula-param-details-" + id);
            details.onClick = [this] { owner.selectParameter (id); };
            slider.mappingMenu = [this] { if (owner.mappingMenu) owner.mappingMenu (slider, juce::Identifier (slider.getComponentID())); };
            slider.onDragStart = [this] { owner.sliderGesture = true; };
            slider.onDragEnd = [this] { owner.sliderGesture = false; };
            slider.onValueChange = [this]
            {
                owner.submitProperty (juce::Identifier (slider.getComponentID()), slider.getValue()); owner.refreshPreview();
            };
            configure (parameter);
        }
        void configure (const Parameter& parameter)
        {
            label.setText (parameter.name, juce::dontSendNotification); label.setFont (Theme::font (12, true));
            if (std::isfinite (parameter.minimum) && std::isfinite (parameter.maximum) && parameter.maximum > parameter.minimum)
            {
                const double interval = std::isfinite (parameter.step) ? juce::jlimit (0.0, parameter.maximum - parameter.minimum, parameter.step) : 0;
                const double reset = std::isfinite (parameter.defaultValue) ? juce::jlimit (parameter.minimum, parameter.maximum, parameter.defaultValue) : parameter.minimum;
                setupSlider (slider, parameter.minimum, parameter.maximum, interval, reset);
                slider.setSkewFactor (1.0);
                if (parameter.logarithmic && parameter.minimum > 0)
                {
                    const double midpoint = std::exp ((std::log (parameter.minimum) + std::log (parameter.maximum)) * .5);
                    if (std::isfinite (midpoint) && midpoint > parameter.minimum && midpoint < parameter.maximum)
                        slider.setSkewFactorFromMidPoint (midpoint);
                }
            }
            else setupSlider (slider, 0, 1, 0, 0);
            const double savedValue = (double) owner.state.getProperty (parameter.property(), parameter.defaultValue);
            slider.setValue (std::isfinite (savedValue) ? savedValue : slider.getMinimum(), juce::dontSendNotification);
        }
        int preferredHeight() const { return 46 + slider.modulationHeight(); }
        void resized() override
        {
            auto bounds = getLocalBounds(); auto top = bounds.removeFromTop (18); details.setBounds (top.removeFromRight (66)); label.setBounds (top);
            slider.setBounds (bounds);
        }
        RemapFormulaEditor& owner; juce::String id; juce::Label label; AssignableSlider slider; juce::TextButton details;
    };
    struct View
    {
        double xMin { 20 }, xMax { 20000 }, yMin { 20 }, yMax { 20000 };
        bool logX { true }, logY { true }, grid { true }; int samples { 256 };
        double outputX { 350 }, outputY { 80 };
    };
    class Canvas : public juce::Component
    {
    public:
        explicit Canvas (RemapFormulaEditor& editor) : owner (editor) { setWantsKeyboardFocus (true); setComponentID ("formula-canvas"); setSize (1800, 1200); }
        bool editing() const { return dragging || panning || pendingSource >= 0; }
        juce::Rectangle<float> boundsFor (const Node& node) const
        { return { (float) node.canvasX, (float) node.canvasY, 142, (float) juce::jmax (58, 34 + scrr::params::formulaOpArity (node.op) * 20) }; }
        juce::Rectangle<float> outputBounds() const { return { (float) owner.view.outputX, (float) owner.view.outputY, 110, 58 }; }
        juce::Point<float> outputFor (const Node& node) const { const auto b = boundsFor (node); return { b.getRight(), b.getY() + 20 }; }
        juce::Point<float> inputFor (const Node& node, int pin) const { const auto b = boundsFor (node); return { b.getX(), b.getY() + 40 + (float) pin * 20 }; }
        juce::Point<float> outputInput() const { const auto b = outputBounds(); return { b.getX(), b.getY() + 37 }; }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::field()); g.setColour (Theme::line().withAlpha (.4f));
            const auto clip = g.getClipBounds();
            for (int x = clip.getX() / 20 * 20; x < clip.getRight(); x += 20)
                for (int y = clip.getY() / 20 * 20; y < clip.getBottom(); y += 20) g.fillRect (x, y, 1, 1);
            for (const auto& node : owner.graph.nodes)
                for (int pin = 0; pin < scrr::params::formulaOpArity (node.op); ++pin)
                    if (const auto* source = owner.findNode (node.inputs[(size_t) pin])) wire (g, outputFor (*source), inputFor (node, pin), Theme::blue());
            if (const auto* node = owner.findNode (owner.graph.outputNode)) wire (g, outputFor (*node), outputInput(), Theme::yellow());
            if (const auto* node = owner.findNode (pendingSource)) wire (g, outputFor (*node), cursor, Theme::yellow());
            for (const auto& node : owner.graph.nodes)
            {
                const auto b = boundsFor (node); g.setColour (Theme::panel()); g.fillRect (b);
                g.setColour (node.id == owner.selectedNode ? Theme::yellow() : Theme::line()); g.drawRect (b, node.id == owner.selectedNode ? 2.0f : 1.0f);
                auto title = juce::String (scrr::params::formulaOpName (node.op));
                if (node.op == Op::Parameter) if (const auto* p = owner.findParameter (node.parameterId)) title = p->name;
                Theme::text (g, title, b.toNearestInt().reduced (10, 0).withHeight (32), 12, Theme::white(), true);
                if (node.op == Op::Constant) Theme::text (g, juce::String (node.value, 4), b.toNearestInt().withTrimmedTop (30).reduced (10, 0), 11, Theme::dim());
                if (node.op == Op::Input) Theme::text (g, owner.graph.unit == Unit::Hertz ? "INPUT Hz" : "INPUT 0..1 LOG", b.toNearestInt().withTrimmedTop (30).reduced (10, 0), 10, Theme::dim());
                socket (g, outputFor (node), Theme::yellow());
                for (int pin = 0; pin < scrr::params::formulaOpArity (node.op); ++pin)
                {
                    socket (g, inputFor (node, pin), Theme::blue());
                    const auto pinLabel = node.op == Op::Clamp ? (pin == 0 ? "value" : pin == 1 ? "min" : "max") : (pin == 0 ? "a" : pin == 1 ? "b" : "c");
                    Theme::text (g, pinLabel, { (int) b.getX() + 12, (int) inputFor (node, pin).y - 9, 110, 18 }, 10, Theme::dim());
                }
            }
            const auto b = outputBounds(); g.setColour (Theme::yellow()); g.drawRect (b, 2);
            Theme::text (g, "y / OUTPUT", b.toNearestInt().reduced (10, 0).withHeight (30), 12, Theme::yellow(), true);
            Theme::text (g, "connect here", b.toNearestInt().withTrimmedTop (30).reduced (10, 0), 10, Theme::dim()); socket (g, outputInput(), Theme::yellow());
        }
        void mouseDown (const juce::MouseEvent& event) override
        {
            grabKeyboardFocus(); cursor = event.position;
            if (event.mods.isPopupMenu())
            {
                if (outputInput().getDistanceFrom (cursor) < 12) { owner.graph.outputNode = -1; owner.submitGraph(); return; }
                for (auto& node : owner.graph.nodes) for (int pin = 0; pin < scrr::params::formulaOpArity (node.op); ++pin)
                    if (inputFor (node, pin).getDistanceFrom (cursor) < 12) { node.inputs[(size_t) pin] = -1; owner.submitGraph(); return; }
                pendingSource = -1; repaint(); return;
            }
            if (finishConnection (cursor)) return;
            for (auto& node : owner.graph.nodes) if (outputFor (node).getDistanceFrom (cursor) < 12)
            { pendingSource = node.id; owner.selectNode (node.id); repaint(); return; }
            for (auto it = owner.graph.nodes.rbegin(); it != owner.graph.nodes.rend(); ++it) if (boundsFor (*it).contains (cursor))
            { owner.selectNode (it->id); dragging = true; draggedOutput = false; start = cursor; startNode = { (float) it->canvasX, (float) it->canvasY }; return; }
            if (outputBounds().contains (cursor))
            { dragging = draggedOutput = true; start = cursor; startNode = { (float) owner.view.outputX, (float) owner.view.outputY }; return; }
            pendingSource = -1; panning = true; startScreen = event.getScreenPosition(); startScroll = owner.viewport.getViewPosition(); repaint();
        }
        void mouseDrag (const juce::MouseEvent& event) override
        {
            cursor = event.position;
            if (panning) { const auto delta = event.getScreenPosition() - startScreen; owner.viewport.setViewPosition (startScroll - delta); return; }
            if (dragging)
            {
                const auto position = startNode + event.position - start;
                const double x = juce::jlimit (12.0, 1620.0, (double) position.x), y = juce::jlimit (12.0, 1080.0, (double) position.y);
                if (draggedOutput) { owner.view.outputX = x; owner.view.outputY = y; }
                else if (auto* node = owner.findNode (owner.selectedNode)) { node->canvasX = x; node->canvasY = y; }
            }
            repaint();
        }
        void mouseMove (const juce::MouseEvent& event) override { if (pendingSource >= 0) { cursor = event.position; repaint(); } }
        void mouseUp (const juce::MouseEvent& event) override
        {
            if (pendingSource >= 0 && event.mouseWasDraggedSinceMouseDown()) finishConnection (event.position);
            if (dragging) { dragging = false; if (draggedOutput) owner.submitView(); else owner.submitGraph(); }
            panning = false;
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::escapeKey) { pendingSource = -1; repaint(); return true; }
            if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { owner.deleteSelectedNode(); return true; }
            return false;
        }
        void cancel() { dragging = panning = false; pendingSource = -1; }
    private:
        bool finishConnection (juce::Point<float> point)
        {
            if (pendingSource < 0) return false;
            if (outputInput().getDistanceFrom (point) < 14) { owner.graph.outputNode = pendingSource; pendingSource = -1; owner.submitGraph(); return true; }
            for (auto& node : owner.graph.nodes) for (int pin = 0; pin < scrr::params::formulaOpArity (node.op); ++pin)
                if (inputFor (node, pin).getDistanceFrom (point) < 14)
                { node.inputs[(size_t) pin] = pendingSource; pendingSource = -1; owner.submitGraph(); return true; }
            return false;
        }
        static void socket (juce::Graphics& g, juce::Point<float> p, juce::Colour colour)
        { g.setColour (Theme::field()); g.fillEllipse (p.x - 5, p.y - 5, 10, 10); g.setColour (colour); g.drawEllipse (p.x - 5, p.y - 5, 10, 10, 2); }
        static void wire (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to, juce::Colour colour)
        {
            juce::Path path; path.startNewSubPath (from);
            const auto bend = juce::jmax (to.x >= from.x ? 8.0f : 40.0f, std::abs (to.x - from.x) * .5f);
            path.cubicTo (from.x + bend, from.y, to.x - bend, to.y, to.x, to.y); g.setColour (colour); g.strokePath (path, juce::PathStrokeType (2));
        }
        RemapFormulaEditor& owner; int pendingSource { -1 }; bool dragging {}, draggedOutput {}, panning {};
        juce::Point<float> cursor, start, startNode; juce::Point<int> startScreen, startScroll;
    };
    class Preview : public juce::Component
    {
    public:
        explicit Preview (RemapFormulaEditor& editor) : owner (editor) { setComponentID ("formula-preview"); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::field()); const auto plot = getLocalBounds().toFloat().reduced (12, 22);
            Theme::text (g, "OUTPUT y  /  " + juce::String (owner.graph.unit == Unit::Hertz ? "Hz" : "LOG-FREQUENCY 0..1"), { 12, 1, getWidth() - 24, 18 }, 10, Theme::dim(), true);
            Theme::text (g, "x " + juce::String (owner.view.xMin, 2), { 12, getHeight() - 20, getWidth() / 2 - 12, 18 }, 10, Theme::dim());
            Theme::text (g, juce::String (owner.view.xMax, 2) + " x", { getWidth() / 2, getHeight() - 20, getWidth() / 2 - 12, 18 }, 10, Theme::dim(), false, juce::Justification::centredRight);
            g.setColour (Theme::line());
            if (owner.view.grid) for (int i = 0; i <= 4; ++i)
            { const float t = (float) i / 4; g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * t), plot.getY(), plot.getBottom()); g.drawHorizontalLine ((int) (plot.getY() + plot.getHeight() * t), plot.getX(), plot.getRight()); }
            juce::Graphics::ScopedSaveState saved (g); g.reduceClipRegion (plot.toNearestInt());
            if (owner.program.valid && owner.validView())
            {
                const auto values = owner.parameterValues(); juce::Path path; bool drawing = false;
                for (int i = 0; i <= owner.view.samples; ++i)
                {
                    const double t = (double) i / owner.view.samples;
                    const double x = fromAxis (t, owner.view.xMin, owner.view.xMax, owner.view.logX);
                    const auto result = owner.program.evaluate (x, values);
                    if (! result.valid || (owner.view.logY && result.value <= 0)) { drawing = false; continue; }
                    const double y = toAxis (result.value, owner.view.yMin, owner.view.yMax, owner.view.logY);
                    const auto point = juce::Point<float> (plot.getX() + (float) t * plot.getWidth(), plot.getBottom() - (float) juce::jlimit (-100.0, 100.0, y) * plot.getHeight());
                    if (drawing) path.lineTo (point); else { path.startNewSubPath (point); drawing = true; }
                }
                g.setColour (Theme::yellow()); g.strokePath (path, juce::PathStrokeType (2));
            }
        }
        static double fromAxis (double t, double low, double high, bool logarithmic) { return logarithmic ? low * std::pow (high / low, t) : low + t * (high - low); }
        static double toAxis (double x, double low, double high, bool logarithmic) { return logarithmic ? std::log (x / low) / std::log (high / low) : (x - low) / (high - low); }
    private:
        RemapFormulaEditor& owner;
    };
public:
    std::function<void(const juce::Identifier&, const juce::var&)> onChange;
    std::function<void(juce::Component&, const juce::Identifier&)> mappingMenu;
    std::function<void()> onParametersChanged, layoutChanged;
    std::function<double(const juce::Identifier&, double)> effectiveValue;

    RemapFormulaEditor() : canvas (*this), preview (*this)
    {
        setComponentID ("remap-formula-editor");
        for (auto* button : { &addNode, &addParameter, &details, &setOutput, &removeNode, &removeParameter }) addAndMakeVisible (*button);
        addNode.setButtonText ("ADD NODE"); addNode.setComponentID ("formula-add-node"); addNode.onClick = [this] { showNodeMenu(); };
        addParameter.setButtonText ("ADD PARAM"); addParameter.setComponentID ("formula-add-param"); addParameter.onClick = [this] { createParameter(); };
        details.setButtonText ("DETAILS"); details.setComponentID ("formula-details"); details.setClickingTogglesState (true);
        details.onClick = [this] { showDetails = details.getToggleState(); updateDetails(); notifyLayout(); };
        setOutput.setButtonText ("SET OUTPUT"); setOutput.setComponentID ("formula-set-output");
        setOutput.onClick = [this] { if (findNode (selectedNode)) { graph.outputNode = selectedNode; submitGraph(); } };
        removeNode.setButtonText ("DELETE NODE"); removeNode.setComponentID ("formula-delete-node"); removeNode.onClick = [this] { deleteSelectedNode(); };
        removeParameter.setButtonText ("DELETE PARAM"); removeParameter.setComponentID ("formula-delete-param"); removeParameter.onClick = [this] { deleteSelectedParameter(); };
        units.addItem ("Hz", 1); units.addItem ("NORMALIZED (LOG)", 2); units.setComponentID ("formula-units"); addAndMakeVisible (units);
        units.setTooltip ("Hz uses frequency directly. Normalized uses the same logarithmic 0..1 frequency coordinates as Edit. Changing units keeps node values.");
        units.onChange = [this] { graph.unit = units.getSelectedId() == 2 ? Unit::Normalized : Unit::Hertz; resetViewRange(); submitGraph(); submitView(); updateDetails(); };
        viewport.setViewedComponent (&canvas, false); viewport.setScrollBarsShown (true, true); viewport.setScrollBarThickness (10); viewport.setComponentID ("formula-viewport"); addAndMakeVisible (viewport);
        addAndMakeVisible (preview); addAndMakeVisible (status); status.setComponentID ("formula-status"); status.setFont (Theme::font (11));
        for (auto* field : { &xMin, &xMax, &yMin, &yMax, &name, &minimum, &maximum, &defaultValue, &step, &constant }) addChildComponent (*field);
        for (auto* field : { &xMin, &xMax, &yMin, &yMax }) field->onCommit = [this] { commitViewFields(); };
        for (auto* field : { &name, &minimum, &maximum, &defaultValue, &step }) field->onCommit = [this] { commitParameterFields(); };
        constant.onCommit = [this]
        {
            const double value = constant.number();
            if (! std::isfinite (value)) { showError ("Constant must be a finite number."); return; }
            if (auto* node = findNode (selectedNode)) { node->value = value; submitGraph(); }
        };
        logX.setButtonText ("X LOG"); logY.setButtonText ("Y LOG"); grid.setButtonText ("GRID"); parameterLog.setButtonText ("LOG PARAM");
        for (auto* toggle : { &logX, &logY, &grid, &parameterLog }) addChildComponent (*toggle);
        logX.setComponentID ("formula-view-log-x"); logY.setComponentID ("formula-view-log-y"); grid.setComponentID ("formula-view-grid"); parameterLog.setComponentID ("formula-param-log");
        logX.onClick = logY.onClick = grid.onClick = [this] { commitViewFields(); };
        parameterLog.onClick = [this] { commitParameterFields(); };
        for (const int count : { 64, 128, 256, 512, 1024 }) precision.addItem (juce::String (count) + " PLOT SAMPLES", count);
        precision.setComponentID ("formula-view-samples"); addChildComponent (precision); precision.onChange = [this] { commitViewFields(); };
        program = Program::compile (graph); updateDetails();
    }
    ~RemapFormulaEditor() override { viewport.setViewedComponent (nullptr, false); }
    void setState (const juce::ValueTree& module)
    {
        const auto nextUid = module.getProperty ("uid").toString();
        if (nextUid == getModuleUid() && isEditing()) return;
        const int previousHeight = getPreferredHeight();
        const bool changedUid = nextUid != getModuleUid();
        const auto encoded = module.getProperty ("remapFormula").toString();
        const auto encodedView = module.getProperty ("formulaView").toString();
        const bool changedGraph = changedUid || encoded != state.getProperty ("remapFormula").toString();
        const bool changedView = changedUid || encodedView != state.getProperty ("formulaView").toString();
        state = module.createCopy();
        if (changedGraph)
        {
            editError.clear();
            graph = Formula::decode (encoded, &decodeError); program = Program::compile (graph);
            for (auto& node : graph.nodes) { node.canvasX = juce::jlimit (12.0, 1620.0, node.canvasX); node.canvasY = juce::jlimit (12.0, 1080.0, node.canvasY); }
            if (changedUid || ! findNode (selectedNode)) selectedNode = graph.nodes.empty() ? -1 : graph.nodes.front().id;
            if (changedUid) { selectedParameter.clear(); canvas.cancel(); viewport.setViewPosition (0, 0); }
            rebuildParameters(); units.setSelectedId (graph.unit == Unit::Hertz ? 1 : 2, juce::dontSendNotification);
        }
        if (changedView) loadView (encodedView);
        for (auto& row : parameterRows) if (const auto* p = findParameter (row->id))
        {
            const double value = (double) state.getProperty (p->property(), p->defaultValue);
            row->slider.setValue (std::isfinite (value) ? value : row->slider.getMinimum(), juce::dontSendNotification);
        }
        if (changedGraph || changedView) { updateDetails(); canvas.repaint(); resized(); }
        refreshPreview();
        if (previousHeight != getPreferredHeight() && layoutChanged) layoutChanged();
    }
    juce::String getModuleUid() const { return state.getProperty ("uid").toString(); }
    void setFrequencyLimit (double value)
    {
        if (! std::isfinite (value) || value <= 20 || std::abs (frequencyLimit - value) < .001) return;
        frequencyLimit = value;
        if (state.getProperty ("formulaView").toString().isEmpty()) { resetViewRange(); updateDetails(); }
        refreshPreview();
    }
    bool isEditing() const
    {
        if (sliderGesture || canvas.editing()) return true;
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        if (source.isDragging()) if (const auto* target = source.getComponentUnderMouse()) if (target == this || isParentOf (target)) return true;
        if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr && hasKeyboardFocus (true)) return true;
        return false;
    }
    std::vector<AssignableSlider*> getParameterSliders()
    { std::vector<AssignableSlider*> result; for (auto& row : parameterRows) result.push_back (&row->slider); return result; }
    int getPreferredHeight() const
    {
        int height = 478 + (showDetails ? detailsHeight() : 0);
        for (const auto& row : parameterRows) height += row->preferredHeight() + 6;
        return height;
    }
    int preferredHeight() const { return getPreferredHeight(); }
    void refreshPreview()
    {
        juce::String message = editError.isNotEmpty() ? editError : (decodeError.isNotEmpty() ? decodeError : program.error);
        if (message.isEmpty())
        {
            const auto values = parameterValues();
            // Always inspect the processing domain independently of the chosen
            // display crop/resolution. This is a sampled diagnostic, not an
            // audio approximation; the DSP evaluates the expression directly.
            for (int i = 0; i <= 1024; ++i)
            {
                const double t = (double) i / 1024;
                const double x = graph.unit == Unit::Normalized ? t : 20.0 * std::pow (frequencyLimit / 20.0, t);
                const auto result = program.evaluate (x, values);
                if (! result.valid) { message = "Node " + juce::String (result.errorNode) + ": undefined result in the audio frequency range."; break; }
            }
        }
        if (message.isEmpty() && ! validView()) message = "Preview range: max must exceed min; log axes need positive bounds.";
        if (message.isEmpty())
        {
            const auto values = parameterValues();
            for (int i = 0; i <= view.samples; ++i)
            {
                const auto result = program.evaluate (Preview::fromAxis ((double) i / view.samples, view.xMin, view.xMax, view.logX), values);
                if (! result.valid) { message = "Node " + juce::String (result.errorNode) + ": undefined result in the displayed range."; break; }
            }
        }
        status.setColour (juce::Label::textColourId, message.isEmpty() ? Theme::dim() : Theme::red());
        status.setText (message.isEmpty() ? "Output y  |  drag sockets to connect  |  drag background to pan" : message, juce::dontSendNotification);
        status.setTooltip (message.isEmpty() ? "Click or drag from a yellow output socket to a blue input or y. Right-click an input to disconnect. Escape cancels a wire; Delete removes the selected node." : message);
        preview.repaint();
    }
    void resized() override
    {
        auto area = getLocalBounds(); auto toolbar = area.removeFromTop (28);
        addNode.setBounds (toolbar.removeFromLeft (88).reduced (0, 1)); toolbar.removeFromLeft (5);
        addParameter.setBounds (toolbar.removeFromLeft (90).reduced (0, 1)); toolbar.removeFromLeft (5);
        details.setBounds (toolbar.removeFromRight (68).reduced (0, 1)); toolbar.removeFromRight (5); units.setBounds (toolbar.reduced (0, 1));
        area.removeFromTop (6); viewport.setBounds (area.removeFromTop (242)); area.removeFromTop (5);
        auto actions = area.removeFromTop (24); setOutput.setBounds (actions.removeFromLeft (112)); actions.removeFromLeft (5); removeNode.setBounds (actions.removeFromLeft (112));
        area.removeFromTop (5); preview.setBounds (area.removeFromTop (132)); status.setBounds (area.removeFromTop (30)); area.removeFromTop (6);
        if (showDetails)
        {
            detailBounds = area.removeFromTop (detailsHeight()); auto panel = detailBounds;
            panel.removeFromTop (21); auto range = panel.removeFromTop (42); const int w = (range.getWidth() - 18) / 4;
            for (auto* field : { &xMin, &xMax, &yMin, &yMax }) { field->setBounds (range.removeFromLeft (w)); range.removeFromLeft (6); }
            panel.removeFromTop (5); auto toggles = panel.removeFromTop (26);
            logX.setBounds (toggles.removeFromLeft (78)); logY.setBounds (toggles.removeFromLeft (78)); grid.setBounds (toggles.removeFromLeft (70)); precision.setBounds (toggles);
            panel.removeFromTop (10);
            if (findParameter (selectedParameter))
            {
                panel.removeFromTop (20); auto row = panel.removeFromTop (42);
                name.setBounds (row.removeFromLeft (juce::jmax (100, row.getWidth() / 2)).withTrimmedRight (6)); minimum.setBounds (row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (6)); maximum.setBounds (row);
                panel.removeFromTop (5); row = panel.removeFromTop (42);
                defaultValue.setBounds (row.removeFromLeft (w)); row.removeFromLeft (6); step.setBounds (row.removeFromLeft (w)); row.removeFromLeft (6);
                parameterLog.setBounds (row.removeFromLeft (112).withTrimmedTop (17)); removeParameter.setBounds (row.withTrimmedTop (17));
            }
            else if (const auto* node = findNode (selectedNode); node && node->op == Op::Constant) constant.setBounds (panel.removeFromTop (42).removeFromLeft (160));
        }
        else detailBounds = {};
        for (auto& row : parameterRows) { row->setBounds (area.removeFromTop (row->preferredHeight())); area.removeFromTop (6); }
    }
    void paint (juce::Graphics& g) override
    {
        if (showDetails)
        {
            Theme::text (g, "PREVIEW ONLY / DISPLAY RANGE", detailBounds.withHeight (18), 10, Theme::yellow(), true);
            if (const auto* p = findParameter (selectedParameter)) Theme::text (g, "PARAMETER / " + p->name, detailBounds.withTrimmedTop (104).withHeight (18), 10, Theme::yellow(), true);
        }
    }
private:
    Node* findNode (int id) { for (auto& node : graph.nodes) if (node.id == id) return &node; return nullptr; }
    const Node* findNode (int id) const { for (const auto& node : graph.nodes) if (node.id == id) return &node; return nullptr; }
    Parameter* findParameter (const juce::String& id) { for (auto& p : graph.parameters) if (p.id == id) return &p; return nullptr; }
    const Parameter* findParameter (const juce::String& id) const { for (const auto& p : graph.parameters) if (p.id == id) return &p; return nullptr; }
    int detailsHeight() const { if (findParameter (selectedParameter)) return 220; const auto* node = findNode (selectedNode); return node && node->op == Op::Constant ? 154 : 108; }
    void notifyLayout() { resized(); if (layoutChanged) layoutChanged(); repaint(); }
    void showError (const juce::String& message) { editError = message; status.setColour (juce::Label::textColourId, Theme::red()); status.setText (message, juce::dontSendNotification); }
    void submitProperty (const juce::Identifier& key, const juce::var& value)
    { state.setProperty (key, value, nullptr); if (onChange) onChange (key, value); }
    void submitGraph()
    {
        decodeError.clear(); editError.clear(); program = Program::compile (graph); submitProperty ("remapFormula", graph.encode());
        addNode.setEnabled (graph.nodes.size() < Formula::maxNodes); addParameter.setEnabled (graph.parameters.size() < Formula::maxParameters && graph.nodes.size() < Formula::maxNodes);
        refreshPreview(); canvas.repaint();
    }
    void selectNode (int id)
    {
        selectedNode = id; selectedParameter.clear(); if (const auto* node = findNode (id)) selectedParameter = node->parameterId;
        updateDetails(); canvas.repaint(); notifyLayout();
    }
    void selectParameter (const juce::String& id)
    {
        selectedParameter = id; for (const auto& node : graph.nodes) if (node.parameterId == id) { selectedNode = node.id; break; }
        showDetails = true; details.setToggleState (true, juce::dontSendNotification); updateDetails(); canvas.repaint(); notifyLayout();
    }
    void deleteSelectedNode()
    {
        if (! findNode (selectedNode)) return;
        const int removed = selectedNode;
        graph.nodes.erase (std::remove_if (graph.nodes.begin(), graph.nodes.end(), [removed] (const Node& node) { return node.id == removed; }), graph.nodes.end());
        for (auto& node : graph.nodes) for (auto& input : node.inputs) if (input == removed) input = -1;
        if (graph.outputNode == removed) graph.outputNode = -1;
        if (graph.nodes.empty()) { Node input; input.id = nextNodeId++; graph.nodes.push_back (input); }
        canvas.cancel(); selectedNode = -1; selectedParameter.clear(); submitGraph(); updateDetails(); notifyLayout();
    }
    void deleteSelectedParameter()
    {
        const auto id = selectedParameter; if (! findParameter (id)) return;
        std::vector<int> removed;
        for (const auto& node : graph.nodes) if (node.op == Op::Parameter && node.parameterId == id) removed.push_back (node.id);
        graph.nodes.erase (std::remove_if (graph.nodes.begin(), graph.nodes.end(), [&] (const Node& node) { return std::find (removed.begin(), removed.end(), node.id) != removed.end(); }), graph.nodes.end());
        for (auto& node : graph.nodes) for (auto& input : node.inputs) if (std::find (removed.begin(), removed.end(), input) != removed.end()) input = -1;
        if (std::find (removed.begin(), removed.end(), graph.outputNode) != removed.end()) graph.outputNode = -1;
        graph.parameters.erase (std::remove_if (graph.parameters.begin(), graph.parameters.end(), [&] (const Parameter& p) { return p.id == id; }), graph.parameters.end());
        if (graph.nodes.empty()) { Node input; input.id = nextNodeId++; graph.nodes.push_back (input); }
        canvas.cancel(); selectedNode = -1; selectedParameter.clear(); rebuildParameters(); submitGraph(); updateDetails(); notifyLayout();
    }
    void showNodeMenu()
    {
        juce::PopupMenu menu;
        for (int i = (int) Op::Input; i <= (int) Op::Sign; ++i) if ((Op) i != Op::Parameter) menu.addItem (i + 1, scrr::params::formulaOpName ((Op) i));
        if (! graph.parameters.empty())
        {
            juce::PopupMenu parameters; for (size_t i = 0; i < graph.parameters.size(); ++i) parameters.addItem (100 + (int) i, graph.parameters[i].name);
            menu.addSubMenu ("Parameters", parameters);
        }
        const auto safe = juce::Component::SafePointer<RemapFormulaEditor> (this); const auto uid = getModuleUid();
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addNode), [safe, uid] (int result)
        {
            if (! safe || safe->getModuleUid() != uid || result <= 0) return;
            if (result >= 100) { const auto index = (size_t) (result - 100); if (index < safe->graph.parameters.size()) safe->createNode (Op::Parameter, safe->graph.parameters[index].id); }
            else safe->createNode ((Op) (result - 1), {});
        });
    }
    void createNode (Op op, const juce::String& parameter)
    {
        if (graph.nodes.size() >= Formula::maxNodes) return;
        for (const auto& node : graph.nodes) nextNodeId = juce::jmax (nextNodeId, node.id + 1);
        Node node; node.id = nextNodeId++; node.op = op; node.parameterId = parameter; node.value = op == Op::Constant ? 1 : 0;
        node.canvasX = juce::jlimit (12.0, 1620.0, (double) viewport.getViewPositionX() + 190 + (double) (graph.nodes.size() % 3) * 24);
        node.canvasY = juce::jlimit (12.0, 1080.0, (double) viewport.getViewPositionY() + 28 + (double) (graph.nodes.size() % 4) * 48);
        graph.nodes.push_back (node); selectNode (node.id); submitGraph();
    }
    void createParameter()
    {
        if (graph.parameters.size() >= Formula::maxParameters || graph.nodes.size() >= Formula::maxNodes) return;
        Parameter p; p.id = juce::Uuid().toString().removeCharacters ("-"); int number = 1;
        for (;;) { p.name = "Parameter " + juce::String (number++); bool exists = false; for (const auto& existing : graph.parameters) exists = exists || existing.name.equalsIgnoreCase (p.name); if (! exists) break; }
        p.defaultValue = .5; graph.parameters.push_back (p);
        createNode (Op::Parameter, p.id); submitProperty (p.property(), p.defaultValue); rebuildParameters(); selectParameter (p.id);
    }
    void rebuildParameters()
    {
        bool sameIds = parameterRows.size() == graph.parameters.size();
        if (sameIds) for (size_t i = 0; i < parameterRows.size(); ++i) sameIds = sameIds && parameterRows[i]->id == graph.parameters[i].id;
        if (sameIds) { for (size_t i = 0; i < parameterRows.size(); ++i) parameterRows[i]->configure (graph.parameters[i]); }
        else
        {
            parameterRows.clear();
            for (const auto& p : graph.parameters) { auto row = std::make_unique<ParameterRow> (*this, p); addAndMakeVisible (*row); parameterRows.push_back (std::move (row)); }
        }
        if (onParametersChanged) onParametersChanged();
    }
    void updateDetails()
    {
        for (auto* field : { &xMin, &xMax, &yMin, &yMax }) field->setVisible (showDetails);
        for (auto* component : std::initializer_list<juce::Component*> { &logX, &logY, &grid, &precision }) component->setVisible (showDetails);
        xMin.set (view.xMin); xMax.set (view.xMax); yMin.set (view.yMin); yMax.set (view.yMax);
        logX.setToggleState (view.logX, juce::dontSendNotification); logY.setToggleState (view.logY, juce::dontSendNotification); grid.setToggleState (view.grid, juce::dontSendNotification);
        precision.setSelectedId (view.samples, juce::dontSendNotification);
        const auto* p = findParameter (selectedParameter); const bool hasParameter = showDetails && p != nullptr;
        for (auto* field : { &name, &minimum, &maximum, &defaultValue, &step }) field->setVisible (hasParameter);
        parameterLog.setVisible (hasParameter); removeParameter.setVisible (hasParameter);
        if (p) { name.set (p->name); minimum.set (p->minimum); maximum.set (p->maximum); defaultValue.set (p->defaultValue); step.set (p->step); parameterLog.setToggleState (p->logarithmic, juce::dontSendNotification); }
        const auto* node = findNode (selectedNode); constant.setVisible (showDetails && node && node->op == Op::Constant); if (node) constant.set (node->value);
        setOutput.setEnabled (node != nullptr); removeNode.setEnabled (node != nullptr);
        addNode.setEnabled (graph.nodes.size() < Formula::maxNodes); addParameter.setEnabled (graph.parameters.size() < Formula::maxParameters && graph.nodes.size() < Formula::maxNodes);
    }
    void commitParameterFields()
    {
        auto* p = findParameter (selectedParameter); if (! p) return;
        auto edited = *p; edited.name = name.editor.getText().trim(); edited.minimum = minimum.number(); edited.maximum = maximum.number(); edited.defaultValue = defaultValue.number(); edited.step = step.number(); edited.logarithmic = parameterLog.getToggleState();
        auto candidate = graph; for (auto& parameter : candidate.parameters) if (parameter.id == p->id) parameter = edited;
        // Validate parameters independently of unfinished graph connections.
        auto validation = candidate; validation.nodes = { Node {} }; validation.outputNode = 0;
        const auto checked = Program::compile (validation);
        if (! checked.valid) { showError (checked.error); return; }
        *p = edited;
        const auto value = juce::jlimit (p->minimum, p->maximum, (double) state.getProperty (p->property(), p->defaultValue));
        submitProperty (p->property(), value); submitGraph();
        for (auto& row : parameterRows) if (row->id == p->id) row->configure (*p);
        if (onParametersChanged) onParametersChanged(); updateDetails(); notifyLayout();
    }
    bool validView() const
    { return std::isfinite (view.xMin) && std::isfinite (view.xMax) && std::isfinite (view.yMin) && std::isfinite (view.yMax) && view.xMax > view.xMin && view.yMax > view.yMin && (! view.logX || view.xMin > 0) && (! view.logY || view.yMin > 0); }
    void commitViewFields()
    {
        if (! std::isfinite (xMin.number()) || ! std::isfinite (xMax.number()) || ! std::isfinite (yMin.number()) || ! std::isfinite (yMax.number()))
        { showError ("Preview limits must be finite numbers."); return; }
        editError.clear();
        view.xMin = xMin.number(); view.xMax = xMax.number(); view.yMin = yMin.number(); view.yMax = yMax.number();
        view.logX = logX.getToggleState(); view.logY = logY.getToggleState(); view.grid = grid.getToggleState(); view.samples = juce::jlimit (64, 1024, precision.getSelectedId()); submitView(); refreshPreview();
    }
    void resetViewRange()
    {
        const bool hertz = graph.unit == Unit::Hertz; view.xMin = view.yMin = hertz ? 20 : 0; view.xMax = view.yMax = hertz ? frequencyLimit : 1; view.logX = view.logY = hertz;
    }
    void loadView (const juce::String& encoded)
    {
        view = {}; resetViewRange(); const auto parsed = juce::JSON::parse (encoded); const auto* object = parsed.getDynamicObject(); if (! object) return;
        auto number = [&] (const char* key, double fallback) { const auto value = object->getProperty (key); const double n = value.isVoid() ? fallback : (double) value; return std::isfinite (n) ? n : fallback; };
        view.xMin = number ("xMin", view.xMin); view.xMax = number ("xMax", view.xMax); view.yMin = number ("yMin", view.yMin); view.yMax = number ("yMax", view.yMax);
        for (const auto& item : { std::pair<const char*, bool*> { "logX", &view.logX }, { "logY", &view.logY }, { "grid", &view.grid } }) if (object->hasProperty (item.first)) *item.second = (bool) object->getProperty (item.first);
        view.samples = juce::jlimit (64, 1024, (int) number ("samples", 256)); view.outputX = juce::jlimit (12.0, 1620.0, number ("outputX", 350)); view.outputY = juce::jlimit (12.0, 1080.0, number ("outputY", 80));
    }
    void submitView()
    {
        auto* object = new juce::DynamicObject;
        object->setProperty ("xMin", view.xMin); object->setProperty ("xMax", view.xMax); object->setProperty ("yMin", view.yMin); object->setProperty ("yMax", view.yMax);
        object->setProperty ("logX", view.logX); object->setProperty ("logY", view.logY); object->setProperty ("grid", view.grid); object->setProperty ("samples", view.samples);
        object->setProperty ("outputX", view.outputX); object->setProperty ("outputY", view.outputY); submitProperty ("formulaView", juce::JSON::toString (juce::var (object), true));
    }
    std::array<double, Formula::maxParameters> parameterValues() const
    {
        std::array<double, Formula::maxParameters> result {};
        for (size_t i = 0; i < program.parameterCount; ++i)
        {
            const auto& p = program.parameters[i]; const double base = (double) state.getProperty (p.property(), p.defaultValue);
            const double value = effectiveValue ? effectiveValue (p.property(), base) : base;
            result[i] = std::isfinite (value) ? juce::jlimit (p.minimum, p.maximum, value) : p.defaultValue;
        }
        return result;
    }

    juce::ValueTree state { "module" }; Formula graph; Program program; View view; double frequencyLimit { 20000 }; juce::String decodeError, editError, selectedParameter;
    int selectedNode { 0 }, nextNodeId { 1 }; bool showDetails {}, sliderGesture {};
    juce::TextButton addNode, addParameter, details, setOutput, removeNode, removeParameter; juce::ComboBox units, precision;
    juce::ToggleButton logX, logY, grid, parameterLog; juce::Label status; juce::Viewport viewport; Canvas canvas; Preview preview;
    Entry xMin { "X MIN", "formula-view-x-min" }, xMax { "X MAX", "formula-view-x-max" }, yMin { "Y MIN", "formula-view-y-min" }, yMax { "Y MAX", "formula-view-y-max" };
    Entry name { "NAME", "formula-param-name" }, minimum { "MIN", "formula-param-min" }, maximum { "MAX", "formula-param-max" }, defaultValue { "DEFAULT", "formula-param-default" }, step { "STEP", "formula-param-step" }, constant { "CONSTANT", "formula-constant" };
    juce::Rectangle<int> detailBounds; std::vector<std::unique_ptr<ParameterRow>> parameterRows;
};
} // namespace scrr::gui
