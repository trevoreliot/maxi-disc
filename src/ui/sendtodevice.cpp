// SPDX-License-Identifier: GPL-3.0-or-later
#include "sendtodevice.h"

#include "devices.h"
#include "minidisc.h"
#include "playback.h"
#include "sync.h"

#include "playqueue.h"

#include <QJSEngine>
#include <QPointer>

namespace {
SendToDevice *s_instance = nullptr;

QString tracks(int n)
{
    return n == 1 ? QStringLiteral("1 track") : QStringLiteral("%1 tracks").arg(n);
}

QString trackLabel(const mmm::TrackRow &t)
{
    return t.artist.isEmpty() ? t.title : QStringLiteral("%1 – %2").arg(t.title, t.artist);
}
} // namespace

SendToDevice::SendToDevice(QObject *parent)
    : QObject(parent)
{
    Q_ASSERT_X(!s_instance, "SendToDevice", "only one SendToDevice may exist");
    Q_ASSERT_X(Devices::instance(), "SendToDevice", "construct Devices before SendToDevice");
    s_instance = this;
    connect(Devices::instance(), &Devices::currentChanged, this, &SendToDevice::update);
    connect(Devices::instance(), &Devices::countChanged, this, &SendToDevice::update);
    connect(Devices::instance()->manager(), &mmm::DeviceManager::devicesChanged, this, &SendToDevice::update);
    connect(Devices::instance()->manager(), &mmm::DeviceManager::profileChanged, this, &SendToDevice::update);
    if (Sync *sync = Sync::instance())
        connect(sync, &Sync::stateChanged, this, &SendToDevice::update);
    if (MiniDisc *md = MiniDisc::instance())
        connect(md, &MiniDisc::recordingChanged, this, &SendToDevice::update);
    update();
}

SendToDevice::~SendToDevice()
{
    if (s_instance == this)
        s_instance = nullptr;
}

SendToDevice *SendToDevice::instance()
{
    return s_instance;
}

SendToDevice *SendToDevice::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "SendToDevice", "construct SendToDevice before the QML engine");
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void SendToDevice::update()
{
    const QVariantMap current = Devices::instance()->current();
    const QString name = current.value(QStringLiteral("name")).toString();
    QString reason;
    if (current.isEmpty()) {
        reason = QStringLiteral("Select a device on the Export screen first");
    } else if (current.value(QStringLiteral("netMd")).toBool()) {
        if (!MiniDisc::instance())
            reason = QStringLiteral("MiniDisc recording isn't available");
        else if (MiniDisc::instance()->isRecording())
            reason = QStringLiteral("Wait for %1 to finish recording").arg(name);
    } else if (!current.value(QStringLiteral("hasProfile")).toBool()) {
        reason = QStringLiteral("Set up %1 on the Export screen first").arg(name);
    } else if (!Sync::instance()) {
        reason = QStringLiteral("USB sync isn't available");
    } else if (Sync::instance()->state() == Sync::Syncing) {
        reason = QStringLiteral("Wait for %1 to finish syncing").arg(name);
    }
    const bool can = reason.isEmpty();
    if (name == m_targetName && can == m_canSend && reason == m_reason)
        return;
    m_targetName = name;
    m_canSend = can;
    m_reason = reason;
    Q_EMIT targetChanged();
}

void SendToDevice::sendItem(const QString &kind, int id, const QString &name)
{
    sendItems({QVariantMap{{QStringLiteral("kind"), kind}, {QStringLiteral("id"), id}, {QStringLiteral("name"), name}}});
}

void SendToDevice::sendItems(const QVariantList &items)
{
    QList<mmm::SyncItem> list;
    for (const QVariant &v : items) {
        const QVariantMap m = v.toMap();
        const auto kind = mmm::SyncItem::kindFrom(m.value(QStringLiteral("kind")).toString());
        const qint64 id = m.value(QStringLiteral("id")).toLongLong();
        if (kind && id > 0)
            list << mmm::SyncItem{0, QString(), *kind, id, m.value(QStringLiteral("name")).toString()};
    }
    if (list.isEmpty()) {
        Q_EMIT notice(QStringLiteral("Nothing selected to send"), true);
        return;
    }
    send(list);
}

void SendToDevice::sendQueue()
{
    Playback *playback = Playback::instance();
    QList<mmm::SyncItem> list;
    if (playback) {
        for (const mmm::TrackRow &t : playback->queue()->items()) {
            if (t.id > 0)
                list << mmm::SyncItem{0, QString(), mmm::SyncItem::Kind::Track, t.id, trackLabel(t)};
        }
    }
    if (list.isEmpty()) {
        Q_EMIT notice(QStringLiteral("The play queue is empty"), true);
        return;
    }
    send(list);
}

void SendToDevice::sendQueueTrack(int index)
{
    Playback *playback = Playback::instance();
    const mmm::TrackRow *t = playback ? playback->queue()->at(index) : nullptr;
    if (!t || t->id <= 0) {
        Q_EMIT notice(QStringLiteral("Nothing selected to send"), true);
        return;
    }
    send({mmm::SyncItem{0, QString(), mmm::SyncItem::Kind::Track, t->id, trackLabel(*t)}});
}

void SendToDevice::send(const QList<mmm::SyncItem> &items)
{
    m_pending << items;
    if (!m_sending)
        startNext();
}

// One send at a time: each resolves off the UI thread, and two in flight could land out of order.
void SendToDevice::startNext()
{
    while (!m_pending.isEmpty()) {
        const QList<mmm::SyncItem> items = m_pending.takeFirst();
        update();
        if (!m_canSend) {
            Q_EMIT notice(m_reason, true);
            continue;
        }
        m_sending = true;
        auto done = [self = QPointer<SendToDevice>(this)](const DeviceAddResult &r) {
            if (!self)
                return;
            bool error = false;
            const QString text = noticeText(r, &error);
            Q_EMIT self->notice(text, error);
            self->m_sending = false;
            self->startNext();
        };
        if (Devices::instance()->current().value(QStringLiteral("netMd")).toBool())
            MiniDisc::instance()->addItems(items, done);
        else
            Sync::instance()->addItems(items, done);
        return;
    }
}

QString SendToDevice::noticeText(const DeviceAddResult &r, bool *isError)
{
    *isError = false;
    if (!r.error.isEmpty()) {
        *isError = true;
        return r.error;
    }
    const QString gone = r.missing > 0 ? QStringLiteral(" · %1 no longer in the library").arg(r.missing) : QString();
    if (r.added > 0)
        return QStringLiteral("Added %1 to %2").arg(tracks(r.added), r.deviceName)
            + (r.skipped > 0 ? QStringLiteral(" · %1 already there").arg(r.skipped) : QString()) + gone;
    if (r.skipped > 0)
        return (r.skipped == 1 ? QStringLiteral("Already on %1's list").arg(r.deviceName)
                               : QStringLiteral("Already on %1's list (%2)").arg(r.deviceName, tracks(r.skipped)))
            + gone;
    if (r.insertedItems > 0)
        return QStringLiteral("Added to %1 (no tracks yet)").arg(r.deviceName) + gone;
    if (r.alreadyItems > 0)
        return QStringLiteral("Already on %1's list").arg(r.deviceName) + gone;
    *isError = true;
    if (r.missing > 0)
        return QStringLiteral("Not in the library any more");
    return QStringLiteral("Nothing to add");
}
