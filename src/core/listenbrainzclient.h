// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <functional>

#include "scrobbletypes.h"

class QNetworkAccessManager;

namespace mmm {

// How a ListenBrainz call went, sorted by what to do about it, plus the server's rate limit
// (sent with every answer, good or bad).
struct ListenBrainzResult
{
    enum Kind {
        None,
        Network,      // no answer (offline, DNS, timeout): retry later
        Temporary,    // busy, down or rate limited (HTTP 429, 5xx): retry later
        Unauthorized, // the token is no good (HTTP 401, or validate-token says so): connect again
        Invalid,      // the request itself was refused (HTTP 400): don't send it again
        Other,        // anything else (an unexpected status, an answer that can't be read)
    };
    Kind kind = None;
    int httpStatus = 0;
    int remaining = -1; // X-RateLimit-Remaining (-1: not said)
    int resetInS = 0;   // X-RateLimit-Reset-In (or Retry-After): seconds until the window resets
    QString message;    // the server's own words, if any
    bool ok() const { return kind == None; }
    bool retryable() const { return kind == Network || kind == Temporary || kind == Other; }
    QString describe() const;
};

// The ListenBrainz API (M14): checks a user token, sends "playing now" and listens. JSON over
// HTTPS, the user token in the Authorization header (no app key, no signing). Asynchronous;
// callbacks run on this object's thread unless `context` is gone first. Base URL:
// MMM_LISTENBRAINZ_URL, else ListenBrainz's (tests point it at a loopback server). Never logs
// the token.
class ListenBrainzClient : public QObject
{
    Q_OBJECT

public:
    explicit ListenBrainzClient(QNetworkAccessManager *nam, QObject *parent = nullptr);

    static QUrl defaultBaseUrl();
    // Where users find their token (opened in their browser).
    static QUrl tokenPageUrl();
    // A token that can go in a header: not empty, no spaces or control characters.
    static bool plausibleToken(const QString &token);
    // The submit-listens body: "playing_now" (one track, no timestamp), "single" (one listen) or
    // "import" (several).
    static QByteArray playingNowBody(const ScrobbleTrack &track);
    static QByteArray listensBody(const QList<PendingScrobble> &batch);

    void setBaseUrl(const QUrl &url);
    QUrl baseUrl() const { return m_baseUrl; }
    void setToken(const QString &token) { m_token = token.trimmed(); }
    bool hasToken() const { return !m_token.isEmpty(); }

    // Checks `token` (not the one set): its user name if ListenBrainz knows it.
    void validateToken(const QString &token, QObject *context,
                       std::function<void(const QString &user, const ListenBrainzResult &)> done);
    void playingNow(const ScrobbleTrack &track, QObject *context, std::function<void(const ListenBrainzResult &)> done);
    // At most kMaxBatch, oldest first. All taken or none.
    void submit(const QList<PendingScrobble> &batch, QObject *context, std::function<void(const ListenBrainzResult &)> done);

    static constexpr int kMaxBatch = 50; // ListenBrainz takes up to 1000

private:
    void call(const QByteArray &verb, const QString &endpoint, const QString &token, const QByteArray &body,
              QObject *context, std::function<void(const QJsonObject &, const ListenBrainzResult &)> done);

    QPointer<QNetworkAccessManager> m_nam;
    QUrl m_baseUrl;
    QString m_token;
};

} // namespace mmm
