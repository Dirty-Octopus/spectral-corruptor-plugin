// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <cmath>
#include "ModulationSource.h"
#include "TransferCurve.h"

namespace scrr::params {
struct MacroMapping
{
    static constexpr int modulationChannel = 5;
    static constexpr int responseChannel = 6;
    static bool hasUniqueSavedRouteId (const juce::ValueTree& tree, const juce::String& id)
    {
        if (id.isEmpty() || id.length() > 64) return false;
        int count = 0;
        for (auto child : tree) if (child.hasType ("MAP") && child["routeUid"].toString() == id && ++count > 1) return false;
        return count == 1;
    }
    enum Mode { unipolarRight, unipolarLeft, bipolarLeftLow, bipolarRightLow };
    int macro {}, channel {}, mode { unipolarRight };
    juce::String uid, label, unit, sourceUid, sourceName, routeUid;
    int sourceIndex { -1 };
    bool sourceEnabled { true };
    int valueIndex() const noexcept { return sourceUid.isEmpty() ? macro : sourceIndex; }
    juce::String sourceLabel() const { return sourceUid.isEmpty() && sourceName.isEmpty() ? "M" + juce::String (macro + 1) : sourceName; }
    bool sameSource (const MacroMapping& other) const { return sourceUid == other.sourceUid && (sourceUid.isNotEmpty() || macro == other.macro); }
    juce::Identifier parameter;
    double low {}, high { 1 }, centre {}, minimum {}, maximum { 1 }, step {};
    bool logarithmic {};
    // Normalised modulation offsets relative to the editable base parameter.
    // Separate endpoints also preserve asymmetric mappings from older presets.
    double startOffset {}, endOffset {};
    TransferCurve responseCurve;
    uint32_t coordinatePointId {};
    bool coordinateXAxis {};
    void prepareCoordinateTarget() { coordinatePointId = 0; parseCurveCoordinateParameter (parameter, coordinatePointId, coordinateXAxis); }
    // Runtime-only, one-level bindings. Coordinate routes use their saved
    // response, so a response curve can never recursively evaluate itself.
    std::vector<MacroMapping> responseMappings;
    void bindResponseMappings (const std::vector<MacroMapping>& all)
    {
        responseMappings.clear();
        prepareCoordinateTarget();
        if (isCurveCoordinateParameter (parameter)) return;
        for (const auto& route : all) if (route.channel == responseChannel && route.uid == routeUid)
        { responseMappings.push_back (route); responseMappings.back().responseMappings.clear(); responseMappings.back().prepareCoordinateTarget(); }
    }
    bool relative { true };
    double normalise (double v) const noexcept
    {
        v = juce::jlimit (minimum, maximum, v);
        if (logarithmic && minimum > 0)
        {
            const double logMinimum = std::log (minimum), span = std::log (maximum) - logMinimum;
            if (span > 0) return juce::jlimit (0.0, 1.0, (std::log (v) - logMinimum) / span);
        }
        return maximum > minimum ? (v - minimum) / (maximum - minimum) : 0;
    }
    double denormalise (double t) const noexcept
    {
        t = juce::jlimit (0.0, 1.0, t);
        double result = logarithmic && minimum > 0 ? std::exp (std::log (minimum) + t * (std::log (maximum) - std::log (minimum))) : minimum + t * (maximum - minimum);
        if (step > 0)
        {
            const double steps = (result - minimum) / step;
            if (std::isfinite (steps)) result = minimum + std::round (steps) * step;
        }
        return juce::jlimit (minimum, maximum, result);
    }
    bool bipolar() const noexcept { return mode >= bipolarLeftLow; }
    double offset (float position, const ModulationValues* values = nullptr) const noexcept
    {
        double t;
        if (values != nullptr && ! responseMappings.empty())
        {
            std::array<TransferCurve::Point, TransferCurve::maxPoints> resolved {};
            const auto count = juce::jmin (resolved.size(), responseCurve.points.size());
            std::copy_n (responseCurve.points.begin(), count, resolved.begin());
            for (size_t i = 0; i < count; ++i)
                for (bool xAxis : { true, false })
                {
                    double sum = 0;
                    for (const auto& route : responseMappings)
                    {
                        const int source = route.valueIndex();
                        if (route.sourceEnabled && source >= 0 && (size_t) source < values->size()
                            && route.coordinatePointId == resolved[i].id && route.coordinateXAxis == xAxis)
                            sum += route.offset ((*values)[(size_t) source]);
                    }
                    if (responseCurve.canMapCoordinate (resolved[i].id, xAxis, true, bipolar()))
                        (xAxis ? resolved[i].x : resolved[i].y) = (float) juce::jlimit (0.0, 1.0, (double) (xAxis ? resolved[i].x : resolved[i].y) + sum);
                }
            TransferCurve::constrainCoordinates (resolved.data(), count, true, bipolar(), responseCurve.points.data());
            t = TransferCurve::evaluatePoints (resolved.data(), count, position, bipolar());
        }
        else t = responseCurve.evaluate (position, bipolar());
        return bipolar() ? (t <= .5 ? startOffset * (1 - 2 * t) : endOffset * (2 * t - 1))
                         : startOffset + (endOffset - startOffset) * t;
    }
    void setDepth (double depth, bool bi) noexcept
    {
        endOffset = juce::jlimit (-1.0, 1.0, depth); startOffset = bi ? -endOffset : 0;
        mode = bi ? bipolarLeftLow : unipolarRight; relative = true;
        syncRange();
    }
    void syncRange() noexcept
    { low = denormalise (normalise (centre) + startOffset); high = denormalise (normalise (centre) + endOffset); }
    void readResponseCurve (const juce::ValueTree& tree)
    { responseCurve = TransferCurve::decode (tree["responseCurve"].toString()); responseCurve.sanitise (true, bipolar()); }
    bool sameResponseCurve (const MacroMapping& other) const noexcept { return responseCurve.sameAs (other.responseCurve); }
    double fromBase (double base, double modulation) const noexcept { return denormalise (normalise (base) + modulation); }
    bool sameTarget (const MacroMapping& other) const
    { return channel == other.channel && uid == other.uid && parameter == other.parameter; }
    double interpolate (double a, double b, double t) const noexcept
    { return logarithmic && a > 0 && b > 0 ? std::exp (std::log (a) + (std::log (b) - std::log (a)) * t) : a + (b - a) * t; }
    double value (float position) const noexcept
    {
        if (relative) return fromBase (centre, offset (position));
        double t = std::isfinite (position) ? juce::jlimit (0.0, 1.0, (double) position) : 0;
        if (mode == unipolarLeft || mode == bipolarRightLow) t = 1 - t;
        double result = mode <= unipolarLeft ? interpolate (low, high, t)
                         : t <= .5 ? interpolate (low, centre, t * 2) : interpolate (centre, high, (t - .5) * 2);
        if (step > 0)
        {
            const double steps = (result - minimum) / step;
            if (std::isfinite (steps)) result = minimum + std::round (steps) * step;
        }
        return juce::jlimit (minimum, maximum, result);
    }
    juce::ValueTree state() const
    {
        juce::ValueTree tree ("MAP");
        tree.setProperty ("source", sourceUid, nullptr);
        tree.setProperty ("macro", macro, nullptr); tree.setProperty ("channel", channel, nullptr);
        tree.setProperty ("uid", uid, nullptr); tree.setProperty ("parameter", parameter.toString(), nullptr);
        tree.setProperty ("routeUid", routeUid, nullptr);
        tree.setProperty ("low", low, nullptr); tree.setProperty ("high", high, nullptr);
        tree.setProperty ("centre", centre, nullptr); tree.setProperty ("mode", mode, nullptr);
        tree.setProperty ("version", 4, nullptr);
        tree.setProperty ("startOffset", startOffset, nullptr); tree.setProperty ("endOffset", endOffset, nullptr);
        tree.setProperty ("responseCurve", responseCurve.encode(), nullptr);
        return tree;
    }
};
using MacroValues = std::array<float, 8>;
}
