// SPDX-License-Identifier: GPL-3.0-or-later
#include "listenbrainzclient.h"

#include "appinfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>

Q_LOGGING_CATEGORY(lcListenBrainz, "mmm.listenbrainz")

namespace mmm {

namespace {

constexpr int kTimeoutMs = 20000;

QJsonObject trackMetadata(const ScrobbleTrack &t)
{
    QJsonObject info;
    if (t.durationMs > 0)
        info.insert(QStringLiteral("duration_ms"), t.durationMs);
    if (t.trackNumber > 0)
        info.insert(QStringLiteral("tracknumber"), t.trackNumber);
    info.insert(QStringLiteral("media_player"), AppInfo::displayName());
    info.insert(QStringLiteral("submission_client"), AppInfo::displayName());
    info.insert(QStringLiteral("submission_client_version"), AppInfo::version());
    QJsonObject meta{{QStringLiteral("artist_name"), t.artist}, {QStringLiteral("track_name"), t.title}};
    if (!t.album.isEmpty())
        meta.insert(QStringLiteral("release_name"), t.album);
    meta.insert(QStringLiteral("additional_info"), info);
    return meta;
}

QByteArray body(const QString &listenType, const QJsonArray &payload)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("listen_type"), listenType}, {QStringLiteral("payload"), payload}})
        .toJson(QJsonDocument::Compact);
}

int headerInt(const QNetworkReply *reply, const char *name, int fallback)
{
    bool ok = false;
    const int v = reply->rawHeader(name).trimmed().toInt(&ok);
    return ok ? v : fallback;
}

} // namespace

QString ListenBrainzResult::describe() const
{
    switch (kind) {
    case None:
        return {};
    case Network:
        return QStringLiteral("Can't reach ListenBrainz (offline?)");
    case Temporary:
        return QStringLiteral("ListenBrainz is busy: trying again later");
    case Unauthorized:
        return QStringLiteral("ListenBrainz doesn't accept the token: paste it again from your settings page");
    case Invalid:
        return message.isEmpty() ? QStringLiteral("ListenBrainz refused the request") : QStringLiteral("ListenBrainz: %1").arg(message);
    case Other:
        break;
    }
    return message.isEmpty() ? QStringLiteral("ListenBrainz error (HTTP %1)").arg(httpStatus) : QStringLiteral("ListenBrainz: %1").arg(message);
}

ListenBrainzClient::ListenBrainzClient(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
{
    setBaseUrl(defaultBaseUrl());
}

QUrl ListenBrainzClient::defaultBaseUrl()
{
    const QString env = qEnvironmentVariable("MMM_LISTENBRAINZ_URL");
    return QUrl(env.isEmpty() ? QStringLiteral("https://api.listenbrainz.org/") : env);
}

QUrl ListenBrainzClient::tokenPageUrl()
{
    return QUrl(QStringLiteral("https://listenbrainz.org/settings/"));
}

bool ListenBrainzClient::plausibleToken(const QString &token)
{
    return !token.isEmpty() && token.size() <= 256
        && std::all_of(token.cbegin(), token.cend(), [](QChar c) { return c.unicode() > 0x20 && c.unicode() < 0x7f; });
}

void ListenBrainzClient::setBaseUrl(const QUrl &url)
{
    // Endpoints are resolved against it ("1/submit-listens"): the path must end in "/".
    m_baseUrl = url;
    if (!m_baseUrl.path().endsWith(QLatin1Char('/')))
        m_baseUrl.setPath(m_baseUrl.path() + QLatin1Char('/'));
}

QByteArray ListenBrainzClient::playingNowBody(const ScrobbleTrack &track)
{
    return body(QStringLiteral("playing_now"), QJsonArray{QJsonObject{{QStringLiteral("track_metadata"), trackMetadata(track)}}});
}

QByteArray ListenBrainzClient::listensBody(const QList<PendingScrobble> &batch)
{
    QJsonArray payload;
    for (const PendingScrobble &s : batch) {
        payload.append(QJsonObject{{QStringLiteral("listened_at"), s.startedAt},
                                   {QStringLiteral("track_metadata"), trackMetadata(s.track)}});
    }
    return body(batch.size() == 1 ? QStringLiteral("single") : QStringLiteral("import"), payload);
}

void ListenBrainzClient::call(const QByteArray &verb, const QString &endpoint, const QString &token, const QByteArray &body,
                              QObject *context, std::function<void(const QJsonObject &, const ListenBrainzResult &)> done)
{
    QPointer<QObject> guard(context);
    if (!m_nam || !plausibleToken(token)) {
        ListenBrainzResult r;
        r.kind = m_nam ? ListenBrainzResult::Unauthorized : ListenBrainzResult::Other;
        QMetaObject::invokeMethod(this, [guard, done, r] {
            if (guard)
                done({}, r);
        }, Qt::QueuedConnection);
        return;
    }
    QNetworkRequest request(m_baseUrl.resolved(QUrl(endpoint)));
    request.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Token ") + token.toLatin1());
    request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, AppInfo::userAgent());
    request.setTransferTimeout(kTimeoutMs);
    // The token goes to ListenBrainz only: never along a redirect.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply *reply = nullptr;
    if (verb == "POST") {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        reply = m_nam->post(request, body);
    } else {
        reply = m_nam->get(request);
    }
    // (Deleted when done even if this client is gone by then.)
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, this, [reply, guard, done, endpoint] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        ListenBrainzResult r;
        r.httpStatus = status;
        r.remaining = headerInt(reply, "X-RateLimit-Remaining", -1);
        r.resetInS = std::max(0, headerInt(reply, "X-RateLimit-Reset-In", headerInt(reply, "Retry-After", 0)));
        if (status >= 200 && status < 300 && reply->error() == QNetworkReply::NoError) {
            if (json.isEmpty()) {
                r.kind = ListenBrainzResult::Other;
                r.message = QStringLiteral("its answer couldn't be read");
            }
        } else {
            // 413: too big a request, like a refused one (a batch is split, a single listen dropped).
            // A good status cut short (the connection dropped mid-answer) is the network's fault.
            r.kind = status == 401                           ? ListenBrainzResult::Unauthorized
                : status == 400 || status == 413             ? ListenBrainzResult::Invalid
                : status == 429 || status >= 500             ? ListenBrainzResult::Temporary
                : status > 0 && (status < 200 || status >= 300) ? ListenBrainzResult::Other
                                                             : ListenBrainzResult::Network;
            r.message = json.value(QStringLiteral("error")).toString();
            if (r.message.isEmpty() && r.kind != ListenBrainzResult::Network)
                r.message = json.value(QStringLiteral("message")).toString();
            if (r.message.isEmpty() && r.kind == ListenBrainzResult::Network)
                r.message = reply->errorString();
            qCInfo(lcListenBrainz) << endpoint << "failed:" << status << r.message;
        }
        if (guard)
            done(json, r);
    });
}

void ListenBrainzClient::validateToken(const QString &token, QObject *context,
                                       std::function<void(const QString &, const ListenBrainzResult &)> done)
{
    call("GET", QStringLiteral("1/validate-token"), token.trimmed(), {}, context,
         [done](const QJsonObject &json, const ListenBrainzResult &r) {
             ListenBrainzResult result = r;
             const QString user = json.value(QStringLiteral("user_name")).toString();
             // An unknown token is still HTTP 200, with "valid": false.
             if (result.ok() && (!json.value(QStringLiteral("valid")).toBool() || user.isEmpty())) {
                 result.kind = ListenBrainzResult::Unauthorized;
                 result.message = json.value(QStringLiteral("message")).toString();
             }
             done(result.ok() ? user : QString(), result);
         });
}

void ListenBrainzClient::playingNow(const ScrobbleTrack &track, QObject *context, std::function<void(const ListenBrainzResult &)> done)
{
    call("POST", QStringLiteral("1/submit-listens"), m_token, playingNowBody(track), context,
         [done](const QJsonObject &, const ListenBrainzResult &r) { done(r); });
}

void ListenBrainzClient::submit(const QList<PendingScrobble> &batch, QObject *context, std::function<void(const ListenBrainzResult &)> done)
{
    Q_ASSERT(!batch.isEmpty() && batch.size() <= kMaxBatch);
    call("POST", QStringLiteral("1/submit-listens"), m_token, listensBody(batch), context,
         [done](const QJsonObject &, const ListenBrainzResult &r) { done(r); });
}

} // namespace mmm
