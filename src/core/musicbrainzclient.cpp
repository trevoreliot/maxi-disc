// SPDX-License-Identifier: GPL-3.0-or-later
#include "musicbrainzclient.h"

#include "appinfo.h"
#include "artworkmatch.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>

namespace mmm {

namespace {

// Inside a quoted Lucene phrase only `"` and `\` are special.
QString quoted(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + out + QLatin1Char('"');
}

// Loose: words only (punctuation, including every Lucene operator character, removed).
QString words(const QString &s)
{
    static const QRegularExpression nonWord(QStringLiteral("[^\\w]+"),
                                            QRegularExpression::UseUnicodePropertiesOption);
    QStringList parts = s.split(nonWord, Qt::SkipEmptyParts);
    // Lucene keywords would be read as operators.
    parts.removeIf([](const QString &w) {
        return w == QLatin1String("AND") || w == QLatin1String("OR") || w == QLatin1String("NOT");
    });
    return parts.join(QLatin1Char(' '));
}

MbRelease releaseFrom(const QJsonObject &o)
{
    MbRelease r;
    r.id = o.value(QLatin1String("id")).toString();
    r.title = o.value(QLatin1String("title")).toString();
    r.status = o.value(QLatin1String("status")).toString();
    r.country = o.value(QLatin1String("country")).toString();
    r.searchScore = o.value(QLatin1String("score")).toInt();
    r.year = o.value(QLatin1String("date")).toString().left(4).toInt();
    r.releaseGroupId = o.value(QLatin1String("release-group")).toObject().value(QLatin1String("id")).toString();

    for (const QJsonValue &c : o.value(QLatin1String("artist-credit")).toArray()) {
        const QJsonObject credit = c.toObject();
        QString name = credit.value(QLatin1String("name")).toString();
        if (name.isEmpty())
            name = credit.value(QLatin1String("artist")).toObject().value(QLatin1String("name")).toString();
        r.artist += name + credit.value(QLatin1String("joinphrase")).toString();
    }

    QStringList formats;
    int mediaTracks = 0;
    for (const QJsonValue &m : o.value(QLatin1String("media")).toArray()) {
        const QJsonObject medium = m.toObject();
        mediaTracks += medium.value(QLatin1String("track-count")).toInt();
        const QString f = medium.value(QLatin1String("format")).toString();
        formats << (f.isEmpty() ? QStringLiteral("?") : f);
    }
    r.trackCount = o.contains(QLatin1String("track-count")) ? o.value(QLatin1String("track-count")).toInt() : mediaTracks;
    // "2×CD" when every medium is the same, else "CD + DVD".
    if (!formats.isEmpty()) {
        QStringList distinct = formats;
        distinct.removeDuplicates();
        r.format = distinct.size() == 1
            ? (formats.size() > 1 ? QStringLiteral("%1×%2").arg(formats.size()).arg(distinct.front()) : distinct.front())
            : distinct.join(QStringLiteral(" + "));
        if (r.format == QLatin1String("?"))
            r.format.clear();
    }
    return r;
}

std::optional<QJsonObject> parseObject(const QByteArray &json, QString *error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("unexpected answer from MusicBrainz (%1)").arg(pe.errorString());
        return std::nullopt;
    }
    return doc.object();
}

} // namespace

MusicBrainzClient::MusicBrainzClient(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent), m_fetcher(nam, HttpFetcher::Options{1, kDefaultIntervalMs, 2, 20000, 2000}, this)
{
    m_fetcher.setUserAgent(AppInfo::userAgent());
}

bool MusicBrainzClient::isMbid(const QString &s)
{
    static const QRegularExpression re(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(s.trimmed()).hasMatch();
}

QString MusicBrainzClient::searchQuery(const QString &album, const QString &artist, bool relaxed)
{
    QString q;
    if (relaxed) {
        const QString title = words(ArtworkMatch::stripEdition(album));
        q = QStringLiteral("release:(%1)").arg(title.isEmpty() ? words(album) : title);
        const QString who = words(artist);
        if (!who.isEmpty())
            q += QStringLiteral(" AND artist:(%1)").arg(who);
    } else {
        q = QStringLiteral("release:") + quoted(album.trimmed());
        if (!artist.trimmed().isEmpty())
            q += QStringLiteral(" AND artist:") + quoted(artist.trimmed());
    }
    return q;
}

QList<MbRelease> MusicBrainzClient::parseSearch(const QByteArray &json, QString *error)
{
    QList<MbRelease> out;
    const auto root = parseObject(json, error);
    if (!root)
        return out;
    for (const QJsonValue &v : root->value(QLatin1String("releases")).toArray()) {
        MbRelease r = releaseFrom(v.toObject());
        if (!r.id.isEmpty())
            out << std::move(r);
    }
    return out;
}

std::optional<MbRelease> MusicBrainzClient::parseRelease(const QByteArray &json, QString *error)
{
    const auto root = parseObject(json, error);
    if (!root)
        return std::nullopt;
    MbRelease r = releaseFrom(*root);
    if (r.id.isEmpty()) {
        if (error)
            *error = QStringLiteral("unexpected answer from MusicBrainz (no release id)");
        return std::nullopt;
    }
    return r;
}

void MusicBrainzClient::searchReleases(const QString &album, const QString &artist, bool relaxed, QObject *context,
                                       Callback done, int limit)
{
    QUrl url = m_baseUrl;
    url.setPath(url.path() + QStringLiteral("/ws/2/release/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("query"), searchQuery(album, artist, relaxed));
    query.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    url.setQuery(query);
    m_fetcher.get(url, context, [done = std::move(done)](const HttpResult &r) {
        if (!r.ok()) {
            done({}, QStringLiteral("MusicBrainz search failed: %1").arg(r.describe()));
            return;
        }
        QString error;
        QList<MbRelease> releases = parseSearch(r.body, &error);
        done(releases, error);
    });
}

void MusicBrainzClient::lookupRelease(const QString &mbid, QObject *context, Callback done)
{
    if (!isMbid(mbid)) {
        done({}, QStringLiteral("not a MusicBrainz id: %1").arg(mbid));
        return;
    }
    QUrl url = m_baseUrl;
    url.setPath(url.path() + QStringLiteral("/ws/2/release/") + mbid.trimmed().toLower());
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("inc"), QStringLiteral("artist-credits release-groups media"));
    query.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
    url.setQuery(query);
    m_fetcher.get(url, context, [done = std::move(done)](const HttpResult &r) {
        if (r.status == 404 || r.status == 400) {
            done({}, QString()); // unknown (e.g. merged or deleted) release: not an error
            return;
        }
        if (!r.ok()) {
            done({}, QStringLiteral("MusicBrainz lookup failed: %1").arg(r.describe()));
            return;
        }
        QString error;
        const auto release = parseRelease(r.body, &error);
        done(release ? QList<MbRelease>{*release} : QList<MbRelease>{}, error);
    });
}

} // namespace mmm
