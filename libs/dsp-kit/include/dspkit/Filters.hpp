#pragma once

#include <algorithm>
#include <cmath>

namespace dspkit
{

/// Coefficients for a topology-preserving-transform state variable filter
/// (Zavalishin / Cytomic). With q <= 1/sqrt(2) the low- and high-pass gains never exceed 1,
/// which keeps feedback loops from growing.
struct SvfCoefficients
{
    float a1 = 1.0f;
    float a2 = 0.0f;
    float a3 = 0.0f;
    float k = 1.41421356237f;

    static SvfCoefficients make (float cutoffHz, float sampleRate, float q = 0.70710678f) noexcept
    {
        const float cutoff = std::clamp (cutoffHz, 1.0f, 0.49f * sampleRate);
        const float g = std::tan (3.14159265358979323846f * cutoff / sampleRate);

        SvfCoefficients c;
        c.k = 1.0f / q;
        c.a1 = 1.0f / (1.0f + g * (g + c.k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        return c;
    }
};

struct SvfState
{
    float ic1 = 0.0f;
    float ic2 = 0.0f;

    struct Outputs
    {
        float low;
        float high;
    };

    Outputs process (float x, const SvfCoefficients& c) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;

        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        return { v2, x - c.k * v1 - v2 };
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }
};

/// One-pole low-pass, y += (1 - a)(x - y). The coefficient is the pole a in [0, 1).
struct OnePole
{
    float state = 0.0f;

    static float coefficientFor (float cutoffHz, float sampleRate) noexcept
    {
        const float cutoff = std::clamp (cutoffHz, 1.0f, 0.49f * sampleRate);
        return std::exp (-2.0f * 3.14159265358979323846f * cutoff / sampleRate);
    }

    float process (float x, float a) noexcept
    {
        state = x + a * (state - x);
        return state;
    }

    void reset() noexcept { state = 0.0f; }
};

} // namespace dspkit
