// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QList>
#include <QQuickItem>
#include <QtQmlIntegration/qqmlintegration.h>

#include <array>
#include <vector>

#include "spectrum.h"


// Q3: the transport bar's analyser as LED columns, one per band: segments light from the bottom
// (brighter up the column) and a peak segment hangs above, then falls. Drawn with scene-graph
// rectangle nodes, which every renderer draws (ctest's offscreen platform uses the software one).
// Motion lives in step(): AudioAnalyzer.qml calls it from a FrameAnimation while `animating`,
// so a meter at rest renders nothing. docs/plans/q3-q4-analyser-and-disc.md.
class SpectrumMeter : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    // Targets, 0..1 per band (Playback.levels); the bars move towards them in step().
    Q_PROPERTY(QList<qreal> levels READ levels WRITE setLevels NOTIFY levelsChanged FINAL)
    // While false the targets count as 0: the bars fall to rest.
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged FINAL)
    // True while anything moves (or may: playing). Drive step() only then.
    Q_PROPERTY(bool animating READ animating NOTIFY animatingChanged FINAL)
    Q_PROPERTY(int bandCount READ bandCount CONSTANT FINAL)
    // From the height: ~3 px segments with 1 px gaps.
    Q_PROPERTY(int segmentCount READ segmentCount NOTIFY segmentCountChanged FINAL)
    Q_PROPERTY(QColor unlitColor READ unlitColor WRITE setUnlitColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor lowColor READ lowColor WRITE setLowColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor midColor READ midColor WRITE setMidColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor highColor READ highColor WRITE setHighColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor peakColor READ peakColor WRITE setPeakColor NOTIFY colorsChanged FINAL)

public:
    explicit SpectrumMeter(QQuickItem *parent = nullptr);

    QList<qreal> levels() const;
    void setLevels(const QList<qreal> &levels);
    bool playing() const { return m_playing; }
    void setPlaying(bool playing);
    bool animating() const { return m_animating; }
    static int bandCount() { return mmm::kSpectrumBandCount; }
    int segmentCount() const { return m_segmentCount; }

    QColor unlitColor() const { return m_unlit; }
    void setUnlitColor(const QColor &c) { setColor(m_unlit, c); }
    QColor lowColor() const { return m_low; }
    void setLowColor(const QColor &c) { setColor(m_low, c); }
    QColor midColor() const { return m_mid; }
    void setMidColor(const QColor &c) { setColor(m_mid, c); }
    QColor highColor() const { return m_high; }
    void setHighColor(const QColor &c) { setColor(m_high, c); }
    QColor peakColor() const { return m_peakColor; }
    void setPeakColor(const QColor &c) { setColor(m_peakColor, c); }

    // Advances the bars and peaks by `dtSeconds` (clamped to 0..0.05: a stalled frame doesn't jump).
    Q_INVOKABLE void step(qreal dtSeconds);
    // Everything to rest at once.
    Q_INVOKABLE void reset();
    // For tests: what's drawn. Segments count from 0 at the bottom; peakSegment -1 = none.
    Q_INVOKABLE int litSegments(int band) const;
    Q_INVOKABLE int peakSegment(int band) const;
    Q_INVOKABLE qreal displayLevel(int band) const;
    Q_INVOKABLE qreal peakLevel(int band) const;

    // Ballistics: fast attack, a constant fall (the scale is already dB, so it looks like a real
    // meter and reaches exactly 0), the peak hangs, then falls more slowly.
    static constexpr qreal kAttackTau = 0.014; // s: ~90 % of a jump in 32 ms
    static constexpr qreal kFallPerSecond = 1.6;
    static constexpr qreal kPeakHoldSeconds = 0.5;
    static constexpr qreal kPeakFallPerSecond = 0.85;

Q_SIGNALS:
    void levelsChanged();
    void playingChanged();
    void animatingChanged();
    void segmentCountChanged();
    void colorsChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    using Bands = std::array<qreal, mmm::kSpectrumBandCount>;

    void setColor(QColor &member, const QColor &value);
    void setAnimating(bool animating);
    void updateSegmentCount();
    QColor segmentColor(int band, int segment) const;

    Bands m_target{};
    Bands m_display{};
    Bands m_peak{};
    Bands m_hold{}; // seconds the peak still hangs
    bool m_playing = false;
    bool m_animating = false;
    int m_segmentCount = 1;

    QColor m_unlit;
    QColor m_low;
    QColor m_mid;
    QColor m_high;
    QColor m_peakColor;

    bool m_layoutDirty = true; // size changed: updatePaintNode builds a new node tree
};
