// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Dirty Octopus

#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <array>
#include <cstdint>

namespace scrr::params {

inline juce::Identifier curveCoordinateParameter (uint32_t id, bool xAxis)
{ return juce::Identifier ("point." + juce::String ((juce::int64) id) + (xAxis ? ".x" : ".y")); }
inline bool parseCurveCoordinateParameter (const juce::Identifier& key, uint32_t& id, bool& xAxis)
{
    const auto text = key.toString();
    if (! text.startsWith ("point.") || (! text.endsWith (".x") && ! text.endsWith (".y"))) return false;
    const auto digits = text.substring (6, text.length() - 2);
    const auto value = digits.getLargeIntValue();
    if (digits.isEmpty() || ! digits.containsOnly ("0123456789") || value <= 0 || value > 2147483646) return false;
    id = (uint32_t) value; xAxis = text.endsWith (".x"); return true;
}
inline bool isCurveCoordinateParameter (const juce::Identifier& key)
{ uint32_t id {}; bool x {}; return parseCurveCoordinateParameter (key, id, x); }

/** A bounded, non-periodic input/output curve. Editing and decoding happen off
    the audio thread; evaluation performs no allocation. */
struct TransferCurve
{
    struct Point { float x {}, y {}, curve {}; uint32_t id {}; };
    static constexpr size_t maxPoints = 64;
    std::vector<Point> points { { 0, 0, 0, 1 }, { 1, 1, 0, 2 } };
    uint32_t nextPointId { 3 };

    uint32_t allocatePointId() noexcept { return nextPointId < 2147483646 ? nextPointId++ : 0; }
    void reset() { points = { { 0, 0, 0, allocatePointId() }, { 1, 1, 0, allocatePointId() } }; }
    int pointIndex (uint32_t id) const noexcept
    { for (size_t i = 0; i < points.size(); ++i) if (points[i].id == id) return (int) i; return -1; }
    bool setCoordinate (uint32_t id, bool xAxis, double value, bool fixedEndValues = false, bool pinMidpoint = false)
    {
        if (! canMapCoordinate (id, xAxis, fixedEndValues, pinMidpoint) || ! std::isfinite (value)) return false;
        const auto index = (size_t) pointIndex (id); auto& point = points[index];
        if (xAxis) point.x = (float) juce::jlimit ((double) points[index - 1].x + .001, (double) points[index + 1].x - .001, value);
        else point.y = (float) juce::jlimit (pinMidpoint && point.x > .5f ? .5 : 0.0, pinMidpoint && point.x < .5f ? .5 : 1.0, value);
        return true;
    }
    bool canMapCoordinate (uint32_t id, bool xAxis, bool fixedEndValues = false, bool pinMidpoint = false) const noexcept
    {
        const int index = pointIndex (id); if (index < 0) return false;
        if (pinMidpoint && std::abs (points[(size_t) index].x - .5f) < .000001f) return false;
        return index != 0 && index + 1 != (int) points.size() ? true : ! xAxis && ! fixedEndValues;
    }
    // Runtime projection preserves point identity/order and never allocates.
    static void constrainCoordinates (Point* data, size_t count, bool fixedEndValues = false, bool pinMidpoint = false,
                                      const Point* base = nullptr) noexcept
    {
        if (count < 2) return;
        data[0].x = 0; data[count - 1].x = 1;
        if (fixedEndValues) { data[0].y = 0; data[count - 1].y = 1; }
        for (size_t i = 0; i < count; ++i) data[i].y = juce::jlimit (0.0f, 1.0f, std::isfinite (data[i].y) ? data[i].y : 0);
        if (pinMidpoint && base)
            for (size_t i = 1; i + 1 < count; ++i) if (std::abs (base[i].x - .5f) < .000001f) data[i].x = .5f;
        const auto fixed = [&] (size_t index)
        {
            return index == 0 || index + 1 == count
                || (base && std::abs (data[index].x - base[index].x) <= 0);
        };
        // Unchanged nodes form fixed anchors. Adjacent moving nodes share the
        // space between their nearest anchors, preserving IDs without pushing
        // an unassigned neighbour when a requested position crosses it.
        for (size_t left = 0; left + 1 < count;)
        {
            size_t right = left + 1;
            while (! fixed (right)) ++right;
            for (size_t i = left + 1; i < right; ++i)
            {
                const float lower = data[i - 1].x + .001f, upper = data[right].x - .001f * (float) (right - i);
                data[i].x = juce::jlimit (juce::jmin (lower, upper), upper,
                    std::isfinite (data[i].x) ? data[i].x : lower);
            }
            left = right;
        }
        if (pinMidpoint)
            for (size_t i = 0; i < count; ++i)
                data[i].y = data[i].x < .5f ? juce::jlimit (0.0f, .5f, data[i].y) : data[i].x > .5f ? juce::jlimit (.5f, 1.0f, data[i].y) : .5f;
    }

    static double between (Point a, Point b, double x) noexcept
    {
        const double t = juce::jlimit (0.0, 1.0, (x - a.x) / juce::jmax (0.000001, (double) b.x - a.x));
        const double bend = std::abs (a.curve) < 0.000001f ? t
            : a.curve < 0 ? std::pow (t, 1.0 - 7.0 * a.curve)
                          : 1.0 - std::pow (1.0 - t, 1.0 + 7.0 * a.curve);
        return juce::jlimit (0.0, 1.0, a.y + (b.y - a.y) * bend);
    }

    double evaluate (double position, bool pinMidpoint = false) const noexcept
    { return evaluatePoints (points.data(), points.size(), position, pinMidpoint); }
    static double evaluatePoints (const Point* data, size_t count, double position, bool pinMidpoint = false) noexcept
    {
        struct View { const Point* data; size_t count; size_t size() const { return count; } const Point& front() const { return data[0]; } const Point& back() const { return data[count - 1]; } const Point& operator[] (size_t i) const { return data[i]; } const Point* begin() const { return data; } const Point* end() const { return data + count; } };
        const View points { data, count };
        const double x = std::isfinite (position) ? juce::jlimit (0.0, 1.0, position) : 0.0;
        if (points.size() < 2) return x;
        if (! pinMidpoint)
        {
            if (x <= points.front().x) return points.front().y;
            for (size_t i = 1; i < points.size(); ++i)
                if (x <= points[i].x) return between (points[i - 1], points[i], x);
            return points.back().y;
        }

        // A bipolar route always crosses its unchanged base at source 50%.
        // Use a virtual centre even when a legacy/unipolar curve has no node.
        if (x == 0.5) return 0.5;
        if (x < 0.5)
        {
            Point a { 0, 0, points.front().curve };
            for (size_t i = 1; i < points.size(); ++i)
            {
                auto b = points[i];
                if (b.x >= 0.5f) return between (a, { 0.5f, 0.5f, 0 }, x);
                b.y = juce::jlimit (0.0f, 0.5f, b.y);
                if (x <= b.x) return between (a, b, x);
                a = b;
            }
        }
        else
        {
            Point a { 0.5f, 0.5f, 0 };
            for (auto b : points)
            {
                if (b.x < 0.5f) continue;
                if (b.x == 0.5f) { a.curve = b.curve; continue; }
                b.y = juce::jlimit (0.5f, 1.0f, b.y);
                if (x <= b.x) return between (a, b, x);
                a = b;
            }
        }
        return x;
    }

    void sanitise (bool fixedEndValues = false, bool pinMidpoint = false)
    {
        std::vector<Point> clean;
        clean.reserve (maxPoints);
        for (auto p : points)
        {
            if (clean.size() >= maxPoints || ! std::isfinite (p.x) || ! std::isfinite (p.y)) continue;
            p.x = juce::jlimit (0.0f, 1.0f, p.x); p.y = juce::jlimit (0.0f, 1.0f, p.y);
            p.curve = std::isfinite (p.curve) ? juce::jlimit (-1.0f, 1.0f, p.curve) : 0.0f;
            clean.push_back (p);
        }
        std::stable_sort (clean.begin(), clean.end(), [] (auto a, auto b) { return a.x < b.x; });
        points.clear();
        for (auto p : clean)
            if (points.empty() || p.x - points.back().x >= 0.001f) points.push_back (p);
        if (points.size() < 2) { reset(); return; }
        points.front().x = 0; points.back().x = 1;
        if (fixedEndValues) { points.front().y = 0; points.back().y = 1; }
        if (pinMidpoint)
        {
            float centreCurve = 0;
            for (auto p : points) if (std::abs (p.x - 0.5f) < 0.001f) { centreCurve = p.curve; break; }
            // Remove both close neighbours before inserting the pinned point;
            // snapping only one could leave an undraggable sub-millistep gap.
            points.erase (std::remove_if (points.begin(), points.end(), [] (auto p) { return std::abs (p.x - 0.5f) < 0.001f; }), points.end());
            if (points.size() >= maxPoints) points.erase (points.end() - 2);
            uint32_t centreId = 0;
            for (auto p : clean) if (std::abs (p.x - .5f) < .001f) { centreId = p.id; break; }
            points.insert (std::lower_bound (points.begin(), points.end(), 0.5f, [] (auto p, float x) { return p.x < x; }), { 0.5f, 0.5f, centreCurve, centreId });
            for (auto& p : points)
                p.y = p.x < 0.5f ? juce::jlimit (0.0f, 0.5f, p.y) : p.x > 0.5f ? juce::jlimit (0.5f, 1.0f, p.y) : 0.5f;
        }
        for (auto p : points) if (p.id < 2147483646) nextPointId = juce::jmax (nextPointId, p.id + 1);
        for (size_t i = 0; i < points.size(); ++i)
        {
            bool duplicate = false;
            for (size_t j = 0; j < i; ++j) duplicate = duplicate || points[j].id == points[i].id;
            if (points[i].id == 0 || points[i].id >= 2147483646 || duplicate) points[i].id = allocatePointId();
        }
    }

    bool sameAs (const TransferCurve& other) const noexcept
    {
        if (points.size() != other.points.size()) return false;
        for (size_t i = 0; i < points.size(); ++i)
            if (std::abs (points[i].x - other.points[i].x) > 0 || std::abs (points[i].y - other.points[i].y) > 0
                || std::abs (points[i].curve - other.points[i].curve) > 0) return false;
        return true;
    }
    bool sameStateAs (const TransferCurve& other) const noexcept
    {
        if (nextPointId != other.nextPointId || ! sameAs (other)) return false;
        for (size_t i = 0; i < points.size(); ++i) if (points[i].id != other.points[i].id) return false;
        return true;
    }

    juce::String encode() const
    {
        auto canonical = *this; canonical.sanitise();
        juce::String result ("2," + juce::String ((juce::int64) canonical.nextPointId));
        for (auto p : canonical.points)
            result += ";" + juce::String (p.x, 9) + "," + juce::String (p.y, 9) + "," + juce::String (p.curve, 9) + "," + juce::String ((juce::int64) p.id);
        return result;
    }

    static TransferCurve decode (const juce::String& text)
    {
        TransferCurve result;
        if (text.isEmpty() || text.length() > 8192) return result;
        const auto records = juce::StringArray::fromTokens (text, ";", "");
        if (records.isEmpty() || records.size() > (int) maxPoints + 1) return result;
        const bool legacy = records[0] == "1";
        if (! legacy)
        {
            if (! records[0].startsWith ("2,")) return result;
            const auto next = records[0].substring (2);
            if (! next.containsOnly ("0123456789") || next.getLargeIntValue() < 1 || next.getLargeIntValue() > 2147483646) return result;
            result.nextPointId = (uint32_t) next.getLargeIntValue();
        }
        std::vector<Point> decoded;
        for (int i = 1; i < records.size(); ++i)
        {
            const auto fields = juce::StringArray::fromTokens (records[i], ",", "");
            if (fields.size() != (legacy ? 3 : 4)) return TransferCurve {};
            float values[3] {};
            for (int field = 0; field < 3; ++field)
            {
                const auto raw = fields[field].trim().toStdString(); char* end {};
                const double value = std::strtod (raw.c_str(), &end);
                if (raw.empty() || end == raw.c_str() || *end != 0 || ! std::isfinite (value)) return result;
                values[field] = (float) juce::jlimit (field == 2 ? -1.0 : 0.0, 1.0, value);
            }
            uint32_t id = 0;
            if (! legacy)
            {
                if (! fields[3].containsOnly ("0123456789") || fields[3].getLargeIntValue() < 1 || fields[3].getLargeIntValue() >= 2147483646) return TransferCurve {};
                id = (uint32_t) fields[3].getLargeIntValue();
            }
            decoded.push_back ({ values[0], values[1], values[2], id });
        }
        result.points = std::move (decoded); result.sanitise(); return result;
    }
};
}
