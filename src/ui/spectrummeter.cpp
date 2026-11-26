// SPDX-License-Identifier: GPL-3.0-or-later
#include "spectrummeter.h"

#include <QQuickWindow>
#include <QSGNode>
#include <QSGRectangleNode>

#include <algorithm>
#include <cmath>

namespace {

constexpr qreal kSegmentPitch = 4.0; // 3 px lit + 1 px gap
constexpr qreal kSegmentGap = 1.0;
constexpr int kMaxSegments = 32;

constexpr std::size_t idx(int band) { return static_cast<std::size_t>(band); }

// The meter's scene-graph tree: one rectangle child per segment, band-major. The list lives with
// the nodes (the scene graph owns and deletes both), never on the item.
struct MeterNode : QSGNode
{
    std::vector<QSGRectangleNode *> segments;
};

} // namespace

SpectrumMeter::SpectrumMeter(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

QList<qreal> SpectrumMeter::levels() const { return {m_target.cbegin(), m_target.cend()}; }

void SpectrumMeter::setLevels(const QList<qreal> &levels)
{
    Bands next{};
    for (int b = 0; b < mmm::kSpectrumBandCount && b < levels.size(); ++b)
        next[idx(b)] = std::clamp(levels[b], 0.0, 1.0);
    if (next == m_target)
        return;
    m_target = next;
    Q_EMIT levelsChanged();
}

void SpectrumMeter::setPlaying(bool playing)
{
    if (m_playing == playing)
        return;
    m_playing = playing;
    if (playing)
        setAnimating(true); // when stopping, step() decides once everything has fallen
    Q_EMIT playingChanged();
}

void SpectrumMeter::setAnimating(bool animating)
{
    if (m_animating == animating)
        return;
    m_animating = animating;
    Q_EMIT animatingChanged();
}

void SpectrumMeter::setColor(QColor &member, const QColor &value)
{
    if (member == value)
        return;
    member = value;
    update();
    Q_EMIT colorsChanged();
}

void SpectrumMeter::step(qreal dtSeconds)
{
    const qreal dt = std::clamp(dtSeconds, 0.0, 0.05);
    const qreal attack = 1.0 - std::exp(-dt / kAttackTau);
    bool moving = m_playing;
    for (std::size_t b = 0; b < m_display.size(); ++b) {
        const qreal target = m_playing ? m_target[b] : 0.0;
        qreal &display = m_display[b];
        if (target > display)
            display += (target - display) * attack;
        else
            display = std::max(target, display - kFallPerSecond * dt);

        qreal &peak = m_peak[b];
        if (display >= peak) {
            peak = display;
            m_hold[b] = kPeakHoldSeconds;
        } else if (m_hold[b] > 0.0) {
            m_hold[b] = std::max(0.0, m_hold[b] - dt);
        } else {
            peak = std::max(display, peak - kPeakFallPerSecond * dt);
        }
        if (display > 0.0 || peak > 0.0)
            moving = true;
    }
    setAnimating(moving);
    update();
}

void SpectrumMeter::reset()
{
    m_display.fill(0.0);
    m_peak.fill(0.0);
    m_hold.fill(0.0);
    setAnimating(m_playing);
    update();
}

int SpectrumMeter::litSegments(int band) const
{
    if (band < 0 || band >= mmm::kSpectrumBandCount)
        return 0;
    return static_cast<int>(std::lround(m_display[idx(band)] * m_segmentCount));
}

int SpectrumMeter::peakSegment(int band) const
{
    if (band < 0 || band >= mmm::kSpectrumBandCount)
        return -1;
    return static_cast<int>(std::lround(m_peak[idx(band)] * m_segmentCount)) - 1;
}

qreal SpectrumMeter::displayLevel(int band) const
{
    return band >= 0 && band < mmm::kSpectrumBandCount ? m_display[idx(band)] : 0.0;
}

qreal SpectrumMeter::peakLevel(int band) const
{
    return band >= 0 && band < mmm::kSpectrumBandCount ? m_peak[idx(band)] : 0.0;
}

void SpectrumMeter::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        updateSegmentCount();
        m_layoutDirty = true;
        update();
    }
}

void SpectrumMeter::updateSegmentCount()
{
    const int count = std::clamp(static_cast<int>((height() + kSegmentGap) / kSegmentPitch), 1, kMaxSegments);
    if (count == m_segmentCount)
        return;
    m_segmentCount = count;
    Q_EMIT segmentCountChanged();
}

QColor SpectrumMeter::segmentColor(int band, int segment) const
{
    if (segment == peakSegment(band))
        return m_peakColor;
    if (segment >= litSegments(band))
        return m_unlit;
    const qreal height = (segment + 0.5) / m_segmentCount;
    return height < 0.6 ? m_low : height < 0.85 ? m_mid : m_high;
}

QSGNode *SpectrumMeter::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QQuickWindow *win = window();
    if (!win || width() <= 0 || height() <= 0) {
        delete oldNode;
        return nullptr;
    }
    const int bands = mmm::kSpectrumBandCount;
    const int segments = m_segmentCount;
    auto *root = static_cast<MeterNode *>(oldNode);
    if (!root || m_layoutDirty) {
        delete root; // and its children (owned by parent)
        root = new MeterNode;
        const qreal roughPitch = width() / bands;
        const qreal columnGap = roughPitch >= 12.0 ? 3.0 : roughPitch >= 6.0 ? 2.0 : 1.0;
        // Pitches include one gap more than there are gaps, so the first column / segment starts
        // at one edge and the last ends at the other.
        const qreal columnPitch = (width() + columnGap) / bands;
        const qreal segmentPitch = (height() + kSegmentGap) / segments;
        root->segments.reserve(static_cast<std::size_t>(bands * segments));
        for (int b = 0; b < bands; ++b) {
            const qreal x0 = std::round(b * columnPitch);
            const qreal x1 = std::round((b + 1) * columnPitch - columnGap);
            for (int s = 0; s < segments; ++s) {
                const qreal bottom = std::round(height() - s * segmentPitch);
                const qreal top = std::round(height() - (s + 1) * segmentPitch + kSegmentGap);
                QSGRectangleNode *node = win->createRectangleNode();
                node->setRect(QRectF(x0, top, std::max(1.0, x1 - x0), std::max(1.0, bottom - top)));
                node->setColor(m_unlit);
                root->appendChildNode(node);
                root->segments.push_back(node);
            }
        }
        m_layoutDirty = false;
    }
    for (int b = 0; b < bands; ++b) {
        for (int s = 0; s < segments; ++s) {
            QSGRectangleNode *node = root->segments[static_cast<std::size_t>(b * segments + s)];
            const QColor c = segmentColor(b, s);
            if (node->color() != c)
                node->setColor(c);
        }
    }
    return root;
}
