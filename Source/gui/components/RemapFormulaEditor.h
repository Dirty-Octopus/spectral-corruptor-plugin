// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include "../Theme.h"
#include "../AssignableSlider.h"
#include "../FormulaWindowPreferences.h"
#include "../../parameters/RemapFormula.h"
#include "../../parameters/RemapFormulaRandom.h"
#include <functional>
#include <cstdlib>
#include <limits>

namespace scrr::gui {

/** A graph editor with an optional larger workspace. Formula state is executable; formulaView is
    presentation only. Callbacks always refer to the currently selected UID. */
class RemapFormulaEditor : public juce::Component
{
    using Formula = scrr::params::RemapFormula;
    using Node = scrr::params::FormulaNode;
    using Parameter = scrr::params::FormulaParameter;
    using Op = scrr::params::FormulaOp;
    using Unit = scrr::params::FormulaUnit;
    using Program = scrr::params::FormulaProgram;

    class Surface : public juce::Component
    {
    public:
        explicit Surface (RemapFormulaEditor& editor) : owner (editor) { setComponentID ("formula-surface"); }
        void resized() override { owner.layoutSurface(); }
        void paint (juce::Graphics& g) override { owner.paintSurface (g); }
    private:
        RemapFormulaEditor& owner;
    };
    class PopoutButton : public juce::Button
    {
    public:
        PopoutButton() : juce::Button ("Open larger Formula editor") { setComponentID ("formula-popout"); }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            g.fillAll (down ? Theme::blue() : over ? Theme::panel().brighter (.15f) : Theme::panel());
            g.setColour (over ? Theme::white() : Theme::line()); g.drawRect (getLocalBounds());
            auto b = getLocalBounds().toFloat().reduced (7);
            g.setColour (Theme::yellow());
            juce::Path icon;
            icon.startNewSubPath (b.getX(), b.getY() + 3); icon.lineTo (b.getX(), b.getBottom()); icon.lineTo (b.getRight() - 3, b.getBottom());
            icon.startNewSubPath (b.getCentreX() - 2, b.getCentreY() + 2); icon.lineTo (b.getRight(), b.getY());
            icon.startNewSubPath (b.getCentreX(), b.getY()); icon.lineTo (b.getRight(), b.getY()); icon.lineTo (b.getRight(), b.getCentreY());
            g.strokePath (icon, juce::PathStrokeType (1.5f));
        }
    };
    class PopoutWindow : public juce::DocumentWindow
    {
    public:
        PopoutWindow (RemapFormulaEditor& editor, bool native)
            : juce::DocumentWindow ("Spectral Corruptor / Formula", Theme::black(), juce::DocumentWindow::closeButton, native), owner (&editor)
        {
            setComponentID ("formula-popout-window"); setUsingNativeTitleBar (native);
            setResizable (true, true); setDraggable (true); setResizeLimits (640, 480, 2400, 1800);
            if (auto* bounds = getConstrainer()) bounds->setMinimumOnscreenAmounts (32, 80, 80, 80);
        }
        ~PopoutWindow() override { clearContentComponent(); setLookAndFeel (nullptr); }
        void parentSizeChanged() override
        {
            juce::DocumentWindow::parentSizeChanged();
            if (auto* parent = getParentComponent())
            {
                const auto area = parent->getLocalBounds().reduced (8);
                if (area.isEmpty()) return;
                setResizeLimits (juce::jmin (640, area.getWidth()), juce::jmin (480, area.getHeight()), area.getWidth(), area.getHeight());
                const int width = juce::jmin (getWidth(), area.getWidth()), height = juce::jmin (getHeight(), area.getHeight());
                setBounds (juce::jlimit (area.getX(), area.getRight() - width, getX()), juce::jlimit (area.getY(), area.getBottom() - height, getY()), width, height);
            }
        }
        void closeButtonPressed() override
        {
            // DocumentWindow is owned by the editor. Release it only after this
            // native close-button callback has returned.
            setVisible (false);
            const auto safeOwner = owner;
            const auto safeWindow = juce::Component::SafePointer<PopoutWindow> (this);
            juce::MessageManager::callAsync ([safeOwner, safeWindow]
            {
                if (safeOwner && safeWindow && safeOwner->popout.get() == safeWindow.getComponent()) safeOwner->closePopout();
            });
        }
    private:
        juce::Component::SafePointer<RemapFormulaEditor> owner;
    };

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
        ParameterRow (RemapFormulaEditor& editor, const Parameter& parameter, int node) : owner (editor), id (parameter.id), nodeId (node)
        {
            setComponentID ("formula-row-" + id + "-" + juce::String (node)); addAndMakeVisible (slider);
            slider.setComponentID (parameter.property().toString());
            slider.mappingMenu = [this] { if (owner.mappingMenu) owner.mappingMenu (slider, juce::Identifier (slider.getComponentID())); };
            slider.onDragStart = [this] { owner.sliderGesture = true; };
            slider.onDragEnd = [this] { owner.sliderGesture = false; };
            slider.onValueChange = [this]
            {
                owner.submitProperty (juce::Identifier (slider.getComponentID()), slider.getValue());
                for (auto& row : owner.parameterRows) if (row.get() != this && row->id == id) row->slider.setValue (slider.getValue(), juce::dontSendNotification);
                owner.refreshPreview();
            };
            configure (parameter);
        }
        void configure (const Parameter& parameter)
        {
            if (std::isfinite (parameter.minimum) && std::isfinite (parameter.maximum) && parameter.maximum > parameter.minimum)
            {
                const double interval = std::isfinite (parameter.step) ? juce::jlimit (0.0, parameter.maximum - parameter.minimum, parameter.step) : 0;
                const double reset = std::isfinite (parameter.defaultValue) ? juce::jlimit (parameter.minimum, parameter.maximum, parameter.defaultValue) : parameter.minimum;
                setupSlider (slider, parameter.minimum, parameter.maximum, interval, reset); slider.setSkewFactor (1.0);
                if (parameter.logarithmic && parameter.minimum > 0)
                {
                    const double midpoint = std::exp ((std::log (parameter.minimum) + std::log (parameter.maximum)) * .5);
                    if (std::isfinite (midpoint) && midpoint > parameter.minimum && midpoint < parameter.maximum) slider.setSkewFactorFromMidPoint (midpoint);
                }
            }
            else setupSlider (slider, 0, 1, 0, 0);
            slider.presetResetValue = [this, fallback = slider.getDoubleClickReturnValue()]
            { return owner.parameterResetValue ? owner.parameterResetValue (juce::Identifier (slider.getComponentID()), fallback) : fallback; };
            slider.setTextBoxStyle (juce::Slider::TextBoxAbove, false, 184, 24); slider.setScrollWheelEnabled (false);
            const double savedValue = (double) owner.state.getProperty (parameter.property(), parameter.defaultValue);
            slider.setValue (std::isfinite (savedValue) ? savedValue : slider.getMinimum(), juce::dontSendNotification);
        }
        int preferredHeight() const { return 54 + slider.modulationHeight(); }
        void resized() override { slider.setBounds (getLocalBounds()); }
        RemapFormulaEditor& owner; juce::String id; int nodeId; AssignableSlider slider;
    };
    struct View
    {
        double xMin { 20 }, xMax { 20000 }, yMin { 20 }, yMax { 20000 };
        bool logX { true }, logY { true }, grid { true }; int samples { 256 };
        double outputX { 350 }, outputY { 80 }, zoom { .8 }, probeX { 200 }, panX {}, panY {};
        int complexity { 8 };
        bool followMouse {}, symbolic {}, chinese {};
    };
    struct Edge { int source { -1 }, destination { -1 }, pin {}; };
    class Canvas : public juce::Component
    {
    public:
        explicit Canvas (RemapFormulaEditor& editor) : owner (editor) { setWantsKeyboardFocus (true); setComponentID ("formula-canvas"); }
        bool editing() const { return dragging || panning || pendingSource >= 0; }
        juce::Rectangle<float> boundsFor (const Node& node) const
        {
            float height = 108;
            const int arity = scrr::params::formulaOpArity (node.op);
            if (arity > 0) height = (float) (108 + arity * 22);
            if (node.op == Op::Parameter)
            {
                height = 154;
                for (const auto& row : owner.parameterRows) if (row->nodeId == node.id) height += (float) row->slider.modulationHeight();
                if (owner.expandedNode == node.id) height += 170;
            }
            return { local (node.canvasX - owner.view.panX), local (node.canvasY - owner.view.panY), 210, height };
        }
        juce::Rectangle<float> outputBounds() const { return { local (owner.view.outputX - owner.view.panX), local (owner.view.outputY - owner.view.panY), 130, 84 }; }
        juce::Point<float> outputFor (const Node& node) const { const auto b = boundsFor (node); return { b.getRight(), b.getY() + 18 }; }
        juce::Point<float> inputFor (const Node& node, int pin) const { const auto b = boundsFor (node); return { b.getX(), b.getY() + 86 + (float) pin * 22 }; }
        juce::Point<float> outputInput() const { const auto b = outputBounds(); return { b.getX(), b.getY() + 48 }; }
        static float local (double value) { return (float) juce::jlimit (-1.0e7, 1.0e7, value); }
        juce::Point<double> world (juce::Point<float> point) const { return { owner.view.panX + point.x, owner.view.panY + point.y }; }
        static juce::Path wirePath (juce::Point<float> from, juce::Point<float> to)
        {
            juce::Path path; path.startNewSubPath (from);
            const auto bend = juce::jmax (to.x >= from.x ? 8.0f : 40.0f, std::abs (to.x - from.x) * .5f);
            path.cubicTo (from.x + bend, from.y, to.x - bend, to.y, to.x, to.y); return path;
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::field()); g.addTransform (juce::AffineTransform::scale ((float) owner.view.zoom));
            g.setColour (Theme::line().withAlpha (.4f)); const auto clip = g.getClipBounds();
            const int gridStep = owner.view.zoom < .25 ? 100 : 20;
            const int offsetX = (int) -std::fmod (owner.view.panX, gridStep), offsetY = (int) -std::fmod (owner.view.panY, gridStep);
            for (int x = offsetX - gridStep; x < clip.getRight(); x += gridStep)
                for (int y = offsetY - gridStep; y < clip.getBottom(); y += gridStep) g.fillRect (x, y, 1, 1);
            for (const auto& node : owner.graph.nodes)
                for (int pin = 0; pin < scrr::params::formulaOpArity (node.op); ++pin)
                    if (const auto* source = owner.findNode (node.inputs[(size_t) pin]); source && ! isDetached (node.id, pin))
                        wire (g, outputFor (*source), inputFor (node, pin), Theme::blue(), hoverEdge.source == source->id && hoverEdge.destination == node.id && hoverEdge.pin == pin);
            if (const auto* node = owner.findNode (owner.graph.outputNode); node && ! isDetached (-1, 0)) wire (g, outputFor (*node), outputInput(), Theme::yellow(), hoverEdge.source == node->id && hoverEdge.destination == -1);
            if (const auto* node = owner.findNode (pendingSource)) wire (g, outputFor (*node), cursor, Theme::yellow());
            for (const auto& node : owner.graph.nodes)
            {
                const auto b = boundsFor (node); if (! b.intersects (clip.toFloat())) continue;
                const auto r = b.toNearestInt();
                g.setColour (hoverNode == node.id ? Theme::panel().brighter (.10f) : Theme::panel()); g.fillRect (b); g.setColour (node.id == owner.selectedNode ? Theme::yellow() : hoverNode == node.id ? Theme::blue() : Theme::line());
                g.drawRect (b, node.id == owner.selectedNode ? 2.0f : 1.0f);
                auto title = owner.nodeName (node.op);
                if (node.op == Op::Parameter) if (const auto* p = owner.findParameter (node.parameterId)) title += " / " + p->name;
                Theme::text (g, title, r.reduced (12, 0).withHeight (32), 12, Theme::white(), true);
                socket (g, outputFor (node), Theme::yellow());
                const int arity = scrr::params::formulaOpArity (node.op);
                if (arity > 0)
                {
                    owner.paintExpression (g, node, r.withTrimmedTop (32).withHeight (38).reduced (12, 0));
                    for (int pin = 0; pin < arity; ++pin)
                    {
                        socket (g, inputFor (node, pin), Theme::blue());
                        const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + pin));
                        const auto text = owner.view.symbolic ? letter : letter + " = " + owner.nodeValue (node.inputs[(size_t) pin]);
                        Theme::text (g, text, { r.getX() + 12, (int) inputFor (node, pin).y - 9, 186, 18 }, 11, Theme::dim());
                    }
                    if (! owner.view.symbolic) Theme::text (g, "= " + owner.nodeValue (node.id), r.withY (r.getBottom() - 25).withHeight (20).reduced (12, 0), 12, Theme::yellow(), true);
                }
                else if (node.op == Op::Input)
                {
                    Theme::text (g, owner.view.symbolic ? "x" : "x = " + compact (owner.displayX()), r.withTrimmedTop (34).withHeight (32).reduced (12, 0), 20, Theme::yellow());
                    Theme::text (g, owner.graph.unit == Unit::Hertz ? "Hz / preview probe" : "0..1 LOG / preview probe", r.withTrimmedTop (76).withHeight (20).reduced (12, 0), 10, Theme::dim());
                }
                else if (node.op == Op::Parameter)
                {
                    if (const auto* p = owner.findParameter (node.parameterId))
                    {
                        int y = r.getY() + 96; for (const auto& row : owner.parameterRows) if (row->nodeId == node.id) y += row->slider.modulationHeight();
                        Theme::text (g, compact (p->minimum) + " ... " + compact (p->maximum) + (p->logarithmic ? "  LOG" : "  LINEAR"), { r.getX() + 12, y, 186, 18 }, 10, Theme::dim());
                        Theme::text (g, "default " + compact (p->defaultValue) + " / step " + compact (p->step), { r.getX() + 12, y + 17, 186, 18 }, 10, Theme::dim());
                        if (! owner.view.symbolic) Theme::text (g, "= " + owner.nodeValue (node.id), { r.getX() + 12, y + 34, 186, 18 }, 11, Theme::yellow());
                    }
                }
            }
            const auto b = outputBounds(); g.setColour (Theme::yellow()); g.drawRect (b, 2);
            Theme::text (g, owner.view.chinese ? juce::String::fromUTF8 ("y / 输出") : "y / Output", b.toNearestInt().reduced (12, 0).withHeight (30), 12, Theme::yellow(), true);
            Theme::text (g, owner.view.symbolic ? "y = f(x)" : "y = " + owner.nodeValue (owner.graph.outputNode), b.toNearestInt().withTrimmedTop (35).reduced (12, 0), 11, Theme::dim());
            socket (g, outputInput(), Theme::yellow());
        }
        void mouseDown (const juce::MouseEvent& event) override
        {
            grabKeyboardFocus(); cursor = event.position / (float) owner.view.zoom;
            if (event.mods.isPopupMenu())
            {
                cancel(); updateHover (cursor);
                for (auto it = owner.graph.nodes.rbegin(); it != owner.graph.nodes.rend(); ++it)
                    if (boundsFor (*it).expanded (7).contains (cursor)) { owner.selectNode (it->id); owner.showNodeContext (it->id, event.getScreenPosition()); return; }
                Edge edge;
                if (hitWire (cursor, edge)) owner.showCreateMenu (world (cursor), event.getScreenPosition(), edge);
                else if (outputBounds().contains (cursor)) owner.showOutputMenu (event.getScreenPosition());
                else owner.showCreateMenu (world (cursor), event.getScreenPosition());
                repaint(); return;
            }
            if (finishConnection (cursor)) return;
            pendingSource = -1; detached = {};
            // Lift the destination end without mutating the graph until release.
            // Escape cancels; dropping on empty space disconnects only this edge.
            Edge input; float inputDistance {};
            int nearestOutput = -1; float outputDistance = (float) (12 / owner.view.zoom);
            for (auto it = owner.graph.nodes.rbegin(); it != owner.graph.nodes.rend(); ++it)
                if (const auto distance = outputFor (*it).getDistanceFrom (cursor); distance < outputDistance)
                { nearestOutput = it->id; outputDistance = distance; }
            if (hitInput (cursor, input, &inputDistance) && input.source >= 0 && (nearestOutput < 0 || inputDistance <= outputDistance))
            { detached = input; pendingSource = input.source; repaint(); return; }
            if (nearestOutput >= 0) { pendingSource = nearestOutput; owner.selectNode (nearestOutput); repaint(); return; }
            for (auto it = owner.graph.nodes.rbegin(); it != owner.graph.nodes.rend(); ++it) if (boundsFor (*it).contains (cursor))
            { owner.selectNode (it->id); dragging = true; draggedOutput = false; start = cursor; startNode = { it->canvasX, it->canvasY }; return; }
            if (outputBounds().contains (cursor))
            { dragging = draggedOutput = true; start = cursor; startNode = { owner.view.outputX, owner.view.outputY }; return; }
            pendingSource = -1; panning = true; startPanCursor = event.position; startPan = { owner.view.panX, owner.view.panY }; repaint();
        }
        void mouseDoubleClick (const juce::MouseEvent& event) override
        {
            if (event.mods.isPopupMenu()) return;
            const auto point = event.position / (float) owner.view.zoom;
            for (auto it = owner.graph.nodes.rbegin(); it != owner.graph.nodes.rend(); ++it)
                if (it->op == Op::Parameter && boundsFor (*it).contains (point))
                {
                    const bool collapse = owner.expandedNode == it->id;
                    cancel(); owner.selectNode (it->id); owner.expandedNode = collapse ? -1 : it->id;
                    owner.updateDetails(); owner.layoutCanvas(); repaint(); return;
                }
        }
        void mouseDrag (const juce::MouseEvent& event) override
        {
            cursor = event.position / (float) owner.view.zoom;
            if (panning)
            {
                const auto delta = event.position - startPanCursor;
                owner.view.panX = startPan.x - delta.x / owner.view.zoom; owner.view.panY = startPan.y - delta.y / owner.view.zoom;
                clearHover(); owner.layoutCanvas(); repaint(); return;
            }
            if (dragging)
            {
                const double x = startNode.x + cursor.x - start.x, y = startNode.y + cursor.y - start.y;
                if (draggedOutput) { owner.view.outputX = x; owner.view.outputY = y; }
                else if (auto* node = owner.findNode (owner.selectedNode)) { node->canvasX = x; node->canvasY = y; }
                owner.layoutCanvas();
            }
            updateHover (cursor); repaint();
        }
        void mouseMove (const juce::MouseEvent& event) override { cursor = event.position / (float) owner.view.zoom; updateHover (cursor); repaint(); }
        void mouseExit (const juce::MouseEvent&) override { clearHover(); repaint(); }
        void mouseUp (const juce::MouseEvent& event) override
        {
            if (pendingSource >= 0 && event.mouseWasDraggedSinceMouseDown())
            {
                if (! finishConnection (event.position / (float) owner.view.zoom) && detached.source >= 0)
                { clearDetachedInput(); pendingSource = -1; detached = {}; owner.submitGraph(); }
            }
            if (dragging) { dragging = false; if (draggedOutput) owner.submitView(); else owner.submitGraph(); }
            if (panning) { panning = false; owner.submitView(); }
            repaint();
        }
        void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
        {
            const auto anchor = world (event.position / (float) owner.view.zoom);
            const float delta = std::abs (wheel.deltaY) > .00001f ? wheel.deltaY : wheel.deltaX;
            owner.view.zoom = juce::jlimit (.05, 2.0, owner.view.zoom * std::exp ((double) delta * 1.6));
            owner.view.panX = anchor.x - event.position.x / owner.view.zoom; owner.view.panY = anchor.y - event.position.y / owner.view.zoom;
            cursor = event.position / (float) owner.view.zoom; clearHover(); owner.layoutCanvas(); owner.submitView(); repaint();
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::escapeKey) { cancel(); repaint(); return true; }
            if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { owner.deleteSelectedNode(); return true; }
            return false;
        }
        void clearHover() { hoverNode = -1; hoverEdge = {}; }
        void cancel() { dragging = panning = false; pendingSource = -1; detached = {}; clearHover(); }
    private:
        bool isDetached (int destination, int pin) const { return detached.source >= 0 && detached.destination == destination && detached.pin == pin; }
        void clearDetachedInput()
        {
            if (detached.source < 0) return;
            if (detached.destination < 0) owner.graph.outputNode = -1;
            else if (auto* node = owner.findNode (detached.destination)) node->inputs[(size_t) detached.pin] = -1;
        }
        bool hitInput (juce::Point<float> point, Edge& edge, float* hitDistance = nullptr) const
        {
            float nearest = (float) (14 / owner.view.zoom); bool found = false;
            auto consider = [&] (juce::Point<float> position, Edge candidate)
            {
                const float distance = position.getDistanceFrom (point);
                if (distance < nearest) { nearest = distance; edge = candidate; found = true; }
            };
            consider (outputInput(), { owner.graph.outputNode, -1, 0 });
            for (auto it = owner.graph.nodes.rbegin(); it != owner.graph.nodes.rend(); ++it)
                for (int pin = 0; pin < scrr::params::formulaOpArity (it->op); ++pin)
                    consider (inputFor (*it, pin), { it->inputs[(size_t) pin], it->id, pin });
            if (hitDistance) *hitDistance = nearest;
            return found;
        }
        void updateHover (juce::Point<float> point)
        {
            clearHover();
            for (auto it = owner.graph.nodes.rbegin(); it != owner.graph.nodes.rend(); ++it)
                if (boundsFor (*it).expanded (7).contains (point)) { hoverNode = it->id; return; }
            hitWire (point, hoverEdge);
        }
        bool hitWire (juce::Point<float> point, Edge& edge) const
        {
            auto containsWire = [&] (juce::Point<float> from, juce::Point<float> to)
            { juce::Path stroke; juce::PathStrokeType ((float) (12 / owner.view.zoom)).createStrokedPath (stroke, wirePath (from, to)); return stroke.contains (point); };
            for (const auto& node : owner.graph.nodes) for (int pin = 0; pin < scrr::params::formulaOpArity (node.op); ++pin)
                if (const auto* source = owner.findNode (node.inputs[(size_t) pin]); source && containsWire (outputFor (*source), inputFor (node, pin)))
                { edge = { source->id, node.id, pin }; return true; }
            if (const auto* source = owner.findNode (owner.graph.outputNode); source && containsWire (outputFor (*source), outputInput()))
            { edge = { source->id, -1, 0 }; return true; }
            return false;
        }
        bool finishConnection (juce::Point<float> point)
        {
            if (pendingSource < 0) return false;
            Edge target; if (! hitInput (point, target)) return false;
            clearDetachedInput();
            if (target.destination < 0) owner.graph.outputNode = pendingSource;
            else if (auto* node = owner.findNode (target.destination)) node->inputs[(size_t) target.pin] = pendingSource;
            pendingSource = -1; detached = {}; owner.submitGraph(); return true;
        }
        static void socket (juce::Graphics& g, juce::Point<float> p, juce::Colour colour)
        { g.setColour (Theme::field()); g.fillEllipse (p.x - 5, p.y - 5, 10, 10); g.setColour (colour); g.drawEllipse (p.x - 5, p.y - 5, 10, 10, 2); }
        static void wire (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to, juce::Colour colour, bool hovered = false)
        {
            const auto path = wirePath (from, to);
            if (hovered) { g.setColour (colour.withAlpha (.2f)); g.strokePath (path, juce::PathStrokeType (10)); }
            g.setColour (hovered ? Theme::white() : colour); g.strokePath (path, juce::PathStrokeType (hovered ? 3.5f : 2.0f));
        }
        RemapFormulaEditor& owner; int pendingSource { -1 }, hoverNode { -1 }; Edge hoverEdge, detached; bool dragging {}, draggedOutput {}, panning {};
        juce::Point<float> cursor, start, startPanCursor; juce::Point<double> startNode, startPan;
    };
    class Preview : public juce::Component
    {
    public:
        explicit Preview (RemapFormulaEditor& editor) : owner (editor) { setComponentID ("formula-preview"); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::field()); const auto plot = getLocalBounds().toFloat().reduced (12, 22);
            Theme::text (g, "OUTPUT y / " + juce::String (owner.graph.unit == Unit::Hertz ? "Hz" : "0..1 LOG"), { 12, 1, getWidth() / 2 - 12, 18 }, 10, Theme::dim(), true);
            Theme::text (g, "probe x = " + compact (owner.displayX()), { getWidth() / 2, 1, getWidth() / 2 - 12, 18 }, 10, Theme::dim(), false, juce::Justification::centredRight);
            Theme::text (g, "x " + juce::String (owner.view.xMin, 2), { 12, getHeight() - 20, getWidth() / 2 - 12, 18 }, 10, Theme::dim());
            Theme::text (g, juce::String (owner.view.xMax, 2) + " x", { getWidth() / 2, getHeight() - 20, getWidth() / 2 - 12, 18 }, 10, Theme::dim(), false, juce::Justification::centredRight);
            g.setColour (Theme::line());
            if (owner.view.grid) for (int i = 0; i <= 4; ++i)
            { const float t = (float) i / 4; g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * t), plot.getY(), plot.getBottom()); g.drawHorizontalLine ((int) (plot.getY() + plot.getHeight() * t), plot.getX(), plot.getRight()); }
            juce::Graphics::ScopedSaveState saved (g); g.reduceClipRegion (plot.toNearestInt());
            if (owner.validView() && (! owner.view.logX || owner.displayX() > 0))
            {
                const double position = toAxis (owner.displayX(), owner.view.xMin, owner.view.xMax, owner.view.logX);
                if (std::isfinite (position) && position >= 0 && position <= 1)
                { g.setColour (Theme::blue()); g.drawVerticalLine ((int) (plot.getX() + (float) position * plot.getWidth()), plot.getY(), plot.getBottom()); }
            }
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
        void mouseMove (const juce::MouseEvent& event) override
        {
            if (! owner.view.followMouse || ! owner.validView()) return;
            const auto plot = getLocalBounds().toFloat().reduced (12, 22);
            if (! plot.contains (event.position)) return;
            owner.hoverProbeX = fromAxis (juce::jlimit (0.0, 1.0, (double) ((event.position.x - plot.getX()) / plot.getWidth())), owner.view.xMin, owner.view.xMax, owner.view.logX);
            owner.refreshReadouts(); owner.canvas.repaint(); repaint();
        }
        static double fromAxis (double t, double low, double high, bool logarithmic) { return logarithmic ? low * std::pow (high / low, t) : low + t * (high - low); }
        static double toAxis (double x, double low, double high, bool logarithmic) { return logarithmic ? std::log (x / low) / std::log (high / low) : (x - low) / (high - low); }
    private:
        RemapFormulaEditor& owner;
    };
public:
    std::function<void(const juce::Identifier&, const juce::var&)> onChange;
    std::function<void(const juce::String&, const juce::String&)> onGraphAndViewChange;
    std::function<void(juce::Component&, const juce::Identifier&)> mappingMenu;
    std::function<void()> onParametersChanged, layoutChanged;
    std::function<double(const juce::Identifier&, double)> effectiveValue;
    std::function<double(const juce::Identifier&, double)> parameterResetValue;

    RemapFormulaEditor() : surface (*this), canvas (*this), preview (*this)
    {
        setComponentID ("remap-formula-editor"); addAndMakeVisible (surface);
        surface.addAndMakeVisible (popoutButton); popoutButton.setTooltip ("Open a larger editable Formula workspace. Window style is in plugin Settings.");
        const auto safe = juce::Component::SafePointer<RemapFormulaEditor> (this);
        popoutButton.onClick = [safe] { if (safe) safe->openPopout(); };
        addChildComponent (returnToInspector); returnToInspector.setButtonText ("RETURN TO INSPECTOR"); returnToInspector.setComponentID ("formula-popout-return");
        returnToInspector.onClick = [safe]
        {
            juce::MessageManager::callAsync ([safe] { if (safe) safe->closePopout(); });
        };
        surface.addAndMakeVisible (details); details.setButtonText ("SETTINGS"); details.setComponentID ("formula-details"); details.setClickingTogglesState (true);
        details.onClick = [this] { showDetails = details.getToggleState(); updateDetails(); notifyLayout(); };
        units.addItem ("Hz", 1); units.addItem ("NORMALIZED (LOG)", 2); units.setComponentID ("formula-units"); surface.addAndMakeVisible (units);
        units.setTooltip ("Hz uses frequency directly. Normalized uses logarithmic 0..1 frequency coordinates. Changing units keeps node values.");
        units.onChange = [this] { graph.unit = units.getSelectedId() == 2 ? Unit::Normalized : Unit::Hertz; resetViewRange(); submitGraphAndView(); updateDetails(); };
        surface.addAndMakeVisible (probeDisclosure); probeDisclosure.setComponentID ("formula-probe-menu"); probeDisclosure.setButtonText ("PREVIEW x"); probeDisclosure.setClickingTogglesState (true);
        probeDisclosure.onClick = [this] { showProbe = probeDisclosure.getToggleState(); probe.setVisible (showProbe); probeMode.setVisible (showProbe); notifyLayout(); };
        surface.addChildComponent (probe); probe.onCommit = [this]
        {
            const double x = probe.number(); if (! std::isfinite (x)) { showError ("Probe x must be finite."); return; }
            view.probeX = hoverProbeX = x; view.followMouse = false; submitView(); updateDetails(); refreshReadouts(); canvas.repaint(); preview.repaint();
        };
        probeMode.addItem ("MANUAL x", 1); probeMode.addItem ("FOLLOW PREVIEW", 2); probeMode.setComponentID ("formula-probe-mode"); surface.addChildComponent (probeMode);
        probeMode.onChange = [this] { view.followMouse = probeMode.getSelectedId() == 2; hoverProbeX = view.probeX; submitView(); refreshReadouts(); canvas.repaint(); preview.repaint(); };
        viewport.setViewedComponent (&canvas, false); viewport.setScrollBarsShown (false, false); viewport.setScrollBarThickness (10); viewport.setComponentID ("formula-viewport"); surface.addAndMakeVisible (viewport);
        for (auto* button : { &randomGraph, &home, &bringOffscreen, &clearOffscreen }) surface.addAndMakeVisible (*button);
        randomGraph.setButtonText ("RANDOM"); randomGraph.setComponentID ("formula-random"); randomGraph.setTooltip ("Generate a connected formula graph. Node count is in Settings. Existing parameter definitions and mappings are preserved. Undo restores the previous graph.");
        home.setButtonText ("HOME"); home.setComponentID ("formula-home"); home.setTooltip ("Return the view to the origin at 100% zoom. Node positions and audio stay unchanged.");
        bringOffscreen.setButtonText ("BRING IN"); bringOffscreen.setComponentID ("formula-bring-offscreen"); bringOffscreen.setTooltip ("Move fully off-screen nodes into view, keeping their connections and values.");
        clearOffscreen.setButtonText ("CLEAR OUT"); clearOffscreen.setComponentID ("formula-clear-offscreen"); clearOffscreen.setTooltip ("Delete only nodes fully outside the visible canvas, along with their connected wires. Partial nodes and Output remain. Undo restores the graph.");
        home.onClick = [this] { canvas.cancel(); view.panX = view.panY = 0; view.zoom = 1; layoutCanvas(); submitView(); refreshPreview(); };
        bringOffscreen.onClick = [this] { bringOutsideNodes(); }; clearOffscreen.onClick = [this] { clearOutsideNodes(); }; randomGraph.onClick = [this] { generateGraph(); };
        surface.addAndMakeVisible (hints); hints.setFont (Theme::font (10)); hints.setColour (juce::Label::textColourId, Theme::dim());
        hints.setText ("Right-click: add / edit / delete   |   Wheel: zoom   |   Drag: pan", juce::dontSendNotification);
        hints.setTooltip ("Right-click a wire to insert a node. Double-click a Parameter to expand it. Drag a connected input to move its wire; drop on empty space to disconnect.");
        surface.addAndMakeVisible (preview); surface.addAndMakeVisible (status); status.setComponentID ("formula-status"); status.setFont (Theme::font (11));
        for (auto* field : { &xMin, &xMax, &yMin, &yMax }) { surface.addChildComponent (*field); field->onCommit = [this] { commitViewFields(); }; }
        for (auto* field : { &name, &minimum, &maximum, &defaultValue, &step }) { canvas.addChildComponent (*field); field->onCommit = [this] { commitParameterFields(); }; }
        logX.setButtonText ("X LOG"); logY.setButtonText ("Y LOG"); grid.setButtonText ("GRID"); parameterLog.setButtonText ("LOG PARAM");
        for (auto* toggle : { &logX, &logY, &grid }) surface.addChildComponent (*toggle);
        canvas.addChildComponent (parameterLog);
        logX.setComponentID ("formula-view-log-x"); logY.setComponentID ("formula-view-log-y"); grid.setComponentID ("formula-view-grid"); parameterLog.setComponentID ("formula-param-log");
        logX.onClick = logY.onClick = grid.onClick = [this] { commitViewFields(); }; parameterLog.onClick = [this] { commitParameterFields(); };
        for (const int count : { 64, 128, 256, 512, 1024 }) precision.addItem (juce::String (count), count);
        precision.setTooltip ("Preview plot sample count. Display only; audio calculation is unchanged.");
        precision.setComponentID ("formula-view-samples"); surface.addChildComponent (precision); precision.onChange = [this] { commitViewFields(); };
        nodeDisplay.addItem ("NUMERIC VALUES", 1); nodeDisplay.addItem ("ALGEBRAIC A / B", 2); nodeDisplay.setComponentID ("formula-node-display"); surface.addChildComponent (nodeDisplay);
        nodeLanguage.addItem ("English", 1); nodeLanguage.addItem (juce::String::fromUTF8 ("简体中文"), 2); nodeLanguage.setComponentID ("formula-node-language"); surface.addChildComponent (nodeLanguage);
        nodeLanguage.setTooltip ("Node names only. Independent of the plugin language; custom parameter names and mathematical symbols stay unchanged.");
        nodeDisplay.onChange = [this] { view.symbolic = nodeDisplay.getSelectedId() == 2; submitView(); canvas.repaint(); };
        nodeLanguage.onChange = [this] { view.chinese = nodeLanguage.getSelectedId() == 2; submitView(); canvas.repaint(); };
        invalidPolicy.addItem ("SILENCE AFFECTED FREQUENCIES", 1); invalidPolicy.addItem ("ORIGINAL INPUT FREQUENCY", 2);
        invalidPolicy.setComponentID ("formula-invalid-policy"); surface.addChildComponent (invalidPolicy);
        invalidPolicy.setTooltip ("Audio: undefined or out-of-range destinations silence only their source frequencies by default. Original input frequency keeps those sources in place. Plot display limits do not affect this policy.");
        invalidPolicy.onChange = [this] { submitProperty ("remapInvalidPolicy", invalidPolicy.getSelectedId() == 2 ? 1 : 0); refreshPreview(); };
        complexity.setComponentID ("formula-random-complexity"); complexity.setRange (3, 64, 1); complexity.setSliderStyle (juce::Slider::LinearHorizontal); complexity.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 24); surface.addChildComponent (complexity);
        complexity.onValueChange = [this] { view.complexity = (int) complexity.getValue(); submitView(); };
        compilePrograms(); rebuildParameters(); updateDetails();
    }
    ~RemapFormulaEditor() override
    {
        if (popout) { popout->clearContentComponent(); popout.reset(); }
        surface.setLookAndFeel (nullptr); viewport.setViewedComponent (nullptr, false);
    }
    void openPopout()
    {
        if (popout) { popout->setVisible (true); popout->toFront (true); return; }
        if (getModuleUid().isEmpty()) return;
        canvas.cancel();
        const bool native = FormulaWindowPreferences::get() == FormulaWindowPreferences::Mode::native;
        auto* host = getTopLevelComponent();
        if (! native)
        {
            juce::Component* transformedHost = nullptr;
            for (auto* ancestor = getParentComponent(); ancestor != nullptr; ancestor = ancestor->getParentComponent())
            {
                // The named logical surface remains the right owner at 100%
                // scale too, including when a Standalone host adds wrappers.
                if (ancestor->getComponentID() == "editor-content") { host = ancestor; transformedHost = nullptr; break; }
                if (transformedHost == nullptr && ! ancestor->getTransform().isIdentity()) transformedHost = ancestor;
            }
            if (transformedHost != nullptr) host = transformedHost;
        }
        // The same controls move between hosts; parameters, selection, viewport
        // and mapping callbacks retain one owner throughout the transfer.
        popout = std::make_unique<PopoutWindow> (*this, native);
        popout->setLookAndFeel (&getLookAndFeel()); surface.setLookAndFeel (&getLookAndFeel());
        popoutButton.setVisible (false); returnToInspector.setVisible (true);
        popout->setContentNonOwned (&surface, false);
        if (native)
        {
            auto bounds = juce::Rectangle<int> (0, 0, 1120, 820);
            const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds());
            if (display != nullptr) bounds = bounds.withSizeKeepingCentre (juce::jmin (1120, display->userArea.getWidth() - 40), juce::jmin (820, display->userArea.getHeight() - 40));
            popout->centreWithSize (bounds.getWidth(), bounds.getHeight());
        }
        else
        {
            if (host == this || host == nullptr) { closePopout(); return; }
            host->addAndMakeVisible (*popout);
            auto bounds = host->getLocalBounds().reduced (14);
            popout->setResizeLimits (juce::jmin (640, bounds.getWidth()), juce::jmin (480, bounds.getHeight()), bounds.getWidth(), bounds.getHeight());
            popout->setBounds (bounds.withSizeKeepingCentre (juce::jmin (1120, bounds.getWidth()), juce::jmin (800, bounds.getHeight())));
        }
        popout->setVisible (true); popout->toFront (true); surface.resized(); notifyLayout();
    }
    void closePopout()
    {
        if (! popout) return;
        canvas.cancel(); popout->clearContentComponent(); popout.reset();
        surface.setLookAndFeel (nullptr); addAndMakeVisible (surface);
        popoutButton.setVisible (true); returnToInspector.setVisible (false); notifyLayout();
    }
    bool isPopoutOpen() const { return popout != nullptr; }
    juce::DocumentWindow* getPopoutWindow() const { return popout.get(); }
    void visibilityChanged() override
    {
        if (! popout || isShowing()) return;
        // Inspector refreshes temporarily hide the same selected module. Check
        // after refresh before closing a workspace that is still selected.
        const auto safe = juce::Component::SafePointer<RemapFormulaEditor> (this);
        juce::MessageManager::callAsync ([safe] { if (safe && safe->popout && ! safe->isShowing()) safe->closePopout(); });
    }
    void setState (const juce::ValueTree& module)
    {
        const auto nextUid = module.getProperty ("uid").toString();
        const bool changedUid = nextUid != getModuleUid();
        if (changedUid) closePopout();
        const auto encoded = module.getProperty ("remapFormula").toString(); const auto encodedView = module.getProperty ("formulaView").toString();
        const bool changedGraph = changedUid || encoded != state.getProperty ("remapFormula").toString();
        const bool changedView = changedUid || encodedView != state.getProperty ("formulaView").toString();
        if (! changedGraph && ! changedView && isEditing()) return;
        // A host restore/Undo can replace this UID while a wire is pending.
        // Cancel its gesture before it can submit the stale graph back.
        if (changedGraph || changedView) canvas.cancel();
        state = module.createCopy();
        invalidPolicy.setSelectedId ((int) state.getProperty ("remapInvalidPolicy", 0) == 1 ? 2 : 1, juce::dontSendNotification);
        if (changedGraph)
        {
            editError.clear(); graph = Formula::decode (encoded, &decodeError);
            if (changedUid || ! findNode (selectedNode)) selectedNode = graph.nodes.empty() ? -1 : graph.nodes.front().id;
            if (changedUid) { selectedParameter.clear(); expandedNode = -1; canvas.cancel(); viewport.setViewPosition (0, 0); }
            compilePrograms(); rebuildParameters(); units.setSelectedId (graph.unit == Unit::Hertz ? 1 : 2, juce::dontSendNotification);
        }
        if (changedView) loadView (encodedView);
        for (auto& row : parameterRows) if (const auto* p = findParameter (row->id))
        {
            const double value = (double) state.getProperty (p->property(), p->defaultValue);
            row->slider.setValue (std::isfinite (value) ? value : row->slider.getMinimum(), juce::dontSendNotification);
        }
        if (changedGraph || changedView) { updateDetails(); layoutCanvas(); resized(); }
        refreshPreview();
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
        if (source.isDragging()) if (const auto* target = source.getComponentUnderMouse()) if (target == &surface || surface.isParentOf (target)) return true;
        return dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr && surface.hasKeyboardFocus (true);
    }
    std::vector<AssignableSlider*> getParameterSliders()
    { std::vector<AssignableSlider*> result; for (auto& row : parameterRows) result.push_back (&row->slider); return result; }
    int getPreferredHeight() const { return popout ? 78 : 578 + (showProbe ? 49 : 0) + (showDetails ? 248 : 0); }
    int preferredHeight() const { return getPreferredHeight(); }
    // Geometry is expressed in canvas pixels so host/UI tests use the same hit
    // targets as users after zooming, without coupling to the card layout.
    juce::Rectangle<float> getNodeBounds (int id) const { if (const auto* n = findNode (id)) return canvas.boundsFor (*n) * (float) view.zoom; return {}; }
    juce::Point<float> getInputPosition (int id, int pin) const { if (const auto* n = findNode (id)) return canvas.inputFor (*n, pin) * (float) view.zoom; return {}; }
    juce::Point<float> getOutputPosition (int id) const { if (const auto* n = findNode (id)) return canvas.outputFor (*n) * (float) view.zoom; return {}; }
    juce::Point<float> getOutputDestination() const { return canvas.outputInput() * (float) view.zoom; }
    double getCanvasZoom() const { return view.zoom; }
    juce::Point<float> getWireMidpoint (int destination, int pin) const
    {
        const auto* dest = findNode (destination); const auto* source = findNode (dest ? dest->inputs[(size_t) pin] : graph.outputNode);
        if (! source) return {};
        const auto path = Canvas::wirePath (canvas.outputFor (*source), dest ? canvas.inputFor (*dest, pin) : canvas.outputInput());
        return path.getPointAlongPath (path.getLength() * .5f) * (float) view.zoom;
    }
    void refreshPreview()
    {
        refreshReadouts(); layoutCanvas();
        juce::String message = editError.isNotEmpty() ? editError : (decodeError.isNotEmpty() ? decodeError : program.error);
        if (message.isEmpty())
        {
            const auto values = parameterValues();
            for (int i = 0; i <= 1024; ++i)
            {
                const double t = (double) i / 1024; const double x = graph.unit == Unit::Normalized ? t : 20.0 * std::pow (frequencyLimit / 20.0, t);
                const auto result = program.evaluate (x, values);
                if (! result.valid) { message = "Node " + juce::String (result.errorNode) + ": undefined result in the audio frequency range."; break; }
            }
        }
        if (message.isEmpty() && ! validView()) message = "Preview range: max must exceed min; log axes need positive bounds.";
        status.setColour (juce::Label::textColourId, message.isEmpty() ? Theme::dim() : Theme::red());
        status.setText (message.isEmpty() ? "Output y | Zoom " + juce::String (juce::roundToInt (view.zoom * 100)) + "%" : message, juce::dontSendNotification);
        status.setTooltip (message.isEmpty() ? "Right-click the background to add; a wire to insert; a node to edit or delete. Drag sockets to connect. Drag background to pan; wheel to zoom. Delete removes the selected node." : message);
        preview.repaint(); canvas.repaint();
    }
    void resized() override
    {
        if (! popout) surface.setBounds (getLocalBounds());
        else surface.resized();
        returnToInspector.setBounds (getLocalBounds().withTrimmedTop (36).withHeight (28));
    }
    void paint (juce::Graphics& g) override
    {
        if (popout) Theme::text (g, "FORMULA / OPEN IN LARGE WORKSPACE", getLocalBounds().withHeight (30), 11, Theme::yellow(), true);
    }
private:
    void layoutSurface()
    {
        auto area = surface.getLocalBounds().reduced (popout ? 12 : 0); auto toolbar = area.removeFromTop (28);
        if (! popout) { popoutButton.setBounds (toolbar.removeFromRight (28).reduced (0, 1)); toolbar.removeFromRight (5); }
        details.setBounds (toolbar.removeFromRight (82).reduced (0, 1)); toolbar.removeFromRight (5);
        probeDisclosure.setBounds (toolbar.removeFromRight (88).reduced (0, 1)); toolbar.removeFromRight (5); units.setBounds (toolbar.reduced (0, 1));
        if (showProbe)
        {
            area.removeFromTop (5); auto probeBar = area.removeFromTop (42);
            probe.setBounds (probeBar.removeFromLeft (juce::jmin (170, probeBar.getWidth() / 2)).withTrimmedRight (6)); probeMode.setBounds (probeBar.withTrimmedTop (17)); area.removeFromTop (2);
        }
        area.removeFromTop (6); auto navigation = area.removeFromTop (26); const int buttonWidth = (navigation.getWidth() - 18) / 4;
        for (auto* button : { &randomGraph, &home, &bringOffscreen, &clearOffscreen }) { button->setBounds (navigation.removeFromLeft (buttonWidth)); navigation.removeFromLeft (6); }
        hints.setBounds (area.removeFromTop (24));
        const bool sideDetails = popout && showDetails;
        detailBounds = {};
        if (sideDetails) { detailBounds = area.removeFromRight (270).withHeight (248); area.removeFromRight (12); }
        const int footerHeight = 154 + (showDetails && ! sideDetails ? 248 : 0);
        const int canvasHeight = popout ? juce::jmax (120, area.getHeight() - footerHeight) : 340;
        viewport.setBounds (area.removeFromTop (canvasHeight)); area.removeFromTop (7);
        preview.setBounds (area.removeFromTop (108)); status.setBounds (area.removeFromTop (32)); area.removeFromTop (7);
        if (showDetails && ! sideDetails) detailBounds = area.removeFromTop (248);
        if (showDetails)
        {
            auto panel = detailBounds; panel.removeFromTop (20); auto range = panel.removeFromTop (42); const int w = (range.getWidth() - 18) / 4;
            for (auto* field : { &xMin, &xMax, &yMin, &yMax }) { field->setBounds (range.removeFromLeft (w)); range.removeFromLeft (6); }
            panel.removeFromTop (6); auto toggles = panel.removeFromTop (26);
            logX.setBounds (toggles.removeFromLeft (60)); logY.setBounds (toggles.removeFromLeft (60)); grid.setBounds (toggles.removeFromLeft (58)); precision.setBounds (toggles);
            panel.removeFromTop (26); auto row = panel.removeFromTop (27); nodeDisplay.setBounds (row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (6)); nodeLanguage.setBounds (row);
            panel.removeFromTop (20); invalidPolicy.setBounds (panel.removeFromTop (27));
            panel.removeFromTop (25); complexity.setBounds (panel.removeFromTop (27));
        }
        layoutCanvas();
    }
    void paintSurface (juce::Graphics& g)
    {
        if (popout) g.fillAll (Theme::black());
        if (! showDetails) return;
        Theme::text (g, "PREVIEW / DISPLAY RANGE", detailBounds.withHeight (18), 10, Theme::yellow(), true);
        auto row = detailBounds.withTrimmedTop (101).withHeight (18);
        Theme::text (g, "NODE EXPRESSION", row.removeFromLeft (row.getWidth() / 2), 10, Theme::dim(), true);
        Theme::text (g, "NODE NAME LANGUAGE", row, 10, Theme::dim(), true);
        Theme::text (g, "AUDIO / INVALID OR OUT-OF-RANGE OUTPUT", detailBounds.withTrimmedTop (149).withHeight (18), 10, Theme::yellow(), true);
        Theme::text (g, "RANDOM GRAPH / NODE COUNT", detailBounds.withTrimmedTop (201).withHeight (18), 10, Theme::dim(), true);
    }
private:
    static juce::String compact (double value)
    {
        if (! std::isfinite (value)) return "undefined";
        if ((std::abs (value) > 0 && std::abs (value) < .0001) || std::abs (value) >= 1.0e6) return juce::String (value, 3, true);
        return juce::String (value, 4).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    }
    juce::String nodeName (Op op) const
    {
        static const char* english[] { "Input x", "Constant", "Parameter", "Add", "Subtract", "Multiply", "Divide", "Power", "Negate", "Absolute", "Minimum", "Maximum", "Clamp", "Square root", "Exponential", "Natural log", "Log base 2", "Log base 10", "Sine", "Cosine", "Tangent", "Floor", "Ceiling", "Round", "Remainder", "Sign" };
        static const char* chinese[] { "输入 x", "常数", "参数", "加法", "减法", "乘法", "除法", "幂", "取负", "绝对值", "最小值", "最大值", "限幅", "平方根", "指数", "自然对数", "以 2 为底的对数", "以 10 为底的对数", "正弦", "余弦", "正切", "向下取整", "向上取整", "四舍五入", "余数", "符号" };
        return juce::String::fromUTF8 ((view.chinese ? chinese : english)[(int) op]);
    }
    double displayX() const { return view.followMouse ? hoverProbeX : view.probeX; }
    juce::String nodeValue (int id) const
    {
        for (const auto& item : readouts) if (item.first == id) return item.second.valid ? compact (item.second.value) : "undefined";
        return juce::String::fromUTF8 ("—");
    }
    void paintExpression (juce::Graphics& g, const Node& node, juce::Rectangle<int> area) const
    {
        auto operand = [&] (int pin)
        {
            auto value = view.symbolic ? juce::String::charToString ((juce::juce_wchar) ('A' + pin)) : nodeValue (node.inputs[(size_t) pin]);
            return value.startsWithChar ('-') ? "(" + value + ")" : value;
        };
        const auto a = operand (0), b = operand (1), c = operand (2);
        if (node.op == Op::Power || node.op == Op::Exp)
        {
            const auto base = node.op == Op::Exp ? juce::String ("e") : a, exponent = node.op == Op::Exp ? a : b;
            const float total = (float) (base.length() * 13 + exponent.length() * 8 + 6);
            const float scale = juce::jmin (1.0f, (float) area.getWidth() / juce::jmax (1.0f, total));
            const int baseWidth = juce::roundToInt ((float) (base.length() * 13 + 4) * scale);
            Theme::text (g, base, { area.getX(), area.getY() + 9, baseWidth, 27 }, 22 * scale, Theme::white());
            Theme::text (g, exponent, { area.getX() + baseWidth, area.getY(), area.getWidth() - baseWidth, 23 }, 14 * scale, Theme::white()); return;
        }
        juce::String expression;
        switch (node.op)
        {
            case Op::Add: expression = a + " + " + b; break;
            case Op::Subtract: expression = a + juce::String::fromUTF8 (" − ") + b; break;
            case Op::Multiply: expression = a + juce::String::fromUTF8 (" × ") + b; break;
            case Op::Divide: expression = a + juce::String::fromUTF8 (" ÷ ") + b; break;
            case Op::Negate: expression = juce::String::fromUTF8 ("−") + a; break;
            case Op::Abs: expression = "|" + a + "|"; break;
            case Op::Min: expression = "min(" + a + ", " + b + ")"; break;
            case Op::Max: expression = "max(" + a + ", " + b + ")"; break;
            case Op::Clamp: expression = "clamp(" + a + ", " + b + ", " + c + ")"; break;
            case Op::Sqrt: expression = juce::String::fromUTF8 ("√(") + a + ")"; break;
            case Op::Log: expression = "ln(" + a + ")"; break;
            case Op::Log2: expression = juce::String::fromUTF8 ("log₂(") + a + ")"; break;
            case Op::Log10: expression = juce::String::fromUTF8 ("log₁₀(") + a + ")"; break;
            case Op::Sin: expression = "sin(" + a + ")"; break;
            case Op::Cos: expression = "cos(" + a + ")"; break;
            case Op::Tan: expression = "tan(" + a + ")"; break;
            case Op::Floor: expression = juce::String::fromUTF8 ("⌊") + a + juce::String::fromUTF8 ("⌋"); break;
            case Op::Ceil: expression = juce::String::fromUTF8 ("⌈") + a + juce::String::fromUTF8 ("⌉"); break;
            case Op::Round: expression = "round(" + a + ")"; break;
            case Op::Modulo: expression = a + " mod " + b; break;
            case Op::Sign: expression = "sgn(" + a + ")"; break;
            case Op::Input: case Op::Constant: case Op::Parameter: case Op::Power: case Op::Exp: break;
        }
        g.setColour (Theme::white()); g.setFont (Theme::font (20)); g.drawFittedText (expression, area, juce::Justification::centredLeft, 1, .45f);
    }
    Node* findNode (int id) { for (auto& node : graph.nodes) if (node.id == id) return &node; return nullptr; }
    const Node* findNode (int id) const { for (const auto& node : graph.nodes) if (node.id == id) return &node; return nullptr; }
    Parameter* findParameter (const juce::String& id) { for (auto& p : graph.parameters) if (p.id == id) return &p; return nullptr; }
    const Parameter* findParameter (const juce::String& id) const { for (const auto& p : graph.parameters) if (p.id == id) return &p; return nullptr; }
    void notifyLayout() { resized(); if (layoutChanged) layoutChanged(); repaint(); }
    void showError (const juce::String& message) { editError = message; status.setColour (juce::Label::textColourId, Theme::red()); status.setText (message, juce::dontSendNotification); }
    void submitProperty (const juce::Identifier& key, const juce::var& value)
    { state.setProperty (key, value, nullptr); if (onChange) onChange (key, value); }
    void compilePrograms()
    {
        program = Program::compile (graph); nodePrograms.clear();
        // Orphan subgraphs also need readouts while the user is wiring them.
        // Compile only on structural edits, never in paint or audio callbacks.
        for (const auto& node : graph.nodes) { auto candidate = graph; candidate.outputNode = node.id; nodePrograms.emplace_back (node.id, Program::compile (candidate)); }
    }
    void refreshReadouts()
    {
        readouts.clear(); const auto values = parameterValues(); const auto trace = program.evaluateTrace (displayX(), values);
        for (const auto& item : nodePrograms)
        {
            if (const auto* n = trace.find (item.first); n && n->status == scrr::params::FormulaTraceStatus::Value) readouts.emplace_back (item.first, scrr::params::FormulaResult { n->value, true, -1 });
            else readouts.emplace_back (item.first, item.second.evaluate (displayX(), values));
        }
    }
    void submitGraph()
    {
        decodeError.clear(); editError.clear(); compilePrograms(); submitProperty ("remapFormula", graph.encode()); refreshPreview();
    }
    void selectNode (int id)
    {
        selectedNode = id; selectedParameter.clear(); if (const auto* node = findNode (id)) selectedParameter = node->parameterId;
        if (expandedNode != id) expandedNode = -1;
        updateDetails(); layoutCanvas(); canvas.repaint();
    }
    bool outside (const juce::Rectangle<float>& bounds) const
    { return ! bounds.intersects (canvas.getLocalBounds().toFloat() * (float) (1 / view.zoom)); }
    void bringOutsideNodes()
    {
        canvas.cancel(); std::vector<Node*> moved;
        for (auto& node : graph.nodes) if (outside (canvas.boundsFor (node))) moved.push_back (&node);
        const bool moveOutput = outside (canvas.outputBounds());
        const int count = (int) moved.size() + (moveOutput ? 1 : 0); if (count == 0) return;
        const double width = canvas.getWidth() / view.zoom, height = canvas.getHeight() / view.zoom;
        const int columns = juce::jmax (1, (int) std::ceil (std::sqrt ((double) count * width / juce::jmax (1.0, height))));
        const int rows = (count + columns - 1) / columns;
        auto position = [&] (int i, double cardWidth, double cardHeight)
        {
            const double availableX = juce::jmax (0.0, width - cardWidth - 24), availableY = juce::jmax (0.0, height - cardHeight - 24);
            return juce::Point<double> (view.panX + 12 + (columns > 1 ? (i % columns) * availableX / (columns - 1) : availableX * .5),
                                        view.panY + 12 + (rows > 1 ? (i / columns) * availableY / (rows - 1) : availableY * .5));
        };
        for (size_t i = 0; i < moved.size(); ++i)
        { const auto b = canvas.boundsFor (*moved[i]); const auto pos = position ((int) i, b.getWidth(), b.getHeight()); moved[i]->canvasX = pos.x; moved[i]->canvasY = pos.y; }
        if (moveOutput) { const auto pos = position ((int) moved.size(), 130, 84); view.outputX = pos.x; view.outputY = pos.y; }
        if (! moved.empty()) { if (moveOutput) submitGraphAndView(); else submitGraph(); } else { submitView(); refreshPreview(); }
    }
    void clearOutsideNodes()
    {
        std::vector<int> removed; for (const auto& node : graph.nodes) if (outside (canvas.boundsFor (node))) removed.push_back (node.id);
        if (removed.empty()) return;
        const auto erased = [&] (int id) { return std::find (removed.begin(), removed.end(), id) != removed.end(); };
        canvas.cancel(); graph.nodes.erase (std::remove_if (graph.nodes.begin(), graph.nodes.end(), [&] (const Node& node) { return erased (node.id); }), graph.nodes.end());
        for (auto& node : graph.nodes) for (auto& input : node.inputs) if (erased (input)) input = -1;
        if (erased (graph.outputNode)) graph.outputNode = -1;
        if (erased (selectedNode)) { selectedNode = expandedNode = -1; selectedParameter.clear(); }
        rebuildParameters(); submitGraph(); updateDetails();
    }
    void generateGraph()
    {
        canvas.cancel(); auto generated = scrr::params::makeRandomRemapFormula (graph.unit, frequencyLimit, view.complexity, juce::Random::getSystemRandom().nextInt64());
        generated.parameters = graph.parameters; graph = std::move (generated); selectedNode = expandedNode = -1; selectedParameter.clear();
        view.panX = view.panY = 0;
        double right = 0, bottom = 0;
        for (const auto& node : graph.nodes) { right = juce::jmax (right, node.canvasX + 210); bottom = juce::jmax (bottom, node.canvasY + 180); }
        view.outputX = right + 60; view.outputY = graph.nodes.empty() ? 80 : graph.nodes.back().canvasY;
        right = view.outputX + 142; bottom = juce::jmax (bottom, view.outputY + 96);
        view.zoom = juce::jlimit (.05, 1.0, juce::jmin (canvas.getWidth() / juce::jmax (1.0, right), canvas.getHeight() / juce::jmax (1.0, bottom)));
        rebuildParameters(); submitGraphAndView(); updateDetails(); layoutCanvas();
    }
    void deleteSelectedNode()
    {
        if (! findNode (selectedNode)) return;
        const int removed = selectedNode;
        graph.nodes.erase (std::remove_if (graph.nodes.begin(), graph.nodes.end(), [removed] (const Node& node) { return node.id == removed; }), graph.nodes.end());
        for (auto& node : graph.nodes) for (auto& input : node.inputs) if (input == removed) input = -1;
        if (graph.outputNode == removed) graph.outputNode = -1;
        if (graph.nodes.empty()) { Node input; input.id = nextId(); graph.nodes.push_back (input); }
        canvas.cancel(); selectedNode = expandedNode = -1; selectedParameter.clear(); rebuildParameters(); submitGraph(); updateDetails(); notifyLayout();
    }
    void deleteSelectedParameter()
    {
        const auto id = selectedParameter; if (! findParameter (id)) return;
        std::vector<int> removed; for (const auto& node : graph.nodes) if (node.op == Op::Parameter && node.parameterId == id) removed.push_back (node.id);
        graph.nodes.erase (std::remove_if (graph.nodes.begin(), graph.nodes.end(), [&] (const Node& node) { return std::find (removed.begin(), removed.end(), node.id) != removed.end(); }), graph.nodes.end());
        for (auto& node : graph.nodes) for (auto& input : node.inputs) if (std::find (removed.begin(), removed.end(), input) != removed.end()) input = -1;
        if (std::find (removed.begin(), removed.end(), graph.outputNode) != removed.end()) graph.outputNode = -1;
        graph.parameters.erase (std::remove_if (graph.parameters.begin(), graph.parameters.end(), [&] (const Parameter& p) { return p.id == id; }), graph.parameters.end());
        if (graph.nodes.empty()) { Node input; input.id = nextId(); graph.nodes.push_back (input); }
        canvas.cancel(); selectedNode = expandedNode = -1; selectedParameter.clear(); rebuildParameters(); submitGraph(); updateDetails(); notifyLayout();
    }
    int nextId() { for (const auto& node : graph.nodes) nextNodeId = juce::jmax (nextNodeId, node.id + 1); return nextNodeId++; }
    int appendNode (Op op, const juce::String& parameter, juce::Point<double> point)
    {
        if (graph.nodes.size() >= Formula::maxNodes) return -1;
        Node node; node.id = nextId(); node.op = op; node.parameterId = parameter; node.value = op == Op::Constant ? 1 : 0;
        node.canvasX = point.x; node.canvasY = point.y;
        graph.nodes.push_back (node); return node.id;
    }
    void showMenu (juce::PopupMenu menu, juce::Point<int> screen, std::function<void(RemapFormulaEditor&, int)> action)
    {
        const auto safe = juce::Component::SafePointer<RemapFormulaEditor> (this); const auto uid = getModuleUid(), revision = graph.encode();
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&canvas).withTargetScreenArea ({ screen.x, screen.y, 1, 1 }), [safe, uid, revision, callback = std::move (action)] (int result)
        {
            if (safe && result > 0 && safe->getModuleUid() == uid && safe->graph.encode() == revision) callback (*safe, result);
        });
    }
    void showCreateMenu (juce::Point<double> point, juce::Point<int> screen) { showCreateMenu (point, screen, Edge {}); }
    void showCreateMenu (juce::Point<double> point, juce::Point<int> screen, Edge edge)
    {
        const bool insert = edge.source >= 0; juce::PopupMenu menu; menu.addSectionHeader (insert ? "Insert node into wire" : "Add node");
        for (int i = (int) Op::Input; i <= (int) Op::Sign; ++i)
        {
            const auto op = (Op) i; const int arity = scrr::params::formulaOpArity (op);
            if (op != Op::Parameter && (! insert || arity > 0)) menu.addItem (i + 1, nodeName (op), graph.nodes.size() + (size_t) (insert ? arity : 1) <= Formula::maxNodes);
        }
        if (! insert)
        {
            menu.addSeparator(); menu.addItem (500, "New parameter", graph.parameters.size() < Formula::maxParameters && graph.nodes.size() < Formula::maxNodes);
            if (! graph.parameters.empty()) { juce::PopupMenu parameters; for (size_t i = 0; i < graph.parameters.size(); ++i) parameters.addItem (100 + (int) i, graph.parameters[i].name, graph.nodes.size() < Formula::maxNodes); menu.addSubMenu ("Existing parameter", parameters); }
        }
        else { menu.addSeparator(); menu.addItem (1300, "Disconnect wire"); }
        showMenu (std::move (menu), screen, [point, edge, insert] (RemapFormulaEditor& self, int result)
        {
            if (result == 1300) { self.disconnect (edge.destination, edge.pin); return; }
            if (result == 500 && ! insert) { self.createParameter (point); return; }
            if (result >= 100 && result < 116 && ! insert)
            {
                const auto index = (size_t) (result - 100); if (index >= self.graph.parameters.size()) return;
                const int id = self.appendNode (Op::Parameter, self.graph.parameters[index].id, point); self.finishCreate (id); return;
            }
            if (result < 1 || result > (int) Op::Sign + 1 || (Op) (result - 1) == Op::Parameter) return;
            const auto op = (Op) (result - 1); const int arity = scrr::params::formulaOpArity (op);
            if (insert && (arity < 1 || self.graph.nodes.size() + (size_t) arity > Formula::maxNodes)) return;
            const auto position = insert ? point - juce::Point<double> (105, 40) : point;
            const int id = self.appendNode (op, {}, position); if (id < 0) return;
            if (insert)
            {
                self.findNode (id)->inputs[0] = edge.source;
                for (int pin = 1; pin < arity; ++pin)
                {
                    const int literal = self.appendNode (Op::Constant, {}, position + juce::Point<double> (-230, 140 * pin));
                    auto* value = self.findNode (literal);
                    value->value = (op == Op::Add || op == Op::Subtract || (op == Op::Clamp && pin == 1)) ? 0 : op == Op::Clamp ? (self.graph.unit == Unit::Hertz ? self.frequencyLimit : 1) : 1;
                    self.findNode (id)->inputs[(size_t) pin] = literal;
                }
                if (edge.destination < 0) self.graph.outputNode = id;
                else if (auto* destination = self.findNode (edge.destination)) destination->inputs[(size_t) edge.pin] = id;
            }
            self.finishCreate (id);
        });
    }
    void finishCreate (int id) { if (id < 0) return; rebuildParameters(); selectNode (id); submitGraph(); updateDetails(); layoutCanvas(); }
    void createParameter (juce::Point<double> point)
    {
        if (graph.parameters.size() >= Formula::maxParameters || graph.nodes.size() >= Formula::maxNodes) return;
        Parameter p; p.id = juce::Uuid().toString().removeCharacters ("-"); int number = 1;
        for (;;) { p.name = "Parameter " + juce::String (number++); bool exists = false; for (const auto& existing : graph.parameters) exists = exists || existing.name.equalsIgnoreCase (p.name); if (! exists) break; }
        p.defaultValue = .5; graph.parameters.push_back (p); submitProperty (p.property(), p.defaultValue);
        const int id = appendNode (Op::Parameter, p.id, point); finishCreate (id); editNode (id);
    }
    void disconnect (int destination, int pin)
    { if (destination < 0) graph.outputNode = -1; else if (auto* node = findNode (destination)) node->inputs[(size_t) pin] = -1; submitGraph(); }
    void showOutputMenu (juce::Point<int> screen)
    {
        juce::PopupMenu menu; menu.addItem (1300, "Disconnect output", graph.outputNode >= 0);
        showMenu (std::move (menu), screen, [] (RemapFormulaEditor& self, int result) { if (result == 1300) self.disconnect (-1, 0); });
    }
    void showNodeContext (int id, juce::Point<int> screen)
    {
        const auto* node = findNode (id); if (! node) return; juce::PopupMenu menu; menu.addSectionHeader (nodeName (node->op));
        if (scrr::params::formulaOpArity (node->op) == 0) menu.addItem (1001, node->op == Op::Parameter ? "Edit value / parameter settings" : node->op == Op::Input ? "Edit preview x" : "Edit value");
        for (int pin = 0; pin < scrr::params::formulaOpArity (node->op); ++pin)
        {
            const auto label = juce::String::charToString ((juce::juce_wchar) ('A' + pin));
            const auto* source = findNode (node->inputs[(size_t) pin]);
            menu.addItem (1100 + pin, (source && (source->op == Op::Constant || source->op == Op::Parameter) ? "Edit " : "Set value for ") + label,
                          (source && (source->op == Op::Constant || source->op == Op::Parameter)) || graph.nodes.size() < Formula::maxNodes);
            menu.addItem (1200 + pin, "Disconnect " + label, source != nullptr);
        }
        menu.addSeparator(); menu.addItem (1003, "Connect to output y", graph.outputNode != id);
        menu.addItem (1002, "Delete node"); if (node->op == Op::Parameter) menu.addItem (1004, "Delete parameter and all its nodes");
        showMenu (std::move (menu), screen, [id] (RemapFormulaEditor& self, int result)
        {
            if (! self.findNode (id)) return; self.selectNode (id);
            if (result == 1001) self.editNode (id);
            else if (result == 1002) self.deleteSelectedNode();
            else if (result == 1003) { self.graph.outputNode = id; self.submitGraph(); }
            else if (result == 1004) self.deleteSelectedParameter();
            else if (result >= 1200 && result < 1203) self.disconnect (id, result - 1200);
            else if (result >= 1100 && result < 1103)
            {
                const int pin = result - 1100; auto* target = self.findNode (id); if (pin >= scrr::params::formulaOpArity (target->op)) return;
                const auto* source = self.findNode (target->inputs[(size_t) pin]);
                if (source && (source->op == Op::Constant || source->op == Op::Parameter)) self.editNode (source->id);
                else
                {
                    const auto position = juce::Point<double> (target->canvasX - 235, target->canvasY + pin * 120);
                    const int literal = self.appendNode (Op::Constant, {}, position); if (literal < 0) return;
                    self.findNode (id)->inputs[(size_t) pin] = literal; self.finishCreate (literal); self.editNode (literal);
                }
            }
        });
    }
    void editNode (int id)
    {
        selectNode (id); const auto* node = findNode (id); if (! node) return;
        if (node->op == Op::Parameter)
        {
            expandedNode = id; updateDetails(); layoutCanvas();
            for (auto& row : parameterRows) if (row->nodeId == id) { revealNode (id); row->slider.showTextBox(); break; }
        }
        else if (node->op == Op::Constant)
        {
            for (auto& item : constants) if (item.first == id) { revealNode (id); item.second->editor.grabKeyboardFocus(); item.second->editor.selectAll(); break; }
        }
        else if (node->op == Op::Input) { showProbe = true; probeDisclosure.setToggleState (true, juce::dontSendNotification); probe.setVisible (true); probeMode.setVisible (true); notifyLayout(); probe.editor.grabKeyboardFocus(); probe.editor.selectAll(); }
    }
    void revealNode (int id)
    {
        const auto* node = findNode (id); if (! node) return;
        if (! canvas.getLocalBounds().toFloat().contains (getNodeBounds (id)))
        { view.panX = node->canvasX - 12 / view.zoom; view.panY = node->canvasY - 12 / view.zoom; layoutCanvas(); submitView(); canvas.repaint(); }
    }
    void rebuildParameters()
    {
        auto oldRows = std::move (parameterRows); parameterRows.clear();
        for (const auto& node : graph.nodes) if (node.op == Op::Parameter) if (const auto* p = findParameter (node.parameterId))
        {
            std::unique_ptr<ParameterRow> row;
            for (auto& existing : oldRows) if (existing && existing->nodeId == node.id && existing->id == p->id) { row = std::move (existing); break; }
            if (! row) row = std::make_unique<ParameterRow> (*this, *p, node.id); else row->configure (*p);
            canvas.addAndMakeVisible (*row); parameterRows.push_back (std::move (row));
        }
        auto oldConstants = std::move (constants); constants.clear();
        for (const auto& node : graph.nodes) if (node.op == Op::Constant)
        {
            std::unique_ptr<Entry> field;
            for (auto& item : oldConstants) if (item.first == node.id) { field = std::move (item.second); break; }
            if (! field)
            {
                const int id = node.id; field = std::make_unique<Entry> ("VALUE", "formula-constant-" + juce::String (id));
                field->onCommit = [this, id]
                {
                    for (auto& item : constants) if (item.first == id)
                    {
                        const double value = item.second->number(); if (! std::isfinite (value)) { showError ("Constant must be a finite number."); return; }
                        if (auto* n = findNode (id)) { n->value = value; submitGraph(); } break;
                    }
                };
            }
            field->set (node.value); canvas.addAndMakeVisible (*field); constants.emplace_back (node.id, std::move (field));
        }
        if (onParametersChanged) onParametersChanged(); layoutCanvas();
    }
    void layoutCanvas()
    {
        const auto transform = juce::AffineTransform::scale ((float) view.zoom);
        canvas.setSize (viewport.getWidth(), viewport.getHeight());
        const auto visible = canvas.getLocalBounds().toFloat() * (float) (1 / view.zoom);
        for (auto& row : parameterRows) if (const auto* n = findNode (row->nodeId))
        {
            const auto b = canvas.boundsFor (*n); const bool showing = b.intersects (visible); row->setVisible (showing);
            if (showing) { row->setBounds (juce::roundToInt (b.getX()) + 12, juce::roundToInt (b.getY()) + 36, 186, row->preferredHeight()); row->setTransform (transform); }
        }
        for (auto& item : constants) if (const auto* n = findNode (item.first))
        {
            const auto b = canvas.boundsFor (*n); const bool showing = b.intersects (visible); item.second->setVisible (showing);
            if (showing) { item.second->setBounds (juce::roundToInt (b.getX()) + 12, juce::roundToInt (b.getY()) + 40, 186, 42); item.second->setTransform (transform); }
        }
        const auto* n = findNode (expandedNode); const bool expanded = n && n->op == Op::Parameter && findParameter (n->parameterId) && canvas.boundsFor (*n).intersects (visible);
        for (auto* field : { &name, &minimum, &maximum, &defaultValue, &step }) { field->setVisible (expanded); field->setTransform (transform); }
        parameterLog.setVisible (expanded); parameterLog.setTransform (transform);
        if (expanded)
        {
            const auto bounds = canvas.boundsFor (*n).toNearestInt(); auto panel = bounds.withY (bounds.getBottom() - 170).withHeight (166).reduced (12, 0);
            name.setBounds (panel.removeFromTop (42)); panel.removeFromTop (4); auto row = panel.removeFromTop (42);
            minimum.setBounds (row.removeFromLeft (90)); row.removeFromLeft (6); maximum.setBounds (row);
            panel.removeFromTop (4); row = panel.removeFromTop (42); defaultValue.setBounds (row.removeFromLeft (90)); row.removeFromLeft (6); step.setBounds (row);
            parameterLog.setBounds (panel.withHeight (22));
        }
    }
    void updateDetails()
    {
        for (auto* field : { &xMin, &xMax, &yMin, &yMax }) field->setVisible (showDetails);
        for (auto* component : std::initializer_list<juce::Component*> { &logX, &logY, &grid, &precision, &nodeDisplay, &nodeLanguage, &invalidPolicy, &complexity }) component->setVisible (showDetails);
        complexity.setValue (view.complexity, juce::dontSendNotification);
        xMin.set (view.xMin); xMax.set (view.xMax); yMin.set (view.yMin); yMax.set (view.yMax); probe.set (view.probeX);
        probe.caption = graph.unit == Unit::Hertz ? "PREVIEW x / Hz" : "PREVIEW x / 0..1 LOG";
        logX.setToggleState (view.logX, juce::dontSendNotification); logY.setToggleState (view.logY, juce::dontSendNotification); grid.setToggleState (view.grid, juce::dontSendNotification);
        precision.setSelectedId (view.samples, juce::dontSendNotification); probeMode.setSelectedId (view.followMouse ? 2 : 1, juce::dontSendNotification);
        nodeDisplay.setSelectedId (view.symbolic ? 2 : 1, juce::dontSendNotification); nodeLanguage.setSelectedId (view.chinese ? 2 : 1, juce::dontSendNotification);
        if (const auto* p = findParameter (selectedParameter))
        { name.set (p->name); minimum.set (p->minimum); maximum.set (p->maximum); defaultValue.set (p->defaultValue); step.set (p->step); parameterLog.setToggleState (p->logarithmic, juce::dontSendNotification); }
    }
    void commitParameterFields()
    {
        auto* p = findParameter (selectedParameter); if (! p) return;
        auto edited = *p; edited.name = name.editor.getText().trim(); edited.minimum = minimum.number(); edited.maximum = maximum.number(); edited.defaultValue = defaultValue.number(); edited.step = step.number(); edited.logarithmic = parameterLog.getToggleState();
        auto validation = graph; for (auto& parameter : validation.parameters) if (parameter.id == p->id) parameter = edited;
        validation.nodes = { Node {} }; validation.outputNode = 0; const auto checked = Program::compile (validation);
        if (! checked.valid) { showError (checked.error); return; }
        *p = edited; const auto value = juce::jlimit (p->minimum, p->maximum, (double) state.getProperty (p->property(), p->defaultValue));
        submitProperty (p->property(), value); submitGraph();
        for (auto& row : parameterRows) if (row->id == p->id) row->configure (*p);
        if (onParametersChanged) onParametersChanged(); updateDetails(); layoutCanvas(); canvas.repaint();
    }
    bool validView() const
    { return std::isfinite (view.xMin) && std::isfinite (view.xMax) && std::isfinite (view.yMin) && std::isfinite (view.yMax) && view.xMax > view.xMin && view.yMax > view.yMin && (! view.logX || view.xMin > 0) && (! view.logY || view.yMin > 0); }
    void commitViewFields()
    {
        if (! std::isfinite (xMin.number()) || ! std::isfinite (xMax.number()) || ! std::isfinite (yMin.number()) || ! std::isfinite (yMax.number())) { showError ("Preview limits must be finite numbers."); return; }
        editError.clear(); view.xMin = xMin.number(); view.xMax = xMax.number(); view.yMin = yMin.number(); view.yMax = yMax.number();
        view.logX = logX.getToggleState(); view.logY = logY.getToggleState(); view.grid = grid.getToggleState(); view.samples = juce::jlimit (64, 1024, precision.getSelectedId()); submitView(); refreshPreview();
    }
    void resetViewRange()
    {
        const bool hertz = graph.unit == Unit::Hertz; view.xMin = view.yMin = hertz ? 20 : 0; view.xMax = view.yMax = hertz ? frequencyLimit : 1; view.logX = view.logY = hertz;
        view.probeX = hoverProbeX = hertz ? 200 : .5;
    }
    void loadView (const juce::String& encoded)
    {
        view = {}; resetViewRange(); const auto parsed = juce::JSON::parse (encoded); const auto* object = parsed.getDynamicObject(); if (! object) return;
        auto number = [&] (const char* key, double fallback) { const auto value = object->getProperty (key); const double n = value.isVoid() ? fallback : (double) value; return std::isfinite (n) ? n : fallback; };
        view.xMin = number ("xMin", view.xMin); view.xMax = number ("xMax", view.xMax); view.yMin = number ("yMin", view.yMin); view.yMax = number ("yMax", view.yMax);
        for (const auto& item : { std::pair<const char*, bool*> { "logX", &view.logX }, { "logY", &view.logY }, { "grid", &view.grid }, { "followMouse", &view.followMouse }, { "symbolic", &view.symbolic }, { "chinese", &view.chinese } }) if (object->hasProperty (item.first)) *item.second = (bool) object->getProperty (item.first);
        view.samples = (int) juce::jlimit (64.0, 1024.0, number ("samples", 256)); view.outputX = number ("outputX", 350); view.outputY = number ("outputY", 80);
        view.panX = number ("panX", 0); view.panY = number ("panY", 0); view.complexity = (int) juce::jlimit (3.0, 64.0, number ("complexity", 8));
        view.zoom = juce::jlimit (.05, 2.0, number ("zoom", .8)); view.probeX = number ("probeX", view.probeX); hoverProbeX = view.probeX;
    }
    juce::String encodedView() const
    {
        auto* object = new juce::DynamicObject;
        object->setProperty ("xMin", view.xMin); object->setProperty ("xMax", view.xMax); object->setProperty ("yMin", view.yMin); object->setProperty ("yMax", view.yMax);
        object->setProperty ("logX", view.logX); object->setProperty ("logY", view.logY); object->setProperty ("grid", view.grid); object->setProperty ("samples", view.samples);
        object->setProperty ("panX", view.panX); object->setProperty ("panY", view.panY); object->setProperty ("complexity", view.complexity);
        object->setProperty ("outputX", view.outputX); object->setProperty ("outputY", view.outputY); object->setProperty ("zoom", view.zoom);
        object->setProperty ("probeX", view.probeX); object->setProperty ("followMouse", view.followMouse); object->setProperty ("symbolic", view.symbolic); object->setProperty ("chinese", view.chinese);
        return juce::JSON::toString (juce::var (object), true);
    }
    void submitView() { submitProperty ("formulaView", encodedView()); }
    void submitGraphAndView()
    {
        decodeError.clear(); editError.clear(); compilePrograms();
        const auto formula = graph.encode(), presentation = encodedView();
        state.setProperty ("remapFormula", formula, nullptr); state.setProperty ("formulaView", presentation, nullptr);
        if (onGraphAndViewChange) onGraphAndViewChange (formula, presentation);
        else if (onChange) { onChange ("remapFormula", formula); onChange ("formulaView", presentation); }
        refreshPreview();
    }
    std::array<double, Formula::maxParameters> parameterValues() const
    {
        std::array<double, Formula::maxParameters> result {};
        for (size_t i = 0; i < graph.parameters.size(); ++i)
        {
            const auto& p = graph.parameters[i]; const double base = (double) state.getProperty (p.property(), p.defaultValue);
            const double value = effectiveValue ? effectiveValue (p.property(), base) : base;
            result[i] = std::isfinite (value) ? juce::jlimit (p.minimum, p.maximum, value) : p.defaultValue;
        }
        return result;
    }
    Surface surface;
    std::unique_ptr<PopoutWindow> popout;
    PopoutButton popoutButton;
    juce::TextButton returnToInspector;
    juce::ValueTree state { "module" }; Formula graph; Program program; View view;
    double frequencyLimit { 20000 }, hoverProbeX { 200 }; juce::String decodeError, editError, selectedParameter;
    int selectedNode { 0 }, expandedNode { -1 }, nextNodeId { 1 }; bool showDetails {}, showProbe {}, sliderGesture {};
    juce::TextButton details, probeDisclosure, randomGraph, home, bringOffscreen, clearOffscreen; juce::Slider complexity; juce::ComboBox units, precision, nodeDisplay, nodeLanguage, probeMode, invalidPolicy;
    juce::ToggleButton logX, logY, grid, parameterLog; juce::Label status, hints; juce::Viewport viewport; Canvas canvas; Preview preview;
    Entry probe { "PREVIEW x / Hz", "formula-probe-x" };
    Entry xMin { "X MIN", "formula-view-x-min" }, xMax { "X MAX", "formula-view-x-max" }, yMin { "Y MIN", "formula-view-y-min" }, yMax { "Y MAX", "formula-view-y-max" };
    Entry name { "NAME", "formula-param-name" }, minimum { "MIN", "formula-param-min" }, maximum { "MAX", "formula-param-max" }, defaultValue { "DEFAULT", "formula-param-default" }, step { "STEP", "formula-param-step" };
    juce::Rectangle<int> detailBounds; std::vector<std::unique_ptr<ParameterRow>> parameterRows;
    std::vector<std::pair<int, std::unique_ptr<Entry>>> constants;
    std::vector<std::pair<int, Program>> nodePrograms;
    std::vector<std::pair<int, scrr::params::FormulaResult>> readouts;
};
} // namespace scrr::gui
