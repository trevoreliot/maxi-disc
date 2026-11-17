// SPDX-License-Identifier: GPL-3.0-or-later
//
// Run under `dbus-run-session` (ctest does): a private session bus, so the test neither
// needs nor touches the desktop's.
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "mprisservice.h"
#include "playbackcontroller.h"
#include "testmedia.h"

using mmm::MprisService;
using mmm::PlaybackController;
using mmm::TrackRow;
using State = mmm::Player::PlaybackState;

namespace {

constexpr int kTimeoutMs = 10000;
const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kRoot = QStringLiteral("org.mpris.MediaPlayer2");
const QString kPlayer = QStringLiteral("org.mpris.MediaPlayer2.Player");

} // namespace

class TestMpris : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    TrackRow m_a, m_b;
    std::unique_ptr<PlaybackController> m_pc;
    std::unique_ptr<MprisService> m_mpris;
    QDBusConnection m_client{QStringLiteral("unset")};
    QList<QVariantMap> m_changes; // PlaybackStatus/Metadata/... from PropertiesChanged

    // Async call from the client connection: the service lives on this thread, so a
    // blocking call would deadlock.
    QDBusMessage call(const QString &iface, const QString &method, const QVariantList &args = {})
    {
        QDBusMessage m = QDBusMessage::createMethodCall(m_mpris->serviceName(), kPath, iface, method);
        m.setArguments(args);
        QDBusPendingCall pending = m_client.asyncCall(m);
        if (!QTest::qWaitFor([&] { return pending.isFinished(); }, kTimeoutMs))
            qWarning("D-Bus call %s timed out", qPrintable(method));
        return pending.reply();
    }

    QVariant prop(const QString &iface, const QString &name)
    {
        const QDBusMessage reply = call(QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"), {iface, name});
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return {};
        return reply.arguments().at(0).value<QDBusVariant>().variant();
    }

    void setProp(const QString &name, const QVariant &value)
    {
        call(QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"),
             {kPlayer, name, QVariant::fromValue(QDBusVariant(value))});
    }

    QVariantMap metadata()
    {
        const QVariant v = prop(kPlayer, QStringLiteral("Metadata"));
        return qdbus_cast<QVariantMap>(v.value<QDBusArgument>());
    }

    bool changedTo(const QString &key, const QVariant &value) const
    {
        for (const QVariantMap &c : m_changes) {
            if (c.value(key) == value)
                return true;
        }
        return false;
    }

private Q_SLOTS:
    void onPropertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &)
    {
        if (iface == kPlayer)
            m_changes << changed;
    }

    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_a.id = 11;
        m_a.path = m_dir.filePath(QStringLiteral("a.flac"));
        m_a.title = QStringLiteral("Airbag");
        m_a.artist = QStringLiteral("Radiohead");
        m_a.album = QStringLiteral("OK Computer");
        m_a.albumArtist = QStringLiteral("Radiohead");
        m_a.trackNumber = 1;
        m_a.durationMs = 5000;
        m_a.coverPath = m_dir.filePath(QStringLiteral("cover.jpg"));
        QFile cover(m_a.coverPath);
        QVERIFY(cover.open(QIODevice::WriteOnly));
        cover.write("jpeg");
        cover.close();
        m_b = m_a;
        m_b.id = 12;
        m_b.path = m_dir.filePath(QStringLiteral("b.flac"));
        m_b.title = QStringLiteral("Paranoid Android");
        m_b.trackNumber = 2;
        QCOMPARE(mmm::test::generateTone(m_a.path, QStringLiteral("flacenc"), 5000), QString());
        QCOMPARE(mmm::test::generateTone(m_b.path, QStringLiteral("flacenc"), 5000), QString());

        m_pc = std::make_unique<PlaybackController>(mmm::PlayerOptions{.audioSink = "fakesink sync=true"});
        m_mpris = std::make_unique<MprisService>(m_pc.get());
        m_mpris->setArtCacheDir(m_dir.filePath(QStringLiteral("art")));
        QString error;
        QVERIFY2(m_mpris->registerOn(QDBusConnection::sessionBus(), &error), qPrintable(error));
        QCOMPARE(m_mpris->serviceName(), QStringLiteral("org.mpris.MediaPlayer2.musicmdmanager"));

        m_client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("client"));
        QVERIFY(m_client.isConnected());
        QVERIFY(m_client.connect(m_mpris->serviceName(), kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                 QStringLiteral("PropertiesChanged"), this,
                                 SLOT(onPropertiesChanged(QString, QVariantMap, QStringList))));
    }

    void cleanupTestCase()
    {
        m_mpris.reset();
        m_pc.reset();
        QDBusConnection::disconnectFromBus(QStringLiteral("client"));
    }

    void rootInterface()
    {
        QCOMPARE(prop(kRoot, QStringLiteral("Identity")).toString(), QStringLiteral("Music MD Manager"));
        QCOMPARE(prop(kRoot, QStringLiteral("DesktopEntry")).toString(), QStringLiteral("music-md-manager"));
        QCOMPARE(prop(kRoot, QStringLiteral("CanRaise")).toBool(), true);
        QCOMPARE(prop(kRoot, QStringLiteral("CanQuit")).toBool(), true);
        QCOMPARE(prop(kRoot, QStringLiteral("HasTrackList")).toBool(), false);

        QSignalSpy raise(m_mpris.get(), &MprisService::raiseRequested);
        QSignalSpy quit(m_mpris.get(), &MprisService::quitRequested);
        call(kRoot, QStringLiteral("Raise"));
        call(kRoot, QStringLiteral("Quit"));
        QCOMPARE(raise.count(), 1);
        QCOMPARE(quit.count(), 1);
    }

    void idleState()
    {
        QCOMPARE(prop(kPlayer, QStringLiteral("PlaybackStatus")).toString(), QStringLiteral("Stopped"));
        QCOMPARE(prop(kPlayer, QStringLiteral("CanPlay")).toBool(), false);
        QCOMPARE(prop(kPlayer, QStringLiteral("CanControl")).toBool(), true);
        const QVariantMap md = metadata();
        QCOMPARE(md.value(QStringLiteral("mpris:trackid")).value<QDBusObjectPath>().path(),
                 QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack"));
        call(kPlayer, QStringLiteral("Play")); // empty queue: harmless
        QCOMPARE(m_pc->player()->playbackState(), State::Stopped);
    }

    void metadataAndStatusSignals()
    {
        m_changes.clear();
        m_pc->playTracks({m_a, m_b}, 0);
        QTRY_VERIFY_WITH_TIMEOUT(changedTo(QStringLiteral("PlaybackStatus"), QStringLiteral("Playing")), kTimeoutMs);
        QCOMPARE(prop(kPlayer, QStringLiteral("PlaybackStatus")).toString(), QStringLiteral("Playing"));

        const QVariantMap md = metadata();
        QCOMPARE(md.value(QStringLiteral("mpris:trackid")).value<QDBusObjectPath>().path(),
                 QStringLiteral("/org/musicmdmanager/track/11"));
        QCOMPARE(md.value(QStringLiteral("xesam:title")).toString(), QStringLiteral("Airbag"));
        QCOMPARE(md.value(QStringLiteral("xesam:artist")).toStringList(), QStringList{QStringLiteral("Radiohead")});
        QCOMPARE(md.value(QStringLiteral("xesam:album")).toString(), QStringLiteral("OK Computer"));
        QCOMPARE(md.value(QStringLiteral("xesam:trackNumber")).toInt(), 1);
        QVERIFY(qAbs(md.value(QStringLiteral("mpris:length")).toLongLong() - 5000000) < 100000);
        QCOMPARE(md.value(QStringLiteral("mpris:artUrl")).toString(), QUrl::fromLocalFile(m_a.coverPath).toString());
        QCOMPARE(prop(kPlayer, QStringLiteral("CanGoNext")).toBool(), true);
    }

    void controlsOverDBus()
    {
        QTRY_COMPARE_WITH_TIMEOUT(m_pc->player()->playbackState(), State::Playing, kTimeoutMs);

        call(kPlayer, QStringLiteral("PlayPause"));
        QTRY_COMPARE_WITH_TIMEOUT(m_pc->player()->playbackState(), State::Paused, kTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(changedTo(QStringLiteral("PlaybackStatus"), QStringLiteral("Paused")), kTimeoutMs);
        call(kPlayer, QStringLiteral("Play"));
        QTRY_COMPARE_WITH_TIMEOUT(m_pc->player()->playbackState(), State::Playing, kTimeoutMs);

        m_changes.clear();
        call(kPlayer, QStringLiteral("Next"));
        QCOMPARE(m_pc->currentTrack()->id, 12);
        QTRY_VERIFY_WITH_TIMEOUT(!m_changes.isEmpty(), kTimeoutMs);
        QCOMPARE(metadata().value(QStringLiteral("xesam:title")).toString(), QStringLiteral("Paranoid Android"));

        call(kPlayer, QStringLiteral("Previous"));
        QCOMPARE(m_pc->currentTrack()->id, 11);

        // Seek forward 2 s: position moves and a Seeked signal goes out.
        QTRY_COMPARE_WITH_TIMEOUT(m_pc->player()->playbackState(), State::Playing, kTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(m_pc->player()->isSeekable(), kTimeoutMs);
        QSignalSpy seeked(m_pc->player(), &mmm::Player::seeked);
        call(kPlayer, QStringLiteral("Seek"), {QVariant::fromValue(qlonglong(2000000))});
        QTRY_COMPARE_WITH_TIMEOUT(seeked.count(), 1, kTimeoutMs);
        QVERIFY(prop(kPlayer, QStringLiteral("Position")).toLongLong() >= 2000000);

        // SetPosition with a stale track id is ignored; with the current one it seeks.
        call(kPlayer, QStringLiteral("SetPosition"),
             {QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/musicmdmanager/track/12"))), qlonglong(0)});
        QCOMPARE(seeked.count(), 1);
        call(kPlayer, QStringLiteral("SetPosition"),
             {QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/musicmdmanager/track/11"))), qlonglong(500000)});
        QTRY_COMPARE_WITH_TIMEOUT(seeked.count(), 2, kTimeoutMs);

        call(kPlayer, QStringLiteral("Stop"));
        QTRY_COMPARE_WITH_TIMEOUT(m_pc->player()->playbackState(), State::Stopped, kTimeoutMs);
    }

    void writableProperties()
    {
        setProp(QStringLiteral("LoopStatus"), QStringLiteral("Playlist"));
        QCOMPARE(m_pc->queue()->repeatMode(), mmm::PlayQueue::RepeatMode::All);
        QCOMPARE(prop(kPlayer, QStringLiteral("LoopStatus")).toString(), QStringLiteral("Playlist"));
        setProp(QStringLiteral("LoopStatus"), QStringLiteral("Track"));
        QCOMPARE(m_pc->queue()->repeatMode(), mmm::PlayQueue::RepeatMode::One);
        setProp(QStringLiteral("LoopStatus"), QStringLiteral("None"));
        QCOMPARE(m_pc->queue()->repeatMode(), mmm::PlayQueue::RepeatMode::Off);

        m_changes.clear();
        setProp(QStringLiteral("Volume"), 0.25);
        QCOMPARE(m_pc->player()->volume(), 0.25);
        QTRY_VERIFY_WITH_TIMEOUT(changedTo(QStringLiteral("Volume"), 0.25), kTimeoutMs);
    }

    void secondInstanceGetsItsOwnName()
    {
        QDBusConnection other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("other"));
        PlaybackController pc2;
        {
            MprisService second(&pc2);
            QVERIFY(second.registerOn(other));
            QVERIFY(second.serviceName().startsWith(QLatin1String("org.mpris.MediaPlayer2.musicmdmanager.instance")));
        }
        QDBusConnection::disconnectFromBus(QStringLiteral("other"));
    }
};

QTEST_GUILESS_MAIN(TestMpris)
#include "tst_mpris.moc"
