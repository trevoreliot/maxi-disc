// SPDX-License-Identifier: GPL-3.0-or-later
#include "scrobbler.h"

#include <QLoggingCategory>
#include <QNetworkInformation>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcListenBrainz)

namespace mmm {

namespace {

const QString kToken = QString::fromLatin1(Scrobbler::kTokenKey);

QString label(const ScrobbleTrack &t)
{
    return t.artist + QStringLiteral(" – ") + t.title;
}

} // namespace

Scrobbler::Scrobbler(Options options, QObject *parent)
    : QObject(parent)
    , m_options(std::move(options))
    , m_client(&m_nam)
    , m_tracker(m_options.monotonicMs, m_options.utcSeconds)
{
    if (!m_options.secrets)
        m_options.secrets = makeKeychainSecretStore();
    if (!m_options.baseUrl.isEmpty())
        m_client.setBaseUrl(m_options.baseUrl);
    m_requestClock.start();
    QString error;
    if (!m_db.open(m_options.databasePath, &error))
        qCWarning(lcListenBrainz) << "can't open the scrobble queue:" << error;

    m_dueTimer.setSingleShot(true);
    connect(&m_dueTimer, &QTimer::timeout, this, [this] {
        counted(m_tracker.check());
        armDue();
    });
    m_nowPlayingTimer.setSingleShot(true);
    connect(&m_nowPlayingTimer, &QTimer::timeout, this, &Scrobbler::sendNowPlaying);
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, &Scrobbler::flush);

    if (m_options.watchNetwork && QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability)) {
        connect(QNetworkInformation::instance(), &QNetworkInformation::reachabilityChanged, this,
                [this](QNetworkInformation::Reachability r) {
                    if (r == QNetworkInformation::Reachability::Online)
                        networkCameBack();
                });
    }
}

Scrobbler::~Scrobbler()
{
    // Quitting: a play that's due by now still counts (sent next time, not now).
    m_quitting = true;
    playEnded();
}

int Scrobbler::queued() const
{
    return m_db.isOpen() ? m_db.pendingScrobbleCount() : 0;
}

void Scrobbler::setAccount(Account a)
{
    if (a == m_account)
        return;
    m_account = a;
    Q_EMIT changed();
}

void Scrobbler::setError(const QString &error)
{
    if (error == m_lastError)
        return;
    m_lastError = error;
    Q_EMIT changed();
}

// --- The account ---

void Scrobbler::restore(const QString &user, bool enabled)
{
    m_user = user;
    m_enabled = enabled;
    ++m_session;
    const quint64 generation = ++m_restoreGeneration;
    m_client.setToken({});
    m_retryTimer.stop();
    m_nowPlayingTimer.stop();
    m_keyringError.clear();
    if (m_user.isEmpty()) {
        m_restoring = false;
        setAccount(Account::Disconnected);
        Q_EMIT changed();
        return;
    }
    // The keyring may take a while (an unlock prompt): plays meanwhile still count.
    m_restoring = true;
    m_options.secrets->read(kToken, this, [this, generation](const QString &token, const QString &error) {
        if (generation != m_restoreGeneration)
            return;
        m_restoring = false;
        if (!error.isEmpty()) {
            m_keyringError = error;
            qCWarning(lcListenBrainz) << "can't read the keyring:" << error;
        }
        if (!error.isEmpty() || !ListenBrainzClient::plausibleToken(token.trimmed())) {
            // A known user without a usable token: as good as expired (plays wait for a new one).
            setAccount(Account::Expired);
            Q_EMIT changed();
            return;
        }
        m_client.setToken(token);
        setAccount(Account::Connected);
        Q_EMIT queueChanged();
        flushSoon();
        if (m_tracker.isPlaying() && !m_nowPlayingSent)
            m_nowPlayingTimer.start(m_options.nowPlayingDelayMs);
    });
}

void Scrobbler::connectAccount(const QString &token)
{
    // Connected already: disconnect first (a half-done switch would leave two accounts' state).
    if (m_account == Account::Checking || m_account == Account::Connected)
        return;
    const QString t = token.trimmed();
    if (!ListenBrainzClient::plausibleToken(t)) {
        setError(t.isEmpty() ? QStringLiteral("Paste your user token first.")
                             : QStringLiteral("That doesn't look like a ListenBrainz user token."));
        return;
    }
    ++m_restoreGeneration; // a restore still reading the keyring is over
    m_restoring = false;
    const quint64 session = ++m_session;
    m_beforeConnect = m_account;
    m_retryTimer.stop();
    setError({});
    setAccount(Account::Checking);
    requestSent();
    m_client.validateToken(t, this, [this, session, t](const QString &user, const ListenBrainzResult &r) {
        if (session != m_session || m_account != Account::Checking)
            return; // canceled, or started over
        noteRateLimit(r);
        if (!r.ok()) {
            setError(r.kind == ListenBrainzResult::Unauthorized
                         ? QStringLiteral("ListenBrainz doesn't know that token: copy it again from your settings page.")
                         : r.describe());
            setAccount(m_beforeConnect);
            return;
        }
        m_user = user;
        m_enabled = true;
        m_client.setToken(t);
        m_keyringError.clear();
        m_oneByOne = 0;
        m_droppedOne = false;
        m_options.secrets->write(kToken, t, this, [this, session](const QString &error) {
            if (!error.isEmpty() && session == m_session) {
                m_keyringError = error;
                qCWarning(lcListenBrainz) << "can't save the token in the keyring:" << error;
                Q_EMIT changed();
            }
        });
        m_backoffStep = 0;
        setAccount(Account::Connected);
        Q_EMIT settingsChanged();
        Q_EMIT changed();
        flushSoon();
        if (m_tracker.isPlaying() && !m_nowPlayingSent)
            m_nowPlayingTimer.start(m_options.nowPlayingDelayMs);
    });
}

void Scrobbler::cancelConnect()
{
    if (m_account != Account::Checking)
        return;
    ++m_session;
    setError({});
    setAccount(m_beforeConnect);
    Q_EMIT changed();
}

void Scrobbler::disconnectAccount()
{
    ++m_restoreGeneration;
    ++m_session;
    m_restoring = false;
    m_options.secrets->remove(kToken, this);
    m_client.setToken({});
    m_user.clear();
    m_enabled = false;
    m_keyringError.clear();
    m_oneByOne = 0;
    m_droppedOne = false;
    m_retryTimer.stop();
    m_nowPlayingTimer.stop();
    m_backoffStep = 0;
    setError({});
    setAccount(Account::Disconnected);
    Q_EMIT settingsChanged();
    Q_EMIT changed();
}

void Scrobbler::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (!on)
        m_nowPlayingTimer.stop();
    Q_EMIT settingsChanged();
    Q_EMIT changed();
    if (on)
        flushSoon();
}

void Scrobbler::retryNow()
{
    const bool wasRetrying = isRetrying();
    m_retryTimer.stop();
    m_backoffStep = 0;
    if (wasRetrying)
        Q_EMIT changed();
    flush();
}

void Scrobbler::networkCameBack()
{
    if (canSend() && !m_flushing && queued() > 0)
        retryNow();
}

// --- Playback ---

void Scrobbler::counted(const std::optional<PendingScrobble> &play)
{
    // Plays count while scrobbling is on (also while the token is expired: they wait for a new one).
    const bool waiting = m_account == Account::Expired || m_restoring
        || (m_account == Account::Checking && m_beforeConnect == Account::Expired);
    if (!play || !m_enabled || (m_account != Account::Connected && !waiting))
        return;
    QString error;
    if (!m_db.addPendingScrobble(*play, 5000, &error)) {
        qCWarning(lcListenBrainz) << "can't queue a scrobble:" << error;
        return;
    }
    m_lastListen = label(play->track);
    if (m_quitting)
        return;
    Q_EMIT queueChanged();
    Q_EMIT changed();
    flushSoon();
}

void Scrobbler::armDue()
{
    if (const std::optional<qint64> ms = m_tracker.msUntilDue())
        m_dueTimer.start(static_cast<int>(std::min<qint64>(*ms, 24 * 3600'000)));
    else
        m_dueTimer.stop();
}

void Scrobbler::playStarted(const TrackRow &track, bool playing)
{
    counted(m_tracker.start(ScrobbleTrack::from(track), playing));
    m_nowPlayingSent = false;
    m_nowPlayingTimer.stop();
    if (playing)
        m_nowPlayingTimer.start(m_options.nowPlayingDelayMs);
    armDue();
}

void Scrobbler::playRetagged(const TrackRow &track)
{
    m_tracker.retag(ScrobbleTrack::from(track));
    if (m_tracker.isPlaying() && !m_nowPlayingSent && !m_nowPlayingTimer.isActive())
        m_nowPlayingTimer.start(m_options.nowPlayingDelayMs); // titled now, perhaps
    armDue();
}

void Scrobbler::playingChanged(bool playing)
{
    counted(m_tracker.setPlaying(playing));
    if (playing && !m_nowPlayingSent && m_tracker.hasPlay())
        m_nowPlayingTimer.start(m_options.nowPlayingDelayMs);
    else if (!playing)
        m_nowPlayingTimer.stop();
    armDue();
}

void Scrobbler::playDurationChanged(qint64 durationMs)
{
    m_tracker.setDuration(durationMs);
    armDue();
}

void Scrobbler::playEnded()
{
    counted(m_tracker.stop());
    m_nowPlayingTimer.stop();
    m_dueTimer.stop();
}

void Scrobbler::sendNowPlaying()
{
    const ScrobbleTrack track = m_tracker.track();
    if (!canSend() || !m_tracker.isPlaying() || track.artist.trimmed().isEmpty() || track.title.trimmed().isEmpty())
        return;
    if (const qint64 wait = msUntilNextRequest(); wait > 0) {
        m_nowPlayingTimer.start(static_cast<int>(wait));
        return;
    }
    m_nowPlayingSent = true;
    const quint64 generation = m_session;
    requestSent();
    m_client.playingNow(track, this, [this, generation](const ListenBrainzResult &r) {
        // Best effort: only a token that stopped working matters (this one's, not an older one's).
        noteRateLimit(r);
        if (generation == m_session && r.kind == ListenBrainzResult::Unauthorized)
            lostToken(r);
    });
}

// --- Sending ---

qint64 Scrobbler::msUntilNextRequest() const
{
    return std::max<qint64>(0, m_nextRequestAt - m_requestClock.elapsed());
}

void Scrobbler::requestSent()
{
    m_nextRequestAt = std::max(m_nextRequestAt, m_requestClock.elapsed() + m_options.minRequestGapMs);
}

void Scrobbler::noteRateLimit(const ListenBrainzResult &r)
{
    // The window is used up (or we were told off): nothing until it resets (an hour at most).
    if ((r.remaining == 0 || r.httpStatus == 429) && r.resetInS > 0)
        m_nextRequestAt = std::max(m_nextRequestAt, m_requestClock.elapsed() + std::min(r.resetInS, 3600) * qint64(1000));
}

void Scrobbler::flushSoon()
{
    if (canSend() && !m_flushing && !m_retryTimer.isActive())
        flush();
}

void Scrobbler::flush()
{
    if (!canSend() || m_flushing)
        return;
    const QList<PendingScrobble> batch = m_db.pendingScrobbles(m_oneByOne > 0 ? 1 : ListenBrainzClient::kMaxBatch);
    if (batch.isEmpty()) {
        m_backoffStep = 0;
        m_oneByOne = 0;
        return;
    }
    if (const qint64 wait = msUntilNextRequest(); wait > 0) {
        m_retryTimer.start(static_cast<int>(wait));
        return;
    }
    m_flushing = true;
    const quint64 generation = m_session;
    requestSent();
    m_client.submit(batch, this, [this, batch, generation](const ListenBrainzResult &r) {
        m_flushing = false;
        noteRateLimit(r);
        if (generation != m_session) {
            // Sent for an account that's gone since. Taken is taken (whichever account it was);
            // otherwise they wait for the one there is now. Its state isn't this answer's to touch.
            if (r.ok()) {
                QList<qint64> ids;
                for (const PendingScrobble &s : batch)
                    ids << s.id;
                m_db.removePendingScrobbles(ids);
                Q_EMIT queueChanged();
            }
            flushSoon();
            return;
        }
        sent(batch, r);
    });
}

void Scrobbler::sent(const QList<PendingScrobble> &batch, const ListenBrainzResult &r)
{
    QList<qint64> all;
    for (const PendingScrobble &s : batch)
        all << s.id;
    const auto next = [this] {
        if (canSend() && queued() > 0 && !m_retryTimer.isActive())
            m_retryTimer.start(std::max<int>(m_options.batchGapMs, static_cast<int>(msUntilNextRequest())));
    };
    if (r.ok()) {
        m_db.removePendingScrobbles(all);
        m_oneByOne = std::max(0, m_oneByOne - static_cast<int>(batch.size()));
        m_droppedOne = false;
        m_backoffStep = 0;
        setError({});
        Q_EMIT queueChanged();
        next();
        return;
    }
    switch (r.kind) {
    case ListenBrainzResult::Invalid:
        if (batch.size() > 1) {
            m_oneByOne = static_cast<int>(batch.size()); // find the bad one: these one by one
            next();
            return;
        }
        if (!m_droppedOne) {
            // This one's the bad one: it'll never go.
            qCWarning(lcListenBrainz) << "ListenBrainz refused a listen for good:" << r.message;
            m_db.removePendingScrobbles(all);
            m_oneByOne = std::max(0, m_oneByOne - 1);
            m_droppedOne = true;
            Q_EMIT queueChanged();
            next();
            return;
        }
        // Two refused in a row: perhaps it's not the listens. Keep them; try again later.
        m_db.notePendingScrobbleFailure(all, r.describe());
        setError(r.describe());
        retryLater();
        return;
    case ListenBrainzResult::Unauthorized:
        lostToken(r);
        return;
    default:
        m_db.notePendingScrobbleFailure(all, r.describe());
        setError(r.describe());
        retryLater(r.resetInS);
        return;
    }
}

void Scrobbler::retryLater(int atLeastS)
{
    const qint64 delay = std::min<qint64>(qint64(m_options.retryFirstMs) << std::min(m_backoffStep, 10), m_options.retryMaxMs);
    ++m_backoffStep;
    m_droppedOne = false; // the next try may drop one refused listen again
    // The server's wait: capped (an hour at most).
    const qint64 server = std::clamp<qint64>(atLeastS, 0, 3600) * 1000;
    m_retryTimer.start(static_cast<int>(std::max({delay, server, msUntilNextRequest()})));
    Q_EMIT changed();
}

void Scrobbler::lostToken(const ListenBrainzResult &r)
{
    if (m_account != Account::Connected)
        return;
    ++m_session;
    m_retryTimer.stop();
    m_nowPlayingTimer.stop();
    m_client.setToken({});
    m_options.secrets->remove(kToken, this);
    setError(r.describe());
    setAccount(Account::Expired);
}

} // namespace mmm
