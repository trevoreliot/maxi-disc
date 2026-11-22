// SPDX-License-Identifier: GPL-3.0-or-later
#include "coverartarchiveclient.h"

#include "appinfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace mmm {

namespace {

// Listings still carry http:// URLs; fetch them over https (the redirects to archive.org
// stay https, which Qt's default redirect policy requires).
QUrl secure(const QString &s)
{
    QUrl url(s);
    if (url.scheme() == QLatin1String("http") && url.host().endsWith(QLatin1String("coverartarchive.org")))
        url.setScheme(QStringLiteral("https"));
    return url;
}

} // namespace

QUrl CaaImage::previewUrl() const
{
    for (const QUrl &u : {thumb500, thumb250, thumb1200, image}) {
        if (u.isValid() && !u.isEmpty())
            return u;
    }
    return {};
}

QUrl CaaImage::saveUrl() const
{
    for (const QUrl &u : {thumb1200, thumb500, image}) {
        if (u.isValid() && !u.isEmpty())
            return u;
    }
    return {};
}

CoverArtArchiveClient::CoverArtArchiveClient(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent), m_fetcher(nam, HttpFetcher::Options{4, 0, 2, 30000, 2000}, this)
{
    m_fetcher.setUserAgent(AppInfo::userAgent());
}

std::optional<CaaImage> CoverArtArchiveClient::parseFront(const QByteArray &json, QString *error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("unexpected answer from the Cover Art Archive (%1)").arg(pe.errorString());
        return std::nullopt;
    }
    for (const QJsonValue &v : doc.object().value(QLatin1String("images")).toArray()) {
        const QJsonObject o = v.toObject();
        if (!o.value(QLatin1String("front")).toBool())
            continue;
        const QJsonObject thumbs = o.value(QLatin1String("thumbnails")).toObject();
        auto thumb = [&](const char *a, const char *b) {
            const QString s = thumbs.value(QLatin1String(a)).toString();
            return secure(s.isEmpty() ? thumbs.value(QLatin1String(b)).toString() : s);
        };
        CaaImage img;
        const QJsonValue id = o.value(QLatin1String("id"));
        img.id = id.isString() ? id.toString() : QString::number(id.toInteger());
        img.image = secure(o.value(QLatin1String("image")).toString());
        img.thumb250 = thumb("250", "small");
        img.thumb500 = thumb("500", "large");
        img.thumb1200 = thumb("1200", "1200");
        if (img.saveUrl().isEmpty())
            continue;
        return img;
    }
    return std::nullopt;
}

void CoverArtArchiveClient::frontCover(Kind kind, const QString &mbid, QObject *context, FrontCallback done)
{
    QUrl url = m_baseUrl;
    url.setPath(url.path() + (kind == Kind::Release ? QStringLiteral("/release/") : QStringLiteral("/release-group/"))
                + mbid.trimmed().toLower());
    m_fetcher.get(url, context, [done = std::move(done)](const HttpResult &r) {
        if (r.status == 404 || r.status == 400) {
            done(std::nullopt, QString());
            return;
        }
        if (!r.ok()) {
            done(std::nullopt, QStringLiteral("Cover Art Archive: %1").arg(r.describe()));
            return;
        }
        QString error;
        const auto front = parseFront(r.body, &error);
        done(front, error);
    });
}

} // namespace mmm
