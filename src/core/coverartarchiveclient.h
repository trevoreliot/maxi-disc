// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

#include "httpfetcher.h"

class QNetworkAccessManager;

namespace mmm {

// A front cover listed by the Cover Art Archive.
struct CaaImage
{
    QString id;      // CAA image id (the same image listed for a release and its group shares it)
    QUrl image;      // original upload (any size)
    QUrl thumb250, thumb500, thumb1200;

    // ~500 px, for showing candidates.
    QUrl previewUrl() const;
    // ≤ 1200 px JPEG for saving (falls back to 500 px, then the original).
    QUrl saveUrl() const;
};

// Cover Art Archive (coverartarchive.org): front covers of releases and release groups.
// No rate limit is required; at most 4 requests run at once.
class CoverArtArchiveClient : public QObject
{
    Q_OBJECT

public:
    enum class Kind { Release, ReleaseGroup };
    // `front` is nullopt with no error when there's no front cover (CAA answers 404).
    using FrontCallback = std::function<void(const std::optional<CaaImage> &front, const QString &error)>;

    static QUrl defaultBaseUrl() { return QUrl(QStringLiteral("https://coverartarchive.org")); }

    explicit CoverArtArchiveClient(QNetworkAccessManager *nam, QObject *parent = nullptr);

    void setBaseUrl(const QUrl &url) { m_baseUrl = url; }
    HttpFetcher &fetcher() { return m_fetcher; }

    void frontCover(Kind kind, const QString &mbid, QObject *context, FrontCallback done);
    void cancelAll() { m_fetcher.cancelAll(); }

    // The front image of a CAA listing (JSON); nullopt if it has none. Exposed for tests.
    static std::optional<CaaImage> parseFront(const QByteArray &json, QString *error = nullptr);

private:
    QUrl m_baseUrl = defaultBaseUrl();
    HttpFetcher m_fetcher;
};

} // namespace mmm
