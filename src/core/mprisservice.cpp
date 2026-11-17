// SPDX-License-Identifier: GPL-3.0-or-later
#include "mprisservice.h"

#include "appinfo.h"
#include "apppaths.h"
#include "artcache.h"
#include "playbackcontroller.h"

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QLoggingCategory>
#include <QUrl>

Q_LOGGING_CATEGORY(lcMpris, "mmm.mpris")

namespace mmm {

namespace {

const QString kObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kPlayerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString kNoTrack = QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack");

QString trackObjectPath(qint64 id)
{
    return QStringLiteral("/org/musicmdmanager/track/%1").arg(id);
}

// org.mpris.MediaPlayer2
class RootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    explicit RootAdaptor(MprisService *service) : QDBusAbstractAdaptor(service), m_service(service) {}

    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return AppInfo::displayName(); }
    QString desktopEntry() const { return AppInfo::name(); }
    QStringList supportedUriSchemes() const { return {QStringLiteral("file")}; }
    QStringList supportedMimeTypes() const
    {
        return {QStringLiteral("audio/flac"), QStringLiteral("audio/mpeg"), QStringLiteral("audio/x-wav"),
                QStringLiteral("audio/mp4"),  QStringLiteral("audio/ogg"),  QStringLiteral("audio/opus")};
    }

public Q_SLOTS:
    void Raise() { Q_EMIT m_service->raiseRequested(); }
    void Quit() { Q_EMIT m_service->quitRequested(); }

private:
    MprisService *m_service;
};

// org.mpris.MediaPlayer2.Player
class PlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    explicit PlayerAdaptor(MprisService *service) : QDBusAbstractAdaptor(service), m_service(service) {}

    PlaybackController *pc() const { return m_service->controller(); }

    QString playbackStatus() const { return m_service->playbackStatus(); }
    QString loopStatus() const { return m_service->loopStatus(); }
    void setLoopStatus(const QString &s) { m_service->setLoopStatus(s); }
    double rate() const { return 1.0; }
    void setRate(double) {} // fixed rate; spec allows ignoring
    bool shuffle() const { return false; }
    void setShuffle(bool) {} // not supported in v1
    QVariantMap metadata() const { return m_service->metadata(); }
    double volume() const { return pc()->player()->volume(); }
    void setVolume(double v) { pc()->player()->setVolume(v); }
    qlonglong position() const { return pc()->player()->position() * 1000; }
    bool canGoNext() const { return pc()->canGoNext(); }
    bool canGoPrevious() const { return pc()->canGoPrevious(); }
    bool canPlay() const { return pc()->queue()->count() > 0; }
    bool canPause() const { return pc()->currentTrack() != nullptr; }
    bool canSeek() const { return pc()->currentTrack() != nullptr; }
    bool canControl() const { return true; }

public Q_SLOTS:
    void Next() { pc()->next(); }
    void Previous() { pc()->previous(); }
    void Pause() { pc()->pause(); }
    void PlayPause() { pc()->togglePlayPause(); }
    void Stop() { pc()->stop(); }
    void Play() { pc()->play(); }
    void Seek(qlonglong offsetUs)
    {
        const qint64 target = pc()->player()->position() + offsetUs / 1000;
        const qint64 duration = pc()->player()->duration();
        if (duration >= 0 && target > duration) {
            pc()->next(); // spec: seeking past the end acts like Next
            return;
        }
        pc()->seek(std::max<qint64>(0, target));
    }
    void SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs)
    {
        const TrackRow *t = pc()->currentTrack();
        if (!t || trackId.path() != trackObjectPath(t->id) || positionUs < 0)
            return; // spec: ignore stale track ids
        const qint64 ms = positionUs / 1000;
        if (pc()->player()->duration() >= 0 && ms > pc()->player()->duration())
            return;
        pc()->seek(ms);
    }
    void OpenUri(const QString &) {} // only library tracks are playable for now

Q_SIGNALS:
    void Seeked(qlonglong positionUs);

private:
    MprisService *m_service;
};

} // namespace

MprisService::MprisService(PlaybackController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_connection(QStringLiteral("unset"))
    , m_artCacheDir(AppPaths::artCacheDir())
{
    new RootAdaptor(this);
    auto *player = new PlayerAdaptor(this);

    Player *p = controller->player();
    connect(p, &Player::playbackStateChanged, this, [this] {
        notifyPlayer({{QStringLiteral("PlaybackStatus"), playbackStatus()}});
    });
    connect(p, &Player::volumeChanged, this, [this](double v) { notifyPlayer({{QStringLiteral("Volume"), v}}); });
    connect(p, &Player::seeked, player, [player](qint64 ms) { Q_EMIT player->Seeked(ms * 1000); });
    connect(p, &Player::durationChanged, this, [this] {
        m_metadataTrackId = -2; // length may have been refined
        notifyPlayer({{QStringLiteral("Metadata"), metadata()}});
    });
    connect(controller, &PlaybackController::currentTrackChanged, this, &MprisService::emitAll);
    connect(controller->queue(), &PlayQueue::upcomingChanged, this, [this] {
        notifyPlayer({{QStringLiteral("CanGoNext"), m_controller->canGoNext()},
                      {QStringLiteral("CanGoPrevious"), m_controller->canGoPrevious()},
                      {QStringLiteral("CanPlay"), m_controller->queue()->count() > 0}});
    });
    connect(controller->queue(), &PlayQueue::repeatModeChanged, this, [this] {
        notifyPlayer({{QStringLiteral("LoopStatus"), loopStatus()}});
    });
}

MprisService::~MprisService()
{
    if (m_connection.isConnected() && !m_serviceName.isEmpty()) {
        m_connection.unregisterObject(kObjectPath);
        m_connection.unregisterService(m_serviceName);
    }
}

bool MprisService::registerOn(const QDBusConnection &connection, QString *error)
{
    m_connection = connection;
    if (!m_connection.isConnected()) {
        if (error)
            *error = QStringLiteral("no D-Bus session bus");
        return false;
    }
    if (!m_connection.registerObject(kObjectPath, this, QDBusConnection::ExportAdaptors)) {
        if (error)
            *error = m_connection.lastError().message();
        return false;
    }
    const QString base = QStringLiteral("org.mpris.MediaPlayer2.musicmdmanager");
    for (const QString &name : {base, base + QStringLiteral(".instance%1").arg(QCoreApplication::applicationPid())}) {
        if (m_connection.registerService(name)) {
            m_serviceName = name;
            qCInfo(lcMpris) << "MPRIS service" << name;
            return true;
        }
    }
    if (error)
        *error = m_connection.lastError().message();
    m_connection.unregisterObject(kObjectPath);
    return false;
}

QString MprisService::playbackStatus() const
{
    switch (m_controller->player()->playbackState()) {
    case Player::PlaybackState::Playing: return QStringLiteral("Playing");
    case Player::PlaybackState::Paused: return QStringLiteral("Paused");
    case Player::PlaybackState::Stopped: break;
    }
    return QStringLiteral("Stopped");
}

QString MprisService::loopStatus() const
{
    switch (m_controller->queue()->repeatMode()) {
    case PlayQueue::RepeatMode::All: return QStringLiteral("Playlist");
    case PlayQueue::RepeatMode::One: return QStringLiteral("Track");
    case PlayQueue::RepeatMode::Off: break;
    }
    return QStringLiteral("None");
}

void MprisService::setLoopStatus(const QString &status)
{
    if (status == QLatin1String("Playlist"))
        m_controller->queue()->setRepeatMode(PlayQueue::RepeatMode::All);
    else if (status == QLatin1String("Track"))
        m_controller->queue()->setRepeatMode(PlayQueue::RepeatMode::One);
    else if (status == QLatin1String("None"))
        m_controller->queue()->setRepeatMode(PlayQueue::RepeatMode::Off);
}

QVariantMap MprisService::metadata() const
{
    const TrackRow *t = m_controller->currentTrack();
    const qint64 id = t ? t->id : -1;
    if (id == m_metadataTrackId)
        return m_cachedMetadata;

    QVariantMap m;
    if (!t) {
        m.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(kNoTrack)));
    } else {
        m.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(trackObjectPath(t->id))));
        // Prefer the Player's duration once known (exact), else the library's.
        const qint64 durationMs = m_controller->player()->duration() > 0 ? m_controller->player()->duration() : t->durationMs;
        m.insert(QStringLiteral("mpris:length"), static_cast<qlonglong>(durationMs * 1000));
        m.insert(QStringLiteral("xesam:title"), t->title);
        if (!t->artist.isEmpty())
            m.insert(QStringLiteral("xesam:artist"), QStringList{t->artist});
        if (!t->album.isEmpty())
            m.insert(QStringLiteral("xesam:album"), t->album);
        if (!t->albumArtist.isEmpty())
            m.insert(QStringLiteral("xesam:albumArtist"), QStringList{t->albumArtist});
        if (t->trackNumber > 0)
            m.insert(QStringLiteral("xesam:trackNumber"), t->trackNumber);
        if (t->discNumber > 0)
            m.insert(QStringLiteral("xesam:discNumber"), t->discNumber);
        m.insert(QStringLiteral("xesam:url"), QUrl::fromLocalFile(t->path).toString());
        const QString art = ArtCache::fileFor(*t, m_artCacheDir);
        if (!art.isEmpty())
            m.insert(QStringLiteral("mpris:artUrl"), QUrl::fromLocalFile(art).toString());
    }
    m_cachedMetadata = m;
    m_metadataTrackId = id;
    return m;
}

void MprisService::notifyPlayer(const QVariantMap &changed)
{
    if (!m_connection.isConnected() || m_serviceName.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createSignal(kObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                  QStringLiteral("PropertiesChanged"));
    msg << kPlayerInterface << changed << QStringList();
    m_connection.send(msg);
}

void MprisService::emitAll()
{
    notifyPlayer({
        {QStringLiteral("Metadata"), metadata()},
        {QStringLiteral("PlaybackStatus"), playbackStatus()},
        {QStringLiteral("CanGoNext"), m_controller->canGoNext()},
        {QStringLiteral("CanGoPrevious"), m_controller->canGoPrevious()},
        {QStringLiteral("CanPlay"), m_controller->queue()->count() > 0},
        {QStringLiteral("CanPause"), m_controller->currentTrack() != nullptr},
        {QStringLiteral("CanSeek"), m_controller->currentTrack() != nullptr},
    });
}

} // namespace mmm

#include "mprisservice.moc"
