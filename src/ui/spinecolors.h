// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QtQmlIntegration/qqmlintegration.h>

#include <atomic>
#include <iterator>
#include <memory>
#include <vector>

class QJSEngine;
class QQmlEngine;

// V1 (MD Visualizer, docs/plans/v1-visualizer-pile.md): the colour of each MiniDisc case in the
// pile, taken from its album cover. QML asks for a cover URL's colour with request() when a case
// appears and release() when it goes; colours are computed newest request first on a small pool
// (a request nobody holds any more is skipped, so scrolling fast through a long pile only computes
// what's still there) and kept in memory by CoverLoader::key, so bumpRevision() (art changed in
// place) gives a new key. The cover itself comes from CoverLoader's 128 px disk cache.
// Construct before the QML engine (main.cpp, tst_qml's Setup); QML gets it via create().
class SpineColors : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    // No default argument: QML would construct its own instead of using create().
    SpineColors(QString coverCacheDir, QObject *parent);
    ~SpineColors() override;

    static SpineColors *instance();
    static SpineColors *create(QQmlEngine *, QJSEngine *);

    // CoverLoader::key: "<albumId>[.<revision>]", "" for no cover.
    Q_INVOKABLE QString key(const QString &coverUrl) const;
    // The colour once it's known; invalid before, and for a cover that has no usable picture.
    Q_INVOKABLE QColor color(const QString &coverUrl) const;
    // Computes the colour (once per key) unless it's known; colorReady(key) when done.
    Q_INVOKABLE void request(const QString &coverUrl);
    // The case that asked went away: if nobody else holds the key, it isn't computed.
    Q_INVOKABLE void release(const QString &coverUrl);
    // Dark ink reads better than light ink on `background` (WCAG contrast).
    Q_INVOKABLE bool prefersDarkText(const QColor &background) const;

    // The cover's dominant colour (a saturation-weighted hue histogram, else the average),
    // its value clamped so a case never vanishes into the dark theme. Invalid for a null image.
    static QColor dominantColor(const QImage &cover);
    static double relativeLuminance(const QColor &c); // WCAG 2.1

    // Tests: hold the workers (requests only queue up) and let them go; how many colours were
    // computed so far; what's queued (keys, ids).
    void setHeld(bool held);
    int computedCount() const { return m_computed; }
    qsizetype pendingCount() const { return std::ssize(m_pending); }
    qsizetype queuedIdCount() const { return m_ids.size(); }

Q_SIGNALS:
    void colorReady(const QString &key);

private:
    void pump();
    void compact();
    void finished(const QString &key, QColor color);

    QString m_cacheDir;
    QHash<QString, QColor> m_colors;  // key → colour (invalid: no usable picture)
    QHash<QString, int> m_holders;    // key → how many cases want it
    QHash<QString, QString> m_ids;    // queued key → the provider id to load it from
    std::vector<QString> m_pending;   // keys, newest last
    QSet<QString> m_inFlight;
    int m_running = 0;
    int m_computed = 0;
    bool m_held = false;
    std::shared_ptr<std::atomic<bool>> m_stopping = std::make_shared<std::atomic<bool>>(false);
    QThreadPool m_pool;
};
