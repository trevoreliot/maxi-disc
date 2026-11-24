// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

#include "devicemanager.h"
#include "sendtodevice.h"
#include "syncjob.h"
#include "syncplanner.h"

class QQmlEngine;
class QJSEngine;

// A device's sync selection. Roles: itemId, kind ("Playlist", "Album", "Artist", "Genre"),
// name, trackCount (-1 until resolved), missing (no longer in the library).
class SyncSelectionModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role { ItemIdRole = Qt::UserRole + 1, KindRole, NameRole, TrackCountRole, MissingRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return rowCount(); }

    void setItems(const QList<mmm::SyncItem> &items);
    void setResolved(const mmm::ResolvedSelection &resolved);
    const QList<mmm::SyncItem> &items() const { return m_items; }

Q_SIGNALS:
    void countChanged();

private:
    QList<mmm::SyncItem> m_items;
    mmm::ResolvedSelection m_resolved;
    bool m_isResolved = false;
};

// The tracks the selection resolves to. Roles: title, artist, album, codec, target ("FLAC"
// copied, "MP3" converted), transcode, durationMs.
class SyncTrackModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role { TitleRole = Qt::UserRole + 1, ArtistRole, AlbumRole, CodecRole, TargetRole, TranscodeRole, DurationRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return rowCount(); }

    void setTracks(const QList<mmm::TrackRow> &tracks, const std::optional<mmm::DeviceProfile> &profile);
    qint64 totalDurationMs() const;

Q_SIGNALS:
    void countChanged();

private:
    QList<mmm::TrackRow> m_tracks;
    std::optional<mmm::DeviceProfile> m_profile;
};

// A sync plan's steps. Roles: kind ("add", "update", "move", "remove", "forget"), label
// ("Add", ...), title, detail (artist · album, or the reason), path (on the device),
// sizeText, playlist.
class SyncPlanModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role { KindRole = Qt::UserRole + 1, LabelRole, TitleRole, DetailRole, PathRole, SizeTextRole, PlaylistRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return rowCount(); }

    void setActions(const QList<mmm::SyncAction> &actions);

Q_SIGNALS:
    void countChanged();

private:
    QList<mmm::SyncAction> m_actions;
};

// USB / DAP sync on the Export screen (QML singleton `Sync`, M9): the selected device's sync
// selection, the tracks it resolves to, planning, running and cancelling a sync. Follows
// Devices.current. Exactly one instance; construct after Library and Devices, before the QML
// engine, and destroy it before them.
//
// Flow: add entries (addItem) → plan() → state Ready shows the plan → run() → Syncing with
// progress → Done with the result. Changing the selection, profile, library or mount state
// drops a shown plan (back to Idle), so a sync always runs the plan the user saw.
class Sync : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY deviceChanged FINAL)
    Q_PROPERTY(SyncSelectionModel *selection READ selection CONSTANT FINAL)
    Q_PROPERTY(SyncTrackModel *tracks READ tracks CONSTANT FINAL)
    Q_PROPERTY(SyncPlanModel *planActions READ planActions CONSTANT FINAL)
    Q_PROPERTY(qint64 totalDurationMs READ totalDurationMs NOTIFY tracksChanged FINAL)
    Q_PROPERTY(State state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool canPlan READ canPlan NOTIFY canPlanChanged FINAL)
    Q_PROPERTY(bool canSync READ canSync NOTIFY canSyncChanged FINAL)
    // {deviceName, adds, updates, moves, removes, unchanged, playlists, untracked, toWrite, freed,
    //  freeAfter, insufficientSpace, hasWork, warnings, missing, error} ({} before planning).
    Q_PROPERTY(QVariantMap planSummary READ planSummary NOTIFY stateChanged FINAL)
    // {filesDone, filesTotal, fraction, stage, currentFile, bytesText}
    Q_PROPERTY(QVariantMap progress READ progress NOTIFY progressChanged FINAL)
    // {ok, canceled, added, updated, moved, removed, playlists, failed, written, error, warnings}
    Q_PROPERTY(QVariantMap result READ result NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged FINAL)

public:
    enum State { Idle, Planning, Ready, Syncing, Done };
    Q_ENUM(State)

    struct Options
    {
        QString databasePath;
        QString cacheDir;     // "" = AppPaths::transcodeCacheDir()
    };

    explicit Sync(const Options &options, QObject *parent = nullptr);
    ~Sync() override;

    static Sync *instance();
    static Sync *create(QQmlEngine *, QJSEngine *);

    QString deviceId() const { return m_deviceId; }
    SyncSelectionModel *selection() const { return m_selection.get(); }
    SyncTrackModel *tracks() const { return m_tracks.get(); }
    SyncPlanModel *planActions() const { return m_planModel.get(); }
    qint64 totalDurationMs() const { return m_tracks->totalDurationMs(); }
    State state() const { return m_state; }
    bool canPlan() const;
    bool canSync() const;
    QVariantMap planSummary() const;
    QVariantMap progress() const { return m_progress; }
    QVariantMap result() const { return m_result; }
    QString errorString() const { return m_error; }

    // `kind`: "playlist", "album", "artist" or "genre" (BrowsePane modes map onto these).
    Q_INVOKABLE bool addItem(const QString &kind, int targetId, const QString &name);
    // Several entries in one go (M11b, SendToDevice); `done` gets the counts in tracks.
    void addItems(const QList<mmm::SyncItem> &items, const DeviceAddCallback &done = {});
    Q_INVOKABLE void removeAt(int row);
    Q_INVOKABLE void plan();
    Q_INVOKABLE void run();
    Q_INVOKABLE void cancel();
    // Back to Idle from Ready or Done.
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE void clearError();

    mmm::SyncJob *job() const { return m_job.get(); }

Q_SIGNALS:
    void deviceChanged();
    void tracksChanged();
    void stateChanged();
    void canPlanChanged();
    void canSyncChanged();
    void progressChanged();
    void errorStringChanged();

private:
    void followCurrentDevice();
    void reloadSelection();
    void resolve();
    void invalidatePlan();
    void setState(State state);
    void setError(const QString &message);
    std::optional<mmm::DeviceEntry> currentEntry() const;
    // The device can take a sync now (and, if given, is still mounted at `mountPoint`).
    static bool syncable(const std::optional<mmm::DeviceEntry> &entry, const QString &mountPoint = {});
    void updateCan();

    std::unique_ptr<SyncSelectionModel> m_selection;
    std::unique_ptr<SyncTrackModel> m_tracks;
    std::unique_ptr<SyncPlanModel> m_planModel;
    std::unique_ptr<mmm::SyncJob> m_job;
    QString m_deviceId;
    QString m_objectPath;
    State m_state = Idle;
    mmm::SyncPlan m_plan;
    QVariantMap m_progress;
    QVariantMap m_result;
    QString m_error;
    quint64 m_resolveGeneration = 0;
    bool m_canPlan = false;
    bool m_canSync = false;
    bool m_planStale = false; // something changed while planning: drop the plan when it comes
};
