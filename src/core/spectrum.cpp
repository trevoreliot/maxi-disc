// SPDX-License-Identifier: GPL-3.0-or-later
#include "spectrum.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace mmm {

std::array<double, kSpectrumBandCount + 1> spectrumBandEdges(double minFreqHz, double maxFreqHz)
{
    std::array<double, kSpectrumBandCount + 1> edges{};
    const double ratio = maxFreqHz / minFreqHz;
    for (std::size_t i = 0; i < edges.size(); ++i)
        edges[i] = minFreqHz * std::pow(ratio, static_cast<double>(i) / kSpectrumBandCount);
    return edges;
}

SpectrumLevels mapSpectrumToBands(std::span<const float> bins, int sampleRateHz, const SpectrumMapping &m)
{
    SpectrumLevels out{};
    const std::size_t n = bins.size();
    if (n < 2 || sampleRateHz <= 0 || !(m.topDb > m.floorDb))
        return out;
    const double nyquist = sampleRateHz / 2.0;
    const double binHz = nyquist / static_cast<double>(n - 1);
    const double top = std::min(m.maxFreqHz, nyquist);
    if (!(top > m.minFreqHz) || m.minFreqHz <= 0.0)
        return out;
    const auto edges = spectrumBandEdges(m.minFreqHz, top);
    const double silenceDb = static_cast<double>(m.thresholdDb) + 0.5;
    const double range = static_cast<double>(m.topDb) - static_cast<double>(m.floorDb);

    for (std::size_t b = 0; b < static_cast<std::size_t>(kSpectrumBandCount); ++b) {
        const double lo = edges[b];
        const double hi = edges[b + 1];
        // Bin k covers [(k − ½)·binHz, (k + ½)·binHz]; bin 0 (DC) is never part of a band.
        const auto first = std::max<std::size_t>(1, static_cast<std::size_t>(std::floor(lo / binHz + 0.5)));
        const auto last = std::min(n - 1, static_cast<std::size_t>(std::floor(hi / binHz + 0.5)));
        double power = 0.0;
        double weight = 0.0;
        for (std::size_t k = first; k <= last; ++k) {
            const double centre = static_cast<double>(k) * binHz;
            const double overlap = (std::min(hi, centre + binHz / 2) - std::max(lo, centre - binHz / 2)) / binHz;
            if (overlap <= 0.0)
                continue;
            weight += overlap;
            const double db = bins[k];
            if (!std::isfinite(db) || db <= silenceDb)
                continue;
            power += overlap * std::pow(10.0, db / 10.0);
        }
        if (weight <= 0.0 || power <= 0.0)
            continue;
        if (weight < 1.0)
            power /= weight; // narrower than a bin: that bin's power, not a fraction of it
        const double db = 10.0 * std::log10(power);
        out[b] = static_cast<float>(std::clamp((db - static_cast<double>(m.floorDb)) / range, 0.0, 1.0));
    }
    return out;
}

} // namespace mmm
