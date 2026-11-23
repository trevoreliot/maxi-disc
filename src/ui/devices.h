// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

#include "devicemanager.h"

class QQmlEngine;
class QJSEngine;

// The sync targets on the Export screen (QML singleton `Devices`): connected USB storage
// with its profile, the selected one, mount/unmount. Wraps mmm::DeviceManager. Exactly one
// instance; create it before the QML engine and destroy it after (like Library).
//
// Roles: deviceId (keys the profile), objectPath (this device right now), name, details ("Vendor Model · exfat"), mounted, mountPoint, busy,
// readOnly, spaceText ("14.2 GB free of 32 GB" / "32 GB · not mounted"), hasProfile,
// profileName, netMd (a MiniDisc recorder, M10: no mount, no profile).
class Devices : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentChanged FINAL)
    // The selected device as {roleName: value} ({} if none), and its profile summarised:
    // {name, musicDir, layout, formats, transcode, artwork, example} ({} if not set up).
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged FINAL)
    Q_PROPERTY(QVariantMap currentProfile READ currentProfile NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged FINAL)

public:
    struct Options
    {
        QString databasePath;
        bool monitor = true; // false: no device detection (tests, --no-devices)
    };
    enum Role {
        DeviceIdRole = Qt::UserRole + 1,
        ObjectPathRole,
        NameRole,
        DetailsRole,
        MountedRole,
        MountPointRole,
        BusyRole,
        ReadOnlyRole,
        SpaceTextRole,
        HasProfileRole,
        ProfileNameRole,
        NetMdRole,
    };

    explicit Devices(const Options &options, QObject *parent = nullptr);
    ~Devices() override;

    static Devices *instance();
    static Devices *create(QQmlEngine *, QJSEngine *);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return rowCount(); }
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int row);
    QVariantMap current() const;
    QVariantMap currentProfile() const;
    QString errorString() const { return m_error; }

    // `device`: its objectPath role (unique), or its deviceId.
    Q_INVOKABLE void mount(const QString &device);
    Q_INVOKABLE void unmount(const QString &device);
    // Mounts the selected device, or unmounts it if mounted.
    Q_INVOKABLE void toggleMountCurrent();
    Q_INVOKABLE void clearError();
    // Test hook: shows these devices ({id, name, mountPoint, totalBytes, freeBytes, ...}).
    Q_INVOKABLE void setTestDevices(const QVariantList &devices);

    mmm::DeviceManager *manager() const { return m_manager.get(); }

    // "14.2 GB" (decimal units, like drive labels).
    static QString formatBytes(qint64 bytes);
    // A profile's settings in words (the Export screen's summary).
    static QVariantMap summarize(const mmm::DeviceProfile &profile);

Q_SIGNALS:
    void countChanged();
    void currentChanged();
    void errorStringChanged();

private:
    void onDevicesChanged();
    void setError(const QString &message);

    std::unique_ptr<mmm::DeviceManager> m_manager;
    QList<mmm::DeviceEntry> m_rows;
    int m_current = -1;
    QString m_error;
};
