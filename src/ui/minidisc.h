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

public:
    struct Options
    {
        mmm::NetMdBackendFactory backend; // default: libnetmd
        bool monitor = true;              // false: no USB detection (tests, --no-devices)
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

    mmm::NetMdManager *manager() const { return m_manager.get(); }

Q_SIGNALS:
    void stateChanged();
    void discChanged();
    void errorChanged();
    void statusChanged();

private:
    void followCurrentDevice();
    void publishDevices();

    std::unique_ptr<MdTrackModel> m_tracks;
    std::unique_ptr<mmm::NetMdManager> m_manager;
};
