// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QUrl>

#include <memory>

#include "database.h"
#include "listenbrainzclient.h"
#include "scrobbletracker.h"
#include "secretstore.h"

namespace mmm {

// Scrobbling to ListenBrainz (M14): the account, "playing now", and the plays that count,
// queued in the database until ListenBrainz has them. Lives on the UI thread; never blocks it
// (the network and the keyring answer asynchronously) and playback never waits for it. Fed by
// the UI's playback wiring (ListenBrainz) through the play*() calls.
//
// Connecting: the user pastes their ListenBrainz user token; it's checked (validate-token) and
// kept in the keyring (SecretStore). The user name and on/off are the caller's to keep
// (settingsChanged).
//
// Sending: right away, then retried after 1, 2, 4 … 30 minutes while offline or ListenBrainz is
// busy, at once when the network comes back (networkCameBack, or the system's reachability) or
// on retryNow(). 50 per request, oldest first; never more than one request a second (and none
// while the server's rate limit window is used up). A batch refused as invalid is sent again
// one by one: the first listen refused alone is dropped, a second in a row waits for the next
// retry instead (so a server that refuses everything can't empty the queue at once). A token
// that stops working keeps the queue (and keeps adding to it) until the user connects again.
class Scrobbler : public QObject
{
    Q_OBJECT

public:
    enum class Account {
        Disconnected,
        Checking,     // asking ListenBrainz whether the token is good
        Connected,
        Expired,      // the token stopped working (or can't be read): connect again
    };
    Q_ENUM(Account)

    struct Options
    {
        QString databasePath;                  // the queue's database
        std::shared_ptr<SecretStore> secrets;  // default: the desktop keyring
        QUrl baseUrl;                          // default: ListenBrainzClient::defaultBaseUrl()
        ScrobbleTracker::Clock monotonicMs;    // default: real time (tests: fake)
        ScrobbleTracker::Clock utcSeconds;
        int nowPlayingDelayMs = 2500;          // skipping through tracks sends nothing
        int retryFirstMs = 60'000;
        int retryMaxMs = 30 * 60'000;
        int batchGapMs = 1000;                 // between batches of a long queue
        int minRequestGapMs = 1000;            // ListenBrainz: at most one request a second
        bool watchNetwork = true;              // retry when the system says it's online again
    };

    explicit Scrobbler(Options options, QObject *parent = nullptr);
    ~Scrobbler() override;

    // --- The account ---
    // What was saved: reads the keyring, connected if it has the token.
    void restore(const QString &user, bool enabled);
    // Checks the token, then keeps it. Not while connected (disconnect first).
    void connectAccount(const QString &token);
    void cancelConnect();
    void disconnectAccount();
    void setEnabled(bool on);
    void retryNow();
    void networkCameBack();

    Account account() const { return m_account; }
    bool isConnected() const { return m_account == Account::Connected; }
    QString user() const { return m_user; }
    bool isEnabled() const { return m_enabled; }
    QString lastError() const { return m_lastError; }   // "" if the last thing worked
    QString keyringError() const { return m_keyringError; }
    QString lastListen() const { return m_lastListen; } // "Artist – Title"
    int queued() const;
    bool isRetrying() const { return m_retryTimer.isActive() && m_backoffStep > 0; }

    // --- Playback (in this order for a track: playStarted, then the others) ---
    void playStarted(const TrackRow &track, bool playing);
    void playRetagged(const TrackRow &track);
    void playingChanged(bool playing);
    void playDurationChanged(qint64 durationMs);
    void playEnded(); // stopped for good: cleared, quitting

    static constexpr auto kTokenKey = "listenbrainz/token";

Q_SIGNALS:
    void changed();         // account, user, enabled, errors, last listen
    void queueChanged();
    void settingsChanged(); // the user or on/off to save

private:
    void setAccount(Account a);
    void setError(const QString &error);
    void counted(const std::optional<PendingScrobble> &play);
    void armDue();
    void sendNowPlaying();
    bool canSend() const { return m_account == Account::Connected && m_enabled; }
    void flushSoon();
    void flush();
    void sent(const QList<PendingScrobble> &batch, const ListenBrainzResult &r);
    void retryLater(int atLeastS = 0);
    void lostToken(const ListenBrainzResult &r);
    // The one-request-a-second gate (real time, not the tracker's clock).
    qint64 msUntilNextRequest() const;
    void requestSent();
    void noteRateLimit(const ListenBrainzResult &r);

    Options m_options;
    Database m_db;
    QNetworkAccessManager m_nam;
    ListenBrainzClient m_client;
    ScrobbleTracker m_tracker;
    QTimer m_dueTimer;
    QTimer m_nowPlayingTimer;
    QTimer m_retryTimer;

    Account m_account = Account::Disconnected;
    Account m_beforeConnect = Account::Disconnected;
    QString m_user;
    bool m_enabled = false;
    QString m_lastError;
    QString m_keyringError;
    QString m_lastListen;
    bool m_nowPlayingSent = false;
    bool m_flushing = false;
    int m_backoffStep = 0;
    int m_oneByOne = 0;       // send this many singly (a refused batch)
    bool m_droppedOne = false; // a listen refused alone was dropped since the last success / retry
    QElapsedTimer m_requestClock;
    qint64 m_nextRequestAt = 0; // m_requestClock ms
    quint64 m_restoreGeneration = 0;
    // Bumped whenever the account changes (connect, cancel, disconnect, lost, restore): answers to
    // requests made before are dropped, so an old request can't touch the new session.
    quint64 m_session = 0;
    bool m_restoring = false;  // reading the keyring at startup: plays still count
    bool m_quitting = false;
};

} // namespace mmm
