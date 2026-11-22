// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

#include "httpfetcher.h"

class QNetworkAccessManager;

namespace mmm {

// A release (one edition of an album) as MusicBrainz describes it.
struct MbRelease
{
    QString id;             // release MBID
    QString releaseGroupId; // the album across editions
    QString title;
    QString artist;         // artist credit as displayed ("A feat. B")
    int year = 0;
    QString country;        // "GB", "XE", ...
    QString status;         // "Official", "Bootleg", "Promotion", "Pseudo-Release"
    QString format;         // "CD", "2×CD", "CD + DVD", "Digital Media"
    int trackCount = 0;     // over all media
    int searchScore = 0;    // MusicBrainz's own 0–100 search score (0 for lookups)
};

// MusicBrainz web service (ws/2) release search and lookup. Requests are serialised and
// spaced >= 1.1 s apart (MusicBrainz allows ~1 per second) with an identifying User-Agent.
// Callbacks run on this object's thread; a lookup of an unknown MBID gives an empty list
// and no error.
class MusicBrainzClient : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(const QList<MbRelease> &releases, const QString &error)>;

    static constexpr int kDefaultIntervalMs = 1100;
    static QUrl defaultBaseUrl() { return QUrl(QStringLiteral("https://musicbrainz.org")); }

    explicit MusicBrainzClient(QNetworkAccessManager *nam, QObject *parent = nullptr);

    void setBaseUrl(const QUrl &url) { m_baseUrl = url; }
    QUrl baseUrl() const { return m_baseUrl; }
    HttpFetcher &fetcher() { return m_fetcher; }

    // Releases matching an album title and artist, best first (up to `limit`). `relaxed`:
    // edition suffixes dropped and words matched loosely (for a second try).
    void searchReleases(const QString &album, const QString &artist, bool relaxed, QObject *context,
                        Callback done, int limit = 10);
    // One release by MBID.
    void lookupRelease(const QString &mbid, QObject *context, Callback done);
    void cancelAll() { m_fetcher.cancelAll(); }

    // --- Pure helpers (exposed for tests) ---
    static QString searchQuery(const QString &album, const QString &artist, bool relaxed);
    static QList<MbRelease> parseSearch(const QByteArray &json, QString *error = nullptr);
    static std::optional<MbRelease> parseRelease(const QByteArray &json, QString *error = nullptr);
    static bool isMbid(const QString &s);

private:
    QUrl m_baseUrl = defaultBaseUrl();
    HttpFetcher m_fetcher;
};

} // namespace mmm
