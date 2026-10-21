// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include "../Theme.h"
#include "../../parameters/TransferCurve.h"
#include <functional>

namespace scrr::gui {

/** Non-periodic input/output editor shared by modulation and frequency maps. */
class TransferCurveEditor : public juce::Component, public juce::SettableTooltipClient
{
public:
    using Curve = scrr::params::TransferCurve;
    std::function<void(Curve)> onChange;
    std::function<void(juce::Component&, uint32_t)> nodeMenu;

    TransferCurveEditor()
    {
        setWantsKeyboardFocus (true);
        setTooltip ("Double-click empty space to add a point. Right-click a point to assign X/Y modulation. Double-click a point or press Delete to remove it. Drag a hollow midpoint to bend a segment. Shift-drag for fine adjustment.");
    }
    void setCurve (const Curve& next) { curve = next; curve.sanitise (fixedEnds, pinCentre); repaint(); }
    void setPreviewCurve (const Curve& next)
    {
        previewCurve = next;
        Curve::constrainCoordinates (previewCurve.points.data(), previewCurve.points.size(), fixedEnds, pinCentre,
                                    curve.points.size() == previewCurve.points.size() ? curve.points.data() : nullptr);
        hasPreview = true; repaint();
    }
    void clearPreviewCurve() { if (hasPreview) { hasPreview = false; repaint(); } }
    const Curve& getCurve() const noexcept { return curve; }
    bool canMapCoordinate (uint32_t id, bool xAxis) const noexcept { return curve.canMapCoordinate (id, xAxis, fixedEnds, pinCentre); }
    void setConstraints (bool fixedEndValues, bool pinMidpoint)
    { fixedEnds = fixedEndValues; pinCentre = pinMidpoint; curve.sanitise (fixedEnds, pinCentre); repaint(); }
    void setAxisLabels (juce::String x, juce::String y)
    { xLabel = std::move (x); yLabel = std::move (y); repaint(); }
    void setEndpointLabels (juce::String first, juce::String last)
    { lowLabel = std::move (first); highLabel = std::move (last); repaint(); }
    void setCoordinateFormatter (std::function<juce::String(float, float)> formatter)
    { coordinateFormatter = std::move (formatter); repaint(); }
    void reset() { curve.reset(); curve.sanitise (fixedEnds, pinCentre); selected = segment = -1; submit(); }
    bool isEditing() const noexcept { return dragging; }
    juce::Rectangle<float> getPlotBounds() const { return getLocalBounds().toFloat().reduced (14, 0).withTrimmedTop (25).withTrimmedBottom (23); }

    void paint (juce::Graphics& g) override
    {
        const auto area = getPlotBounds();
        g.fillAll (Theme::field());
        Theme::text (g, yLabel, { 14, 3, getWidth() / 2, 18 }, 10, Theme::dim(), true);
        Theme::text (g, xLabel + "  >", { getWidth() / 2, getHeight() - 21, getWidth() / 2 - 14, 18 }, 10, Theme::dim(), true, juce::Justification::centredRight);
        g.setColour (Theme::line());
        for (int i = 0; i <= 4; ++i)
        {
            g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / 4), area.getY(), area.getBottom());
            g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * (float) i / 4), area.getX(), area.getRight());
        }
        g.setColour (Theme::dim().withAlpha (0.35f));
        const juce::Line<float> diagonal (area.getBottomLeft(), area.getTopRight());
        const float dash[] { 3, 4 }; g.drawDashedLine (diagonal, dash, 2, 1.0f);
        if (pinCentre)
        {
            g.setColour (Theme::yellow().withAlpha (0.3f));
            g.drawVerticalLine ((int) area.getCentreX(), area.getY(), area.getBottom());
            g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
        }
        juce::Path path;
        for (int i = 0; i <= 256; ++i)
        {
            const float x = (float) i / 256.0f; const auto p = screenPoint (x, (float) curve.evaluate (x, pinCentre));
            if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
        }
        g.setColour (Theme::yellow()); g.strokePath (path, juce::PathStrokeType (2.0f));
        if (hasPreview && ! previewCurve.sameAs (curve))
        {
            juce::Path preview;
            for (int i = 0; i <= 256; ++i)
            {
                const float x = (float) i / 256.0f;
                const auto point = screenPoint (x, (float) previewCurve.evaluate (x, pinCentre));
                if (i == 0) preview.startNewSubPath (point); else preview.lineTo (point);
            }
            g.setColour (Theme::blue()); g.strokePath (preview, juce::PathStrokeType (1.5f));
        }
        for (size_t i = 0; i + 1 < curve.points.size(); ++i)
        {
            const auto h = juce::Rectangle<float> (7, 7).withCentre (handle ((int) i));
            g.setColour (Theme::field()); g.fillEllipse (h);
            g.setColour ((int) i == segment || (int) i == hoverSegment ? Theme::white() : Theme::yellow()); g.drawEllipse (h, 1.2f);
        }
        for (size_t i = 0; i < curve.points.size(); ++i)
        {
            const auto p = curve.points[i]; const auto box = juce::Rectangle<float> (7, 7).withCentre (screenPoint (p.x, p.y));
            g.setColour ((int) i == selected ? Theme::white() : isPinned ((int) i) ? Theme::dim() : Theme::yellow()); g.fillRect (box);
        }
        Theme::text (g, lowLabel, { 14, getHeight() - 21, 80, 18 }, 9, Theme::dim());
        const int readoutPoint = dragging && selected >= 0 ? selected : hoverPoint >= 0 ? hoverPoint : hoverX < 0 ? selected : -1;
        float input = hoverX, output = input >= 0 ? (float) curve.evaluate (input, pinCentre) : 0;
        if (readoutPoint >= 0 && readoutPoint < (int) curve.points.size())
        { input = curve.points[(size_t) readoutPoint].x; output = curve.points[(size_t) readoutPoint].y; }
        const auto coordinates = input < 0 ? highLabel : coordinateFormatter ? coordinateFormatter (input, output)
            : juce::String (input * 100, 1) + "% > " + juce::String (output * 100, 1) + "%";
        Theme::text (g, coordinates, { getWidth() / 2, 3, getWidth() / 2 - 14, 18 }, 9,
                     input < 0 ? Theme::dim() : Theme::white(), true, juce::Justification::centredRight);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        selected = nearestPoint (e.position); segment = -1; dragging = false;
        if (e.mods.isPopupMenu()) { if (selected >= 0 && nodeMenu) nodeMenu (*this, curve.points[(size_t) selected].id); return; }
        if (selected < 0) segment = nearestHandle (e.position);
        if (selected < 0 && segment < 0) { repaint(); return; }
        dragging = true; dragStart = e.position;
        if (segment >= 0) startCurve = curve.points[(size_t) segment].curve;
        else startPoint = curve.points[(size_t) selected];
        if (segment >= 0) hoverX = (curve.points[(size_t) segment].x + curve.points[(size_t) segment + 1].x) * 0.5f;
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || e.mods.isPopupMenu()) return;
        if (segment >= 0 && segment + 1 < (int) curve.points.size())
        {
            auto& a = curve.points[(size_t) segment]; const auto b = curve.points[(size_t) segment + 1];
            const float direction = b.y >= a.y ? 1.0f : -1.0f;
            a.curve = juce::jlimit (-1.0f, 1.0f, startCurve + direction * (dragStart.y - e.position.y) / (e.mods.isShiftDown() ? 1000.0f : 150.0f));
            submit(); return;
        }
        if (selected < 0 || selected >= (int) curve.points.size() || isPinned (selected)) return;
        auto& p = curve.points[(size_t) selected]; const auto area = getPlotBounds();
        const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
        const float y = startPoint.y + (dragStart.y - e.position.y) / area.getHeight() * fine;
        p.y = juce::jlimit (pinCentre && p.x > 0.5f ? 0.5f : 0.0f, pinCentre && p.x < 0.5f ? 0.5f : 1.0f, y);
        if (selected > 0 && selected + 1 < (int) curve.points.size())
            p.x = juce::jlimit (curve.points[(size_t) selected - 1].x + 0.001f, curve.points[(size_t) selected + 1].x - 0.001f,
                               startPoint.x + (e.position.x - dragStart.x) / area.getWidth() * fine);
        submit();
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; segment = -1; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        dragging = false; segment = -1; selected = nearestPoint (e.position);
        if (selected >= 0) { removeSelected(); return; }
        const auto area = getPlotBounds();
        if (! area.contains (e.position) || curve.points.size() >= Curve::maxPoints) return;
        const float x = juce::jlimit (0.001f, 0.999f, (e.position.x - area.getX()) / area.getWidth());
        for (auto p : curve.points) if (std::abs (p.x - x) < 0.002f) return;
        float y = juce::jlimit (0.0f, 1.0f, (area.getBottom() - e.position.y) / area.getHeight());
        if (pinCentre) y = x < 0.5f ? juce::jlimit (0.0f, 0.5f, y) : juce::jlimit (0.5f, 1.0f, y);
        const auto position = std::lower_bound (curve.points.begin(), curve.points.end(), x, [] (auto p, float value) { return p.x < value; });
        selected = (int) std::distance (curve.points.begin(), position);
        curve.points.insert (position, { x, y, 0, curve.allocatePointId() }); submit();
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto area = getPlotBounds(); hoverPoint = nearestPoint (e.position);
        hoverX = area.contains (e.position) ? juce::jlimit (0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth()) : -1.0f;
        const int next = hoverPoint < 0 ? nearestHandle (e.position) : -1;
        if (next != hoverSegment) { hoverSegment = next; setMouseCursor (next >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor); repaint(); }
        repaint();
    }
    void mouseExit (const juce::MouseEvent&) override { hoverSegment = hoverPoint = -1; hoverX = -1; repaint(); }
    bool keyPressed (const juce::KeyPress& key) override
    { if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { removeSelected(); return true; } return false; }

private:
    juce::Point<float> screenPoint (float x, float y) const
    { const auto a = getPlotBounds(); return { a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight() }; }
    juce::Point<float> handle (int index) const
    { const float x = (curve.points[(size_t) index].x + curve.points[(size_t) index + 1].x) * 0.5f; return screenPoint (x, (float) curve.evaluate (x, pinCentre)); }
    bool isPinned (int index) const
    { return (fixedEnds && (index == 0 || index + 1 == (int) curve.points.size())) || (pinCentre && curve.points[(size_t) index].x == 0.5f); }
    int nearestPoint (juce::Point<float> position) const
    {
        int nearest = -1; float distance = 10;
        for (size_t i = 0; i < curve.points.size(); ++i)
        { const auto p = curve.points[i]; const float d = screenPoint (p.x, p.y).getDistanceFrom (position); if (d < distance) { distance = d; nearest = (int) i; } }
        return nearest;
    }
    int nearestHandle (juce::Point<float> position) const
    { for (size_t i = 0; i + 1 < curve.points.size(); ++i) if (handle ((int) i).getDistanceFrom (position) < 8) return (int) i; return -1; }
    void removeSelected()
    {
        if (selected <= 0 || selected + 1 >= (int) curve.points.size() || isPinned (selected)) return;
        curve.points.erase (curve.points.begin() + selected); selected = -1; submit();
    }
    void submit() { repaint(); if (onChange) onChange (curve); }

    Curve curve, previewCurve;
    juce::String xLabel { "SOURCE" }, yLabel { "RESPONSE" }, lowLabel { "0%" }, highLabel { "100%" };
    std::function<juce::String(float, float)> coordinateFormatter;
    bool fixedEnds {}, pinCentre {}, dragging {}, hasPreview {};
    int selected { -1 }, segment { -1 }, hoverSegment { -1 }, hoverPoint { -1 };
    juce::Point<float> dragStart;
    Curve::Point startPoint;
    float startCurve {}, hoverX { -1 };
};
}
