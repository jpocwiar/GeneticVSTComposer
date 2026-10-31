#pragma once

#include <dspkit/DelayLine.hpp>
#include <dspkit/Filters.hpp>

#include <array>

namespace reverbcore
{

/// Schroeder allpass diffuser: four allpasses in series smear a hit into a dense onset.
class Diffuser
{
public:
    void prepare (double sampleRate, double lengthScale);
    void reset() noexcept;
    float process (float x) noexcept;

private:
    static constexpr float kGain = 0.6f;

    std::array<dspkit::MonoDelay, 4> stages;
    std::array<int, 4> lengths {};
};

/// Eight-line feedback delay network. The Hadamard feedback matrix is orthogonal, so the loop
/// never adds energy; per-line gains below 1 set the decay (RT60) and a one-pole low-pass per
/// line darkens the tail. Allocates in prepare() only.
class Fdn8
{
public:
    static constexpr int kLines = 8;

    void prepare (double sampleRate);
    void reset() noexcept;

    void setDecay (float rt60Seconds) noexcept;
    void setSize (float size) noexcept; // 0..1; the line lengths glide to it
    void setDamping (float cutoffHz) noexcept;

    void process (float inL, float inR, float& outL, float& outR) noexcept;

private:
    void updateLengths() noexcept;
    void updateGains() noexcept;

    double sampleRate = 48000.0;
    std::array<dspkit::MonoDelay, kLines> lines;
    std::array<dspkit::OnePole, kLines> damping;
    std::array<float, kLines> gains {};
    std::array<double, kLines> lengths {};

    double size = 0.7;
    double targetSize = 0.7;
    double sizeCoefficient = 0.0;
    float rt60 = 2.0f;
    float dampingCoefficient = 0.0f;
    int gainCountdown = 0;

    Diffuser diffuserL, diffuserR;
};

} // namespace reverbcore
