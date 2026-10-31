#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace dspkit
{

/// Circular buffer with 3rd-order Lagrange fractional reads.
/// Memory is allocated only in prepare(); read/write/advance never allocate.
template <int NumChannels>
class MultiDelay
{
public:
    static constexpr int kChannels = NumChannels;
    static constexpr double kMinDelay = 2.0; // the interpolator needs one newer sample

    void prepare (int maxDelaySamples)
    {
        int size = 1;

        while (size < maxDelaySamples + kInterpolationMargin)
            size <<= 1;

        for (auto& channel : channels)
            channel.assign (static_cast<std::size_t> (size), 0.0f);

        mask = size - 1;
        maxDelay = maxDelaySamples;
        writeIndex = 0;
    }

    void clear() noexcept
    {
        for (auto& channel : channels)
            std::fill (channel.begin(), channel.end(), 0.0f);
    }

    int getMaxDelay() const noexcept { return maxDelay; }

    /// Sample written `delay` samples ago, for kMinDelay <= delay <= getMaxDelay().
    /// Delay 0 is the slot at the write position (the current input once written).
    float read (int channel, double delay) const noexcept
    {
        const auto whole = static_cast<int> (delay);
        const auto f = static_cast<float> (delay - whole);
        const auto* data = channels[static_cast<std::size_t> (channel)].data();
        const int base = writeIndex - whole;

        const float xNewer = data[(base + 1) & mask];
        const float x0     = data[base & mask];
        const float x1     = data[(base - 1) & mask];
        const float x2     = data[(base - 2) & mask];

        // Lagrange weights for nodes at -1, 0, 1, 2, evaluated at f in [0, 1).
        // f == 0 gives weights (0, 1, 0, 0), so integer delays are bit-exact.
        const float fp1 = f + 1.0f;
        const float fm1 = f - 1.0f;
        const float fm2 = f - 2.0f;

        const float cNewer = -f * fm1 * fm2 * (1.0f / 6.0f);
        const float c0     = fp1 * fm1 * fm2 * 0.5f;
        const float c1     = -fp1 * f * fm2 * 0.5f;
        const float c2     = fp1 * f * fm1 * (1.0f / 6.0f);

        return cNewer * xNewer + c0 * x0 + c1 * x1 + c2 * x2;
    }

    void write (int channel, float value) noexcept
    {
        channels[static_cast<std::size_t> (channel)][static_cast<std::size_t> (writeIndex)] = value;
    }

    void advance() noexcept { writeIndex = (writeIndex + 1) & mask; }

private:
    static constexpr int kInterpolationMargin = 4;

    std::array<std::vector<float>, NumChannels> channels;
    int mask = 0;
    int writeIndex = 0;
    int maxDelay = 0;
};

using DelayLine = MultiDelay<2>;
using MonoDelay = MultiDelay<1>;

} // namespace dspkit
