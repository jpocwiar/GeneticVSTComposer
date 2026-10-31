#include "reverbcore/Fdn.hpp"

#include <algorithm>
#include <cmath>

namespace reverbcore
{

namespace
{
    // Mutually incommensurate line lengths at full size, in milliseconds.
    constexpr std::array<double, Fdn8::kLines> kLineMs { 39.5, 47.3, 53.9, 61.1, 69.7, 76.3, 83.9, 97.1 };
    constexpr std::array<double, 4> kDiffuserMs { 4.77, 3.59, 12.73, 9.31 };

    double sizeScale (double size) noexcept { return 0.2 + 0.8 * size; }

    void hadamard (std::array<float, Fdn8::kLines>& v) noexcept
    {
        for (std::size_t len = 1; len < v.size(); len <<= 1)
            for (std::size_t i = 0; i < v.size(); i += 2 * len)
                for (std::size_t j = i; j < i + len; ++j)
                {
                    const float a = v[j];
                    const float b = v[j + len];
                    v[j] = a + b;
                    v[j + len] = a - b;
                }

        constexpr float norm = 0.35355339f; // 1 / sqrt(8): orthonormal

        for (auto& x : v)
            x *= norm;
    }
}

//==============================================================================
void Diffuser::prepare (double sampleRate, double lengthScale)
{
    for (std::size_t i = 0; i < stages.size(); ++i)
    {
        lengths[i] = std::max (2, static_cast<int> (std::lround (kDiffuserMs[i] * 0.001 * lengthScale * sampleRate)));
        stages[i].prepare (lengths[i] + 4);
    }
}

void Diffuser::reset() noexcept
{
    for (auto& s : stages)
        s.clear();
}

float Diffuser::process (float x) noexcept
{
    for (std::size_t i = 0; i < stages.size(); ++i)
    {
        auto& stage = stages[i];
        const float delayed = stage.read (0, lengths[i]);
        const float w = x + kGain * delayed;
        stage.write (0, w);
        stage.advance();
        x = delayed - kGain * w;
    }

    return x;
}

//==============================================================================
void Fdn8::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    for (std::size_t i = 0; i < lines.size(); ++i)
        lines[i].prepare (static_cast<int> (std::ceil (kLineMs[i] * 0.001 * sampleRate)) + 8);

    diffuserL.prepare (sampleRate, 1.0);
    diffuserR.prepare (sampleRate, 1.07);

    sizeCoefficient = 1.0 - std::exp (-1.0 / (0.2 * sampleRate));
    size = targetSize;
    updateLengths();
    updateGains();
    reset();
}

void Fdn8::reset() noexcept
{
    for (auto& line : lines)
        line.clear();

    for (auto& d : damping)
        d.reset();

    diffuserL.reset();
    diffuserR.reset();
}

void Fdn8::setDecay (float rt60Seconds) noexcept
{
    rt60 = std::max (0.05f, rt60Seconds);
    updateGains();
}

void Fdn8::setSize (float newSize) noexcept
{
    targetSize = std::clamp (static_cast<double> (newSize), 0.0, 1.0);
}

void Fdn8::setDamping (float cutoffHz) noexcept
{
    dampingCoefficient = dspkit::OnePole::coefficientFor (cutoffHz, static_cast<float> (sampleRate));
}

void Fdn8::updateLengths() noexcept
{
    for (std::size_t i = 0; i < lines.size(); ++i)
        lengths[i] = std::max (4.0, kLineMs[i] * 0.001 * sizeScale (size) * sampleRate);
}

void Fdn8::updateGains() noexcept
{
    // Each pass through a line of L samples must lose 60 dB over rt60 seconds.
    for (std::size_t i = 0; i < lines.size(); ++i)
        gains[i] = static_cast<float> (std::pow (10.0, -3.0 * lengths[i] / (rt60 * sampleRate)));
}

void Fdn8::process (float inL, float inR, float& outL, float& outR) noexcept
{
    if (std::abs (targetSize - size) > 1.0e-6)
    {
        size += sizeCoefficient * (targetSize - size);
        updateLengths();

        if (--gainCountdown <= 0)
        {
            updateGains();
            gainCountdown = 64;
        }
    }

    const float dl = 0.5f * diffuserL.process (inL);
    const float dr = 0.5f * diffuserR.process (inR);

    std::array<float, kLines> v;

    for (std::size_t i = 0; i < lines.size(); ++i)
        v[i] = damping[i].process (lines[i].read (0, lengths[i]), dampingCoefficient) * gains[i];

    // Decorrelated stereo taps.
    outL = 0.35355339f * (v[0] - v[1] + v[2] - v[3] + v[4] - v[5] + v[6] - v[7]);
    outR = 0.35355339f * (v[0] + v[1] - v[2] - v[3] + v[4] + v[5] - v[6] - v[7]);

    hadamard (v);

    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        lines[i].write (0, v[i] + (i % 2 == 0 ? dl : dr));
        lines[i].advance();
    }
}

} // namespace reverbcore
