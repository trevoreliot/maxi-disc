// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMetaType>

#include <array>
#include <span>

namespace mmm {

// Q3, the transport bar's analyser: GStreamer's `spectrum` element gives linear FFT bins (DC to
// Nyquist, in dB); the meter shows kSpectrumBandCount log-spaced bands, each 0 (silent) .. 1 (full).
inline constexpr int kSpectrumBandCount = 24;
using SpectrumLevels = std::array<float, kSpectrumBandCount>;

struct SpectrumMapping
{
    double minFreqHz = 40.0;
    double maxFreqHz = 16000.0;
    // A band's summed power at or below floorDb shows nothing; at or above topDb, full scale.
    // From real tracks (24 bands, 1024 bins): a quiet 70s master's bands sit around -45 dB
    // (loud moments -30), a loud modern one's around -34 (bass -20).
    float floorDb = -66.0f;
    float topDb = -15.0f;
    // The element's own threshold: bins it clamped there are silence, not a quiet signal.
    float thresholdDb = -80.0f;
};

// kSpectrumBandCount + 1 log-spaced edges from minFreqHz to maxFreqHz.
std::array<double, kSpectrumBandCount + 1> spectrumBandEdges(double minFreqHz, double maxFreqHz);

// `binMagnitudesDb`: the element's "magnitude" list, N bins from DC to Nyquist (N = its `bands`;
// bin k is centred on k · rate / (2N − 2)). Each band is the power sum of the bins it covers
// (weighted by overlap), so roughly pink music looks level from bass to treble; a band narrower
// than one bin takes that bin's power. Bins at the threshold, and non-finite values, add nothing.
SpectrumLevels mapSpectrumToBands(std::span<const float> binMagnitudesDb, int sampleRateHz,
                                  const SpectrumMapping &mapping = {});

} // namespace mmm

Q_DECLARE_METATYPE(mmm::SpectrumLevels)
