// SPDX-License-Identifier: GPL-3.0-or-later
#include "listenbrainz.h"

#include "playback.h"
#include "playbackcontroller.h"

#include <QDesktopServices>
#include <QJSEngine>
#include <QSettings>

namespace {
ListenBrainz *s_instance = nullptr;
}

ListenBrainz::ListenBrainz(Options options, QObject *parent)
    : QObject(parent)
    , m_options(std::move(options))
{
    Q_ASSERT_X(!s_instance, "ListenBrainz", "only one ListenBrainz may exist");
    s_instance = this;
    if (!m_options.openUrl)
        m_options.openUrl = [](const QUrl &url) { QDesktopServices::openUrl(url); };
    // The token's store: the keyring, or pass without one (QSettings secrets/backend overrides).
    if (!m_options.secrets) {
        const QString preference = m_options.persistSettings ? QSettings().value(QStringLiteral("secrets/backend")).toString() : QString();
        m_options.secrets = mmm::makeDefaultSecretStore(mmm::parseSecretBackendPreference(preference));
    }
    mmm::Scrobbler::Options so;
    so.databasePath = m_options.databasePath;
    so.secrets = m_options.secrets;
    so.baseUrl = m_options.baseUrl;
    so.watchNetwork = m_options.watchNetwork;
    m_s = std::make_unique<mmm::Scrobbler>(so);

    connect(m_s.get(), &mmm::Scrobbler::changed, this, [this] {
        Q_EMIT changed();
        Q_EMIT statusChanged();
    });
    connect(m_s.get(), &mmm::Scrobbler::queueChanged, this, [this] {
        Q_EMIT queueChanged();
        Q_EMIT statusChanged();
    });
    connect(m_s.get(), &mmm::Scrobbler::settingsChanged, this, &ListenBrainz::save);

    // The plays: what PlaybackController starts, what the Player does with them.
    mmm::PlaybackController *pc = m_options.playback;
    if (!pc && Playback::instance())
        pc = Playback::instance()->controller();
    Q_ASSERT_X(pc, "ListenBrainz", "construct Playback before ListenBrainz (or pass a PlaybackController)");
    if (pc) {
        auto *player = pc->player();
        const auto playing = [player] { return player->playbackState() == mmm::Player::PlaybackState::Playing; };
        connect(pc, &mmm::PlaybackController::playStarted, this, [this, pc, playing] {
            if (const mmm::TrackRow *t = pc->currentTrack())
                m_s->playStarted(*t, playing());
        });
        connect(pc, &mmm::PlaybackController::currentTrackRetagged, this, [this, pc] {
            if (const mmm::TrackRow *t = pc->currentTrack())
                m_s->playRetagged(*t);
        });
        connect(pc, &mmm::PlaybackController::currentTrackChanged, this, [this, pc] {
            if (!pc->currentTrack())
                m_s->playEnded();
        });
        connect(player, &mmm::Player::playbackStateChanged, this, [this](mmm::Player::PlaybackState state) {
            m_s->playingChanged(state == mmm::Player::PlaybackState::Playing);
        });
        connect(player, &mmm::Player::durationChanged, this, [this](qint64 ms) { m_s->playDurationChanged(ms); });
    }

    if (m_options.persistSettings) {
        QSettings settings;
        m_s->restore(settings.value(QStringLiteral("listenbrainz/user")).toString(),
                     settings.value(QStringLiteral("listenbrainz/enabled"), false).toBool());
    }
}

ListenBrainz::~ListenBrainz()
{
    m_s.reset(); // counts the playing track if it's due, before Playback goes
    s_instance = nullptr;
}

ListenBrainz *ListenBrainz::instance()
{
    return s_instance;
}

ListenBrainz *ListenBrainz::create(QQmlEngine *, QJSEngine *engine)
{
    Q_ASSERT_X(s_instance, "ListenBrainz", "construct ListenBrainz before the QML engine");
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    Q_UNUSED(engine)
    return s_instance;
}

void ListenBrainz::save()
{
    if (!m_options.persistSettings)
        return;
    QSettings settings;
    settings.setValue(QStringLiteral("listenbrainz/user"), m_s->user());
    settings.setValue(QStringLiteral("listenbrainz/enabled"), m_s->isEnabled());
}

QString ListenBrainz::state() const
{
    switch (m_s->account()) {
    case mmm::Scrobbler::Account::Disconnected:
        return QStringLiteral("disconnected");
    case mmm::Scrobbler::Account::Checking:
        return QStringLiteral("checking");
    case mmm::Scrobbler::Account::Connected:
        return QStringLiteral("connected");
    case mmm::Scrobbler::Account::Expired:
        return QStringLiteral("expired");
    }
    return {};
}

QString ListenBrainz::errorText() const
{
    if (!m_s->keyringError().isEmpty()) {
        return m_options.secrets->backend() == mmm::SecretBackend::Pass
            ? QStringLiteral("Can't use pass: %1").arg(m_s->keyringError())
            : QStringLiteral("The keyring can't be used: %1").arg(m_s->keyringError());
    }
    return m_s->lastError();
}

QString ListenBrainz::statusText() const
{
    const int n = m_s->queued();
    const QString waiting = n == 1 ? QStringLiteral("1 listen waiting") : QStringLiteral("%1 listens waiting").arg(n);
    switch (m_s->account()) {
    case mmm::Scrobbler::Account::Connected:
        if (!m_s->isEnabled())
            return QStringLiteral("Paused: plays aren't sent");
        if (n == 0)
            return QStringLiteral("Up to date");
        return m_s->isRetrying() ? waiting + QStringLiteral(": retrying automatically") : waiting;
    case mmm::Scrobbler::Account::Expired:
        return n > 0 ? QStringLiteral("The token stopped working: connect again (%1)").arg(waiting)
                     : QStringLiteral("The token stopped working: connect again");
    default:
        return n > 0 ? waiting : QString();
    }
}

QString ListenBrainz::toolTip() const
{
    switch (m_s->account()) {
    case mmm::Scrobbler::Account::Connected: {
        QString t = m_s->isEnabled() ? QStringLiteral("ListenBrainz: submitting as %1").arg(m_s->user())
                                     : QStringLiteral("ListenBrainz: off (%1)").arg(m_s->user());
        if (const int n = m_s->queued(); n > 0)
            t += QStringLiteral(", %1 waiting").arg(n);
        return t;
    }
    case mmm::Scrobbler::Account::Expired:
        return QStringLiteral("ListenBrainz: the token stopped working. Click to connect again");
    case mmm::Scrobbler::Account::Disconnected:
        return QStringLiteral("ListenBrainz: not connected. Click to set up");
    case mmm::Scrobbler::Account::Checking:
        return QStringLiteral("ListenBrainz: checking the token…");
    }
    return {};
}

QString ListenBrainz::secretStoreHint() const
{
    if (m_options.secrets->backend() == mmm::SecretBackend::Pass)
        return QStringLiteral("It's kept in pass (%1).")
            .arg(m_options.secrets->entryLocation(QString::fromLatin1(mmm::Scrobbler::kTokenKey)));
    return QStringLiteral("It's kept in your keyring.");
}

void ListenBrainz::openTokenPage()
{
    m_options.openUrl(mmm::ListenBrainzClient::tokenPageUrl());
}
