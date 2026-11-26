// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "spectrum.h"

#include <cmath>
#include <limits>
#include <vector>

using mmm::kSpectrumBandCount;
using mmm::mapSpectrumToBands;
using mmm::SpectrumLevels;

namespace {

constexpr int kRate = 44100;
constexpr std::size_t kBins = 1024; // the player's `spectrum bands=1024`
constexpr double kBinHz = (kRate / 2.0) / (kBins - 1);

std::vector<float> silentBins() { return std::vector<float>(kBins, -80.0f); }

std::size_t binOf(double hz) { return static_cast<std::size_t>(std::lround(hz / kBinHz)); }

int bandOf(double hz)
{
    const auto edges = mmm::spectrumBandEdges(40.0, 16000.0);
    for (int b = 0; b < kSpectrumBandCount; ++b) {
        if (hz >= edges[static_cast<std::size_t>(b)] && hz < edges[static_cast<std::size_t>(b) + 1])
            return b;
    }
    return -1;
}

int peakBand(const SpectrumLevels &levels)
{
    int best = 0;
    for (int b = 1; b < kSpectrumBandCount; ++b) {
        if (levels[static_cast<std::size_t>(b)] > levels[static_cast<std::size_t>(best)])
            best = b;
    }
    return best;
}

} // namespace

class TestSpectrum : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void bandEdgesAreLogSpaced()
    {
        const auto edges = mmm::spectrumBandEdges(40.0, 16000.0);
        QCOMPARE(edges.size(), std::size_t(kSpectrumBandCount + 1));
        QVERIFY(qFuzzyCompare(edges.front(), 40.0));
        QVERIFY(qFuzzyCompare(edges.back(), 16000.0));
        const double ratio = edges[1] / edges[0];
        for (std::size_t i = 1; i < edges.size(); ++i)
            QVERIFY(qFuzzyCompare(edges[i] / edges[i - 1], ratio));
    }

    void silenceIsZero()
    {
        const SpectrumLevels levels = mapSpectrumToBands(silentBins(), kRate);
        for (float v : levels)
            QCOMPARE(v, 0.0f);
    }

    void toneLandsInItsBand_data()
    {
        QTest::addColumn<double>("hz");
        QTest::newRow("60 Hz") << 60.0;
        QTest::newRow("1 kHz") << 1000.0;
        QTest::newRow("8 kHz") << 8000.0;
    }

    void toneLandsInItsBand()
    {
        QFETCH(double, hz);
        std::vector<float> bins = silentBins();
        bins[binOf(hz)] = -20.0f;
        const SpectrumLevels levels = mapSpectrumToBands(bins, kRate);
        const int band = bandOf(hz);
        QCOMPARE(peakBand(levels), band);
        // -20 dB on the default -66 .. -15 scale.
        QVERIFY2(qAbs(levels[static_cast<std::size_t>(band)] - 46.0f / 51.0f) < 0.02f,
                 qPrintable(QString::number(levels[static_cast<std::size_t>(band)])));
        for (int b = 0; b < kSpectrumBandCount; ++b) {
            if (qAbs(b - band) >= 2)
                QCOMPARE(levels[static_cast<std::size_t>(b)], 0.0f);
        }
    }

    void loudClampsToFull()
    {
        std::vector<float> bins(kBins, 0.0f); // 0 dB in every bin: far above the top
        for (float v : mapSpectrumToBands(bins, kRate))
            QCOMPARE(v, 1.0f);
    }

    // Pink noise (equal power per octave) should look level from bass to treble: the power
    // sum per log band is what makes that so. Bands 0-2 are narrower than a bin.
    void pinkIsLevel()
    {
        std::vector<float> bins(kBins);
        for (std::size_t k = 1; k < kBins; ++k)
            bins[k] = static_cast<float>(10.0 * std::log10(8e-3 / (static_cast<double>(k) * kBinHz)));
        bins[0] = -80.0f;
        const SpectrumLevels levels = mapSpectrumToBands(bins, kRate);
        float lo = 1.0f;
        float hi = 0.0f;
        for (std::size_t b = 3; b <= 22; ++b) {
            lo = std::min(lo, levels[b]);
            hi = std::max(hi, levels[b]);
        }
        QVERIFY2(hi - lo < 0.05f, qPrintable(QStringLiteral("%1 .. %2").arg(lo).arg(hi)));
        QVERIFY(lo > 0.2f && hi < 0.9f); // on the scale, not pinned
    }

    void oddInputsAreSafe()
    {
        QCOMPARE(mapSpectrumToBands({}, kRate), SpectrumLevels{});
        QCOMPARE(mapSpectrumToBands(silentBins(), 0), SpectrumLevels{});
        const std::vector<float> one(1, 0.0f);
        QCOMPARE(mapSpectrumToBands(one, kRate), SpectrumLevels{});

        std::vector<float> bins = silentBins();
        bins[10] = std::numeric_limits<float>::quiet_NaN();
        bins[20] = -std::numeric_limits<float>::infinity();
        bins[30] = std::numeric_limits<float>::infinity();
        QCOMPARE(mapSpectrumToBands(bins, kRate), SpectrumLevels{});

        // A low sample rate: bands stop at Nyquist (11 025 Hz) and a tone near it still shows.
        std::vector<float> low = silentBins();
        low[kBins - 10] = -20.0f;
        const SpectrumLevels levels = mapSpectrumToBands(low, 22050);
        QCOMPARE(peakBand(levels), kSpectrumBandCount - 1);

        // A tiny rate whose Nyquist is below the lowest band: nothing, no crash.
        QCOMPARE(mapSpectrumToBands(low, 60), SpectrumLevels{});
    }
};

QTEST_GUILESS_MAIN(TestSpectrum)
#include "tst_spectrum.moc"
