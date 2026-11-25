// SPDX-License-Identifier: GPL-3.0-or-later
#include "discord.h"

#include "playback.h"
#include "playbackcontroller.h"

#include <QDesktopServices>
#include <QJSEngine>
#include <QSettings>

namespace {

Discord *s_instance = nullptr;

mmm::DiscordIpc::Options ipcOptions(const Discord::Options &o)
{
    mmm::DiscordIpc::Options ipc;
    ipc.ipcDirs = o.ipcDirs;
    ipc.firstRetryMs = o.firstRetryMs;
    return ipc;
}

mmm::DiscordPresence::Options presenceOptions(const Discord::Options &o)
{
    mmm::DiscordPresence::Options p;
    p.coalesceMs = o.coalesceMs;
    return p;
}

const QString kSetActivity = QStringLiteral("SET_ACTIVITY");

} // namespace

Discord::Discord(Options options, QObject *parent)
    : QObject(parent)
    , m_options(std::move(options))
    , m_ipc(ipcOptions(m_options))
    , m_presence(presenceOptions(m_options))
{
    Q_ASSERT_X(!s_instance, "Discord", "only one Discord may exist");
    s_instance = this;
    if (!m_options.openUrl)
        m_options.openUrl = [](const QUrl &url) { QDesktopServices::openUrl(url); };

    connect(&m_ipc, &mmm::DiscordIpc::stateChanged, this, [this](mmm::DiscordIpc::State s) {
        if (s == mmm::DiscordIpc::State::Ready) {
            m_commandError.clear();
            feed(); // the position now, not when Discord went away
        }
        m_presence.setConnected(s == mmm::DiscordIpc::State::Ready);
        Q_EMIT changed();
    });
    connect(&m_ipc, &mmm::DiscordIpc::commandError, this, [this](const QString &message) {
        m_commandError = message;
        Q_EMIT changed();
    });
    connect(&m_presence, &mmm::DiscordPresence::activityChanged, this,
            [this](const QJsonObject &args) { m_ipc.send(kSetActivity, args); });

    m_pc = m_options.playback;
    if (!m_pc && Playback::instance())
        m_pc = Playback::instance()->controller();
    Q_ASSERT_X(m_pc, "Discord", "construct Playback before Discord (or pass a PlaybackController)");
    if (m_pc) {
        const auto feedNow = [this] { feed(); };
        connect(m_pc, &mmm::PlaybackController::currentTrackChanged, this, feedNow);
        connect(m_pc, &mmm::PlaybackController::playStarted, this, feedNow);
        connect(m_pc, &mmm::PlaybackController::currentTrackRetagged, this, feedNow);
        mmm::Player *player = m_pc->player();
        connect(player, &mmm::Player::playbackStateChanged, this, feedNow);
        connect(player, &mmm::Player::positionChanged, this, feedNow); // cheap: unchanged isn't re-sent
        connect(player, &mmm::Player::seeked, this, feedNow);
        connect(player, &mmm::Player::durationChanged, this, feedNow);
    }

    if (m_options.persistSettings) {
        QSettings settings;
        m_appId = settings.value(QStringLiteral("discord/appId")).toString().trimmed();
        m_enabled = settings.value(QStringLiteral("discord/enabled"), false).toBool();
    } else {
        m_appId = m_options.appId.trimmed();
    }
    m_ipc.setClientId(m_appId);
    apply();
}

Discord::~Discord()
{
    m_ipc.disconnect(this);
    m_presence.disconnect(this);
    // Quitting: cleared now (Discord would clear it when the connection goes anyway).
    if (m_ipc.state() == mmm::DiscordIpc::State::Ready)
        m_ipc.sendAndClose(kSetActivity, m_presence.clearArgs());
    s_instance = nullptr;
}

Discord *Discord::instance()
{
    return s_instance;
}

Discord *Discord::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "Discord", "construct Discord before the QML engine");
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void Discord::feed()
{
    if (!m_pc)
        return;
    const mmm::Player *player = m_pc->player();
    std::optional<mmm::DiscordTrack> track;
    if (const mmm::TrackRow *t = m_pc->currentTrack()) {
        const qint64 duration = t->durationMs > 0 ? t->durationMs : player->duration();
        track = mmm::DiscordTrack{t->title, t->artist, t->album, duration};
    }
    m_presence.update(track, player->playbackState() == mmm::Player::PlaybackState::Playing, player->position());
}

void Discord::apply()
{
    if (m_enabled && hasAppId()) {
        m_ipc.start();
    } else if (m_ipc.state() == mmm::DiscordIpc::State::Ready) {
        m_ipc.sendAndClose(kSetActivity, m_presence.clearArgs()); // cleared at once
    } else {
        m_ipc.stop();
    }
    Q_EMIT changed();
}

void Discord::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    save();
    apply();
}

void Discord::setAppId(const QString &id)
{
    const QString trimmed = id.trimmed();
    if (trimmed == m_appId)
        return;
    const bool first = m_appId.isEmpty();
    m_appId = trimmed;
    m_commandError.clear();
    if (m_appId.isEmpty() && m_ipc.state() == mmm::DiscordIpc::State::Ready)
        m_ipc.sendAndClose(kSetActivity, m_presence.clearArgs()); // forgotten: cleared first
    m_ipc.setClientId(m_appId); // a running connection starts over with it
    if (first && hasAppId())
        m_enabled = true; // set up: on
    save();
    apply();
}

void Discord::openDeveloperPortal()
{
    m_options.openUrl(QUrl(QStringLiteral("https://discord.com/developers/applications")));
}

void Discord::save()
{
    if (!m_options.persistSettings)
        return;
    QSettings settings;
    settings.setValue(QStringLiteral("discord/appId"), m_appId);
    settings.setValue(QStringLiteral("discord/enabled"), m_enabled);
}

QString Discord::state() const
{
    if (!m_enabled)
        return QStringLiteral("off");
    if (!hasAppId())
        return QStringLiteral("no-app-id");
    switch (m_ipc.state()) {
    case mmm::DiscordIpc::State::Stopped:
    case mmm::DiscordIpc::State::NotRunning:
        return QStringLiteral("not-running");
    case mmm::DiscordIpc::State::Connecting:
        return QStringLiteral("connecting");
    case mmm::DiscordIpc::State::Ready:
        return QStringLiteral("connected");
    case mmm::DiscordIpc::State::Error:
        return QStringLiteral("error");
    }
    return {};
}

QString Discord::errorText() const
{
    if (m_ipc.state() == mmm::DiscordIpc::State::Error)
        return QStringLiteral("Discord refused the application ID (%1). Check it on the Developer Portal.").arg(m_ipc.lastError());
    if (!m_commandError.isEmpty())
        return QStringLiteral("Discord didn't take the status: %1").arg(m_commandError);
    return {};
}

QString Discord::statusText() const
{
    const QString s = state();
    if (s == QLatin1String("off"))
        return hasAppId() ? QStringLiteral("Off: Discord isn't told what you play") : QString();
    if (s == QLatin1String("no-app-id"))
        return QStringLiteral("Paste your application's ID to start");
    if (s == QLatin1String("not-running"))
        return QStringLiteral("Waiting for Discord: it isn't running (checking now and then)");
    if (s == QLatin1String("connecting"))
        return QStringLiteral("Connecting to Discord…");
    if (s == QLatin1String("connected"))
        return QStringLiteral("Connected: Discord shows what you play");
    return {};
}

QString Discord::toolTip() const
{
    const QString s = state();
    if (s == QLatin1String("connected"))
        return QStringLiteral("Discord: showing what you play");
    if (s == QLatin1String("off"))
        return hasAppId() ? QStringLiteral("Discord: off") : QStringLiteral("Discord: not set up. Click to set up");
    if (s == QLatin1String("no-app-id"))
        return QStringLiteral("Discord: not set up. Click to set up");
    if (s == QLatin1String("not-running"))
        return QStringLiteral("Discord: on, waiting for Discord to start");
    if (s == QLatin1String("connecting"))
        return QStringLiteral("Discord: connecting…");
    return QStringLiteral("Discord: %1").arg(errorText());
}
