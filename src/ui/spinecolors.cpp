// SPDX-License-Identifier: GPL-3.0-or-later
#include "spinecolors.h"

#include "coverimageprovider.h"

#include <QJSEngine>
#include <QMetaObject>
#include <QQmlEngine>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <numbers>

namespace {

SpineColors *s_instance = nullptr;

constexpr int kWorkers = 2;
constexpr int kSample = 24; // the cover is scaled to kSample × kSample first
constexpr int kHueBins = 36;
// Below this the cover counts as black and white: a few coloured specks don't colour the case.
constexpr double kMinChromaticWeight = 12.0;
constexpr float kMinSaturation = 0.18F;
constexpr float kMinValue = 0.14F;
// Cases stay between these values: never black (they'd vanish into the theme), never white.
constexpr float kCaseMinValue = 0.22F;
constexpr float kCaseMaxValue = 0.86F;
constexpr qsizetype kCompactAt = 4096;
constexpr QLatin1StringView kCoverPrefix("image://cover/");

double linear(int channel)
{
    const double c = channel / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

} // namespace

SpineColors::SpineColors(QString coverCacheDir, QObject *parent)
    : QObject(parent)
    , m_cacheDir(std::move(coverCacheDir))
{
    Q_ASSERT_X(!s_instance, "SpineColors", "only one SpineColors may exist");
    s_instance = this;
    m_pool.setMaxThreadCount(kWorkers);
}

SpineColors::~SpineColors()
{
    m_stopping->store(true);
    m_pool.clear();
    m_pool.waitForDone(); // results still queued to this object are dropped with it
    if (s_instance == this)
        s_instance = nullptr;
}

SpineColors *SpineColors::instance()
{
    return s_instance;
}

SpineColors *SpineColors::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "SpineColors", "construct SpineColors before the QML engine");
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QString SpineColors::key(const QString &coverUrl) const
{
    return CoverLoader::key(coverUrl);
}

QColor SpineColors::color(const QString &coverUrl) const
{
    return m_colors.value(CoverLoader::key(coverUrl));
}

void SpineColors::request(const QString &coverUrl)
{
    const QString k = CoverLoader::key(coverUrl);
    if (k.isEmpty())
        return;
    ++m_holders[k];
    if (m_colors.contains(k) || m_inFlight.contains(k))
        return;
    // Queued already (another case with the cover, or scrolled back to it): pushed again, so
    // it's first in line; the older entry is skipped when it comes up.
    m_ids.insert(k, coverUrl.mid(kCoverPrefix.size()));
    m_pending.push_back(k);
    if (std::ssize(m_pending) > kCompactAt)
        compact();
    pump();
}

void SpineColors::release(const QString &coverUrl)
{
    const QString k = CoverLoader::key(coverUrl);
    const auto it = m_holders.find(k);
    if (it == m_holders.end())
        return;
    if (--*it <= 0) {
        m_holders.erase(it);
        m_ids.remove(k); // its pending entries are skipped (or compacted away)
    }
}

void SpineColors::compact()
{
    // Long scrolls while the workers are busy: keep each key still held once, newest first.
    QSet<QString> seen;
    std::vector<QString> kept;
    for (auto it = m_pending.rbegin(); it != m_pending.rend(); ++it) {
        if (m_holders.contains(*it) && !seen.contains(*it)) {
            seen.insert(*it);
            kept.push_back(*it);
        }
    }
    std::reverse(kept.begin(), kept.end());
    m_pending = std::move(kept);
}

bool SpineColors::prefersDarkText(const QColor &background) const
{
    const double l = relativeLuminance(background);
    const double withWhite = 1.05 / (l + 0.05);
    const double withBlack = (l + 0.05) / 0.05;
    return withBlack >= withWhite;
}

void SpineColors::setHeld(bool held)
{
    m_held = held;
    pump();
}

void SpineColors::pump()
{
    while (!m_held && m_running < kWorkers && !m_pending.empty()) {
        const QString k = m_pending.back(); // newest first: what's on screen now
        m_pending.pop_back();
        if (!m_holders.contains(k) || m_colors.contains(k) || m_inFlight.contains(k))
            continue; // scrolled away, done, or on its way
        m_inFlight.insert(k);
        ++m_running;
        m_pool.start([this, k, id = m_ids.take(k), dir = m_cacheDir, stopping = m_stopping] {
            if (stopping->load())
                return;
            const QColor c = dominantColor(CoverLoader::load(id, QSize(128, 128), dir));
            if (stopping->load())
                return;
            QMetaObject::invokeMethod(this, [this, k, c] { finished(k, c); }, Qt::QueuedConnection);
        });
    }
}

void SpineColors::finished(const QString &key, QColor color)
{
    --m_running;
    ++m_computed;
    m_inFlight.remove(key);
    m_colors.insert(key, color);
    Q_EMIT colorReady(key);
    pump();
}

double SpineColors::relativeLuminance(const QColor &c)
{
    const QColor rgb = c.toRgb();
    return 0.2126 * linear(rgb.red()) + 0.7152 * linear(rgb.green()) + 0.0722 * linear(rgb.blue());
}

QColor SpineColors::dominantColor(const QImage &cover)
{
    if (cover.isNull())
        return {};
    const QImage small = cover.scaled(kSample, kSample, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGB32);

    // Hue histogram, each pixel weighted towards rich mid-value colours (s² · a hump in v).
    std::array<double, kHueBins> weight{};
    std::array<std::array<double, 3>, kHueBins> sum{};
    std::array<double, 3> all{};
    int count = 0;
    for (int y = 0; y < small.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(small.constScanLine(y));
        for (int x = 0; x < small.width(); ++x) {
            const QRgb p = line[x];
            const double r = qRed(p);
            const double g = qGreen(p);
            const double b = qBlue(p);
            all[0] += r;
            all[1] += g;
            all[2] += b;
            ++count;
            float h = 0;
            float s = 0;
            float v = 0;
            QColor::fromRgb(p).getHsvF(&h, &s, &v);
            if (h < 0 || s < kMinSaturation || v < kMinValue)
                continue; // grey, white or black: no say in the hue
            const double w = double(s) * double(s) * (0.3 + 0.7 * std::sin(double(v) * std::numbers::pi));
            const int bin = std::min(kHueBins - 1, static_cast<int>(h * float(kHueBins)));
            weight[size_t(bin)] += w;
            sum[size_t(bin)][0] += r * w;
            sum[size_t(bin)][1] += g * w;
            sum[size_t(bin)][2] += b * w;
        }
    }
    if (count == 0)
        return {};

    double total = 0;
    for (const double w : weight)
        total += w;

    std::array<double, 3> rgb{};
    if (total >= kMinChromaticWeight) {
        const auto at = [](int i) { return size_t((i + kHueBins) % kHueBins); };
        int best = 0;
        double bestScore = -1;
        for (int i = 0; i < kHueBins; ++i) {
            const double score = weight[at(i)] + 0.25 * (weight[at(i - 1)] + weight[at(i + 1)]);
            if (score > bestScore) {
                bestScore = score;
                best = i;
            }
        }
        double w = 0;
        for (int d = -1; d <= 1; ++d) {
            w += weight[at(best + d)];
            for (size_t c = 0; c < 3; ++c)
                rgb[c] += sum[at(best + d)][c];
        }
        for (double &c : rgb)
            c /= w;
    } else {
        for (size_t c = 0; c < 3; ++c)
            rgb[c] = all[c] / count;
    }

    const QColor mean = QColor::fromRgbF(float(rgb[0] / 255.0), float(rgb[1] / 255.0), float(rgb[2] / 255.0));
    float h = 0;
    float s = 0;
    float v = 0;
    mean.getHsvF(&h, &s, &v);
    return QColor::fromHsvF(h, s, std::clamp(v, kCaseMinValue, kCaseMaxValue)).toRgb();
}
