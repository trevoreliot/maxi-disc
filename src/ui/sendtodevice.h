// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

#include "synctypes.h"

class QQmlEngine;
class QJSEngine;

// What adding music to a device did (M11b): counted in tracks, after resolving.
struct DeviceAddResult
{
    QString deviceName;
    int added = 0;         // tracks new to the device's list
    int skipped = 0;       // tracks it had already
    int missing = 0;       // entries no longer in the library
    int insertedItems = 0; // USB: new selection entries (an empty playlist adds one, 0 tracks)
    int alreadyItems = 0;  // USB: entries that were selected already (an empty playlist again)
    QString error;         // refused / failed

    static DeviceAddResult failed(const QString &message)
    {
        DeviceAddResult r;
        r.error = message;
        return r;
    }
};
using DeviceAddCallback = std::function<void(const DeviceAddResult &)>;

// "Send to device" (QML singleton `SendToDevice`, M11b): puts library music on the device
// selected on the Export screen (Devices.current): a MiniDisc recorder's "To record" list, or
// a USB device's sync selection. Used by the Library screen (`i`, the Send buttons), the queue
// (`I`) and Export's own adds; every send ends in notice() for the toast.
// Construct after Devices, Playback, Sync and MiniDisc, before the QML engine.
class SendToDevice : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // The selected device's name ("" if none).
    Q_PROPERTY(QString targetName READ targetName NOTIFY targetChanged FINAL)
    Q_PROPERTY(bool canSend READ canSend NOTIFY targetChanged FINAL)
    // Why it can't ("Select a device on the Export screen first", ...): for tooltips.
    Q_PROPERTY(QString disabledReason READ disabledReason NOTIFY targetChanged FINAL)

public:
    // No default argument: QML would construct its own instead of using create().
    explicit SendToDevice(QObject *parent);
    ~SendToDevice() override;

    static SendToDevice *instance();
    static SendToDevice *create(QQmlEngine *, QJSEngine *);

    QString targetName() const { return m_targetName; }
    bool canSend() const { return m_canSend; }
    QString disabledReason() const { return m_reason; }

    // `kind`: "artist", "genre", "album", "playlist" or "track".
    Q_INVOKABLE void sendItem(const QString &kind, int id, const QString &name);
    // [{kind, id, name}, ...] in order ([] → "Nothing selected").
    Q_INVOKABLE void sendItems(const QVariantList &items);
    // The play queue's tracks (the queue isn't changed).
    Q_INVOKABLE void sendQueue();
    // `index` in the play queue (`i` in the queue panel).
    Q_INVOKABLE void sendQueueTrack(int index);

    void send(const QList<mmm::SyncItem> &items);
    static QString noticeText(const DeviceAddResult &result, bool *isError);

Q_SIGNALS:
    void targetChanged();
    void notice(const QString &text, bool isError);

private:
    void update();
    void startNext();

    QList<QList<mmm::SyncItem>> m_pending; // sends wait for the one before (order kept)
    bool m_sending = false;

    QString m_targetName;
    bool m_canSend = false;
    QString m_reason;
};
