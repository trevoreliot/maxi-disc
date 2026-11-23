// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

#include "devicebrowser.h"

class QQmlEngine;
class QJSEngine;

// A device's music as one flat list: artist rows, their album rows, their tracks (M9b).
// Headers fold and unfold; the cursor (currentIndex) stays on its row when rows change.
// Roles: kind (0 artist, 1 album, 2 track), key, label, detail, depth, expanded, trackCount,
// sizeText, notInLibraryCount (headers), trackNumber, discNumber, durationMs, codec, relPath,
// inLibrary, synced (tracks).
class OnDeviceModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("OnDevice.model is the one")
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged FINAL)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY statsChanged FINAL)
    Q_PROPERTY(int notInLibraryCount READ notInLibraryCount NOTIFY statsChanged FINAL)
    Q_PROPERTY(int syncedCount READ syncedCount NOTIFY statsChanged FINAL)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY statsChanged FINAL)
    Q_PROPERTY(QString totalSizeText READ totalSizeText NOTIFY statsChanged FINAL) // "14.2 GB"

public:
    enum Kind { ArtistRow, AlbumRow, TrackRow };
    Q_ENUM(Kind)
    enum Role {
        KindRole = Qt::UserRole + 1,
        KeyRole,
        LabelRole,
        DetailRole,
        DepthRole,
        ExpandedRole,
        TrackCountRole,
        SizeTextRole,
        NotInLibraryCountRole,
        TrackNumberRole,
        DiscNumberRole,
        DurationRole,
        CodecRole,
        RelPathRole,
        InLibraryRole,
        SyncedRole,
    };

    explicit OnDeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return rowCount(); }
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int row);
    int trackCount() const { return static_cast<int>(m_tracks.size()); }
    int notInLibraryCount() const { return m_notInLibrary; }
    int syncedCount() const { return m_synced; }
    qint64 totalBytes() const { return m_bytes; }
    QString totalSizeText() const;

    Q_INVOKABLE void move(int delta);
    // Folds / unfolds the artist or album at `row` (tracks: nothing).
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE int indexOfKey(const QString &key) const;

    void clear();
    // A browse starts: tracks it doesn't send again are dropped when it ends (endBrowse).
    void beginBrowse();
    void addTracks(const QList<mmm::DeviceTrack> &tracks);
    void endBrowse();
    // Rebuilds the rows now (addTracks coalesces rebuilds while a browse streams in).
    void rebuild();

Q_SIGNALS:
    void countChanged();
    void currentIndexChanged();
    void statsChanged();

private:
    struct Row
    {
        Kind kind = TrackRow;
        QString key;
        QString label;
        QString detail;
        int trackCount = 0;
        qint64 bytes = 0;
        int notInLibrary = 0;
        mmm::DeviceTrack track; // tracks (a copy: m_tracks may change before the next rebuild)
    };
    void updateStats();

    QHash<QString, mmm::DeviceTrack> m_tracks; // by path key
    QSet<QString> m_seen;                      // keys sent by the running browse
    QList<Row> m_rows;
    QSet<QString> m_collapsed;                 // header keys
    int m_current = -1;
    QString m_currentKey;
    int m_notInLibrary = 0;
    int m_synced = 0;
    qint64 m_bytes = 0;
    QTimer m_rebuildTimer;
};

// The Export screen's "On Device" view (QML singleton `OnDevice`, M9b): the selected device's
// music, read while the view is shown (active), paused while a sync runs and refreshed after
// it, and after library changes (for the "not in library" marks). Read-only. Construct after
// Library, Devices and Sync, before the QML engine.
class OnDevice : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged FINAL)
    Q_PROPERTY(OnDeviceModel *model READ model CONSTANT FINAL)
    Q_PROPERTY(bool browsing READ isBrowsing NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool paused READ isPaused NOTIFY stateChanged FINAL)     // a sync is running
    Q_PROPERTY(int done READ done NOTIFY progressChanged FINAL)
    Q_PROPERTY(int total READ total NOTIFY progressChanged FINAL)       // audio files found (-1: still listing)
    Q_PROPERTY(QString scannedDir READ scannedDir NOTIFY stateChanged FINAL) // "" = whole device
    Q_PROPERTY(bool fellBack READ fellBack NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool wholeDevice READ wholeDevice WRITE setWholeDevice NOTIFY stateChanged FINAL)
    // There's a music folder to switch between it and the whole device.
    Q_PROPERTY(bool canScanWholeDevice READ canScanWholeDevice NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged FINAL)

public:
    struct Options
    {
        QString databasePath;
    };

    explicit OnDevice(const Options &options, QObject *parent = nullptr);
    ~OnDevice() override;

    static OnDevice *instance();
    static OnDevice *create(QQmlEngine *, QJSEngine *);

    bool isActive() const { return m_active; }
    void setActive(bool active);
    OnDeviceModel *model() const { return m_model.get(); }
    bool isBrowsing() const { return m_browser->isBusy(); }
    bool isPaused() const { return m_paused; }
    int done() const { return m_done; }
    int total() const { return m_total; }
    QString scannedDir() const { return m_scannedDir; }
    bool fellBack() const { return m_fellBack; }
    bool wholeDevice() const { return m_wholeDevice; }
    void setWholeDevice(bool whole);
    bool canScanWholeDevice() const;
    QString errorString() const { return m_error; }

    // Reads the device again (only new or changed files are actually read).
    Q_INVOKABLE void refresh();

    mmm::DeviceBrowser *browser() const { return m_browser.get(); }

Q_SIGNALS:
    void activeChanged();
    void stateChanged();
    void progressChanged();

private:
    void update(bool force = false);

    std::unique_ptr<OnDeviceModel> m_model;
    std::unique_ptr<mmm::DeviceBrowser> m_browser;
    bool m_active = false;
    bool m_paused = false;
    bool m_wholeDevice = false;
    int m_done = 0;
    int m_total = -1;
    QString m_scannedDir;
    bool m_fellBack = false;
    QString m_error;
    QString m_deviceKey;  // device id + object path the model shows
    QString m_browseKey;  // ... + mount point + music folder + whole device: what was browsed
    QString m_shownKey;   // the scope the model's tracks are from
    QString m_profileMusicDir;
};
