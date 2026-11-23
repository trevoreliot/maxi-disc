// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

#include "netmdmanager.h"

class QQmlEngine;
class QJSEngine;

// The disc's tracks (M10). Roles: number (1-based), title, durationText ("3:45"), mode ("SP",
// "LP2", "LP4", "SP mono"), protectedTrack, groupTitle (set on the first track of a group),
// inGroup. currentIndex is the keyboard cursor.
class MdTrackModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("MiniDisc.tracks is the one")
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged FINAL)

public:
    enum Role { NumberRole = Qt::UserRole + 1, TitleRole, DurationTextRole, ModeRole, ProtectedRole, GroupTitleRole, InGroupRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return rowCount(); }
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int row);
    Q_INVOKABLE void move(int delta);
    // The title of row `row` ("" if untitled or out of range).
    Q_INVOKABLE QString titleAt(int row) const;

    void setDisc(const mmm::MdDisc &disc);

    static QString formatDuration(qint64 ms);

Q_SIGNALS:
    void countChanged();
    void currentIndexChanged();

private:
    mmm::MdDisc m_disc;
    int m_current = -1;
};

// The tracks to record (M11). Roles: number, title (as it goes on the disc), artist,
// durationText, itemState ("pending", "decoding", "uploading", "done", "failed", "skipped"), error.
class MdQueueModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("MiniDisc.queue is the one")
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged FINAL)

public:
    enum Role { NumberRole = Qt::UserRole + 1, TitleRole, ArtistRole, DurationTextRole, StateRole, ErrorRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return rowCount(); }
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int row);
    Q_INVOKABLE void move(int delta);

    const QList<mmm::MdUploadItem> &items() const { return m_items; }
    void setItems(const QList<mmm::MdUploadItem> &items);
    // Same rows, new states (a recording's progress): the cursor stays.
    void updateStates(const QList<mmm::MdUploadItem> &items);

Q_SIGNALS:
    void countChanged();
    void currentIndexChanged();

private:
    QList<mmm::MdUploadItem> m_items;
    int m_current = -1;
};

// NetMD MiniDisc on the Export screen (QML singleton `MiniDisc`, M10): follows the selected
// device (Devices.current); when it's a recorder, opens it and shows its disc; renames the disc
// and its tracks. Puts the recorders it finds into the Devices list. Construct after Devices,
// before the QML engine.
class MiniDisc : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool selected READ isSelected NOTIFY stateChanged FINAL)       // the current device is a recorder
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ isBusy NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString busyText READ busyText NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool hasDisc READ hasDisc NOTIFY discChanged FINAL)
    Q_PROPERTY(QString discTitle READ discTitle NOTIFY discChanged FINAL)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY discChanged FINAL)
    Q_PROPERTY(int groupCount READ groupCount NOTIFY discChanged FINAL)
    // "42:18 recorded · 38:41 free of 80:59" ("" without a disc); used / total for a bar.
    Q_PROPERTY(QString capacityText READ capacityText NOTIFY discChanged FINAL)
    Q_PROPERTY(double usedFraction READ usedFraction NOTIFY discChanged FINAL)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged FINAL)
    Q_PROPERTY(bool permissionError READ permissionError NOTIFY errorChanged FINAL)
    Q_PROPERTY(MdTrackModel *tracks READ tracks CONSTANT FINAL)
    // A status line for the device list ("12 tracks", "No disc", "Reading…").
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged FINAL)

    // --- Recording (M11) ---
    // The right pane shows the tracks to record (true) or the disc (false). o switches.
    Q_PROPERTY(bool recordView READ recordView WRITE setRecordView NOTIFY recordViewChanged FINAL)
    Q_PROPERTY(MdQueueModel *queue READ queue CONSTANT FINAL)
    // {canRecord, error, warning, durationText, neededText, availableText, queuedFraction,
    //  overflow, suggestedDiscTitle, pending}: can the queue go on the disc?
    Q_PROPERTY(QVariantMap plan READ plan NOTIFY planChanged FINAL)
    // Title the (blank) disc after the album when recording a single album to it.
    Q_PROPERTY(bool titleDisc READ titleDisc WRITE setTitleDisc NOTIFY planChanged FINAL)
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged FINAL)
    Q_PROPERTY(bool cancelling READ isCancelling NOTIFY recordingChanged FINAL)
    // {trackText ("Track 2 of 8: Title"), stage, trackFraction, overallFraction, bytesText}
    Q_PROPERTY(QVariantMap progress READ progress NOTIFY progressChanged FINAL)
    // The last recording's outcome ("" before one): "Recorded 8 tracks.", "Canceled ...", ...
    Q_PROPERTY(QString resultText READ resultText NOTIFY recordingChanged FINAL)

public:
    struct Options
    {
        mmm::NetMdBackendFactory backend; // default: libnetmd
        bool monitor = true;              // false: no USB detection (tests, --no-devices)
        mmm::MdAudioEncoderFactory encoder = {}; // default: GstPcmEncoder (tests: a fake)
        QString tempDir = {};             // decoded audio ("" = the app's cache)
    };

    explicit MiniDisc(const Options &options, QObject *parent = nullptr);
    ~MiniDisc() override;

    static MiniDisc *instance();
    static MiniDisc *create(QQmlEngine *, QJSEngine *);

    bool isSelected() const { return !m_manager->selectedId().isEmpty(); }
    QString deviceName() const;
    bool isBusy() const { return m_manager->isBusy(); }
    QString busyText() const;
    bool hasDisc() const { return m_manager->hasDisc(); }
    QString discTitle() const { return m_manager->disc().title; }
    int trackCount() const { return static_cast<int>(m_manager->disc().tracks.size()); }
    int groupCount() const { return static_cast<int>(m_manager->disc().groups.size()); }
    QString capacityText() const;
    double usedFraction() const;
    QString errorString() const { return m_manager->errorString(); }
    bool permissionError() const { return m_manager->error() == mmm::NetMdError::PermissionDenied; }
    MdTrackModel *tracks() const { return m_tracks.get(); }
    QString statusText() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void renameDisc(const QString &title);
    Q_INVOKABLE void renameTrack(int index, const QString &title);
    // What a title becomes on the disc: {text, changed, truncated}.
    Q_INVOKABLE QVariantMap preview(const QString &title, bool disc) const;
    // Test hook: these recorders instead of detected ones ({id, name}).
    Q_INVOKABLE void setTestDevices(const QVariantList &devices);

    // `kind`: "artist", "album", "genre" or "playlist" (BrowsePane.currentItem): its tracks
    // join the queue (resolved off the UI thread; tracks already queued are skipped).
    Q_INVOKABLE void addItem(const QString &kind, int targetId, const QString &name);
    Q_INVOKABLE void removeQueued(int row);
    Q_INVOKABLE void clearQueue();
    // Records the queue (done tracks of an earlier run are dropped first). False if the plan
    // says it can't.
    Q_INVOKABLE bool record();
    // Stops after the track being recorded.
    Q_INVOKABLE void cancelRecord();

    bool recordView() const { return m_recordView; }
    void setRecordView(bool on);
    MdQueueModel *queue() const { return m_queue.get(); }
    QVariantMap plan() const;
    bool titleDisc() const { return m_titleDisc; }
    void setTitleDisc(bool on);
    bool isRecording() const { return m_manager->isUploading(); }
    bool isCancelling() const { return m_manager->isCancelling(); }
    QVariantMap progress() const;
    QString resultText() const { return m_result; }

    mmm::NetMdManager *manager() const { return m_manager.get(); }

Q_SIGNALS:
    void stateChanged();
    void discChanged();
    void errorChanged();
    void statusChanged();
    void recordViewChanged();
    void planChanged();
    void recordingChanged();
    void progressChanged();

private:
    void followCurrentDevice();
    void publishDevices();
    mmm::MdUploadPlan currentPlan() const;
    QList<mmm::TrackRow> queuedTracks() const;

    std::unique_ptr<MdTrackModel> m_tracks;
    std::unique_ptr<mmm::NetMdManager> m_manager;
    std::unique_ptr<MdQueueModel> m_queue;
    bool m_recordView = false;
    bool m_titleDisc = true;
    QString m_result;
};
