// SPDX-License-Identifier: GPL-3.0-or-later
#include "artworkmatch.h"

#include "libraryquery.h"
#include "musicbrainzclient.h"

#include <QHash>
#include <QRegularExpression>

#include <cmath>

namespace mmm::ArtworkMatch {

QString normalize(const QString &s)
{
    const QString decomposed = s.normalized(QString::NormalizationForm_KD).toLower();
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() == QChar::Mark_NonSpacing)
            continue; // accents
        if (c == QLatin1Char('&'))
            out += QStringLiteral(" and ");
        else if (c.isLetterOrNumber())
            out += c;
        else if (c != QLatin1Char('\'') && c != QChar(0x2019)) // "don't" stays one word
            out += QLatin1Char(' ');
    }
    out = out.simplified();
    if (out.startsWith(QLatin1String("the ")))
        out = out.mid(4);
    return out;
}

QString stripEdition(const QString &title)
{
    static const QString notes = QStringLiteral(
        "edition|remaster(ed)?|deluxe|expanded|anniversary|bonus|reissue|special|limited|collector|"
        "disc|cd|version|mono|stereo|explicit|clean|digipak|japan");
    // A trailing (...) or [...] holding an edition word, or " - ... Edition".
    static const QRegularExpression bracket(
        QStringLiteral("\\s*[\\(\\[][^\\(\\)\\[\\]]*\\b(%1)\\b[^\\(\\)\\[\\]]*[\\)\\]]\\s*$").arg(notes),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression dash(QStringLiteral("\\s+[-–:]\\s+[^-–:]*\\b(%1)\\b[^-–:]*$").arg(notes),
                                         QRegularExpression::CaseInsensitiveOption);
    QString t = title.trimmed();
    for (int i = 0; i < 3; ++i) { // "Album (Disc 1) [Remastered]"
        QString next = t;
        next.remove(bracket);
        next.remove(dash);
        next = next.trimmed();
        if (next == t || next.isEmpty())
            break;
        t = next;
    }
    return t;
}

double similarity(const QString &a, const QString &b)
{
    const QString x = normalize(a);
    const QString y = normalize(b);
    if (x == y)
        return 1.0;
    if (x.size() < 2 || y.size() < 2)
        return 0.0;
    QHash<QString, int> pairs;
    for (qsizetype i = 0; i + 1 < x.size(); ++i)
        ++pairs[x.mid(i, 2)];
    int common = 0;
    for (qsizetype i = 0; i + 1 < y.size(); ++i) {
        auto it = pairs.find(y.mid(i, 2));
        if (it != pairs.end() && *it > 0) {
            --*it;
            ++common;
        }
    }
    return 2.0 * common / double((x.size() - 1) + (y.size() - 1));
}

int score(const ArtworkTarget &album, const MbRelease &release)
{
    // Title: compare with and without edition notes on either side.
    const double title = std::max({similarity(album.title, release.title),
                                   similarity(stripEdition(album.title), stripEdition(release.title))});
    if (title < 0.5)
        return -1;
    int s = static_cast<int>(std::lround(40.0 * title));

    const bool variousArtists = album.compilation || normalize(album.artist) == QLatin1String("various artists");
    if (variousArtists && normalize(release.artist) == QLatin1String("various artists"))
        s += 25;
    else if (!album.artist.isEmpty())
        s += static_cast<int>(std::lround(25.0 * similarity(album.artist, release.artist)));

    if (album.trackCount > 0 && release.trackCount > 0) {
        const int diff = std::abs(album.trackCount - release.trackCount);
        s += diff == 0 ? 15 : diff <= 2 ? 7 : 0;
    }
    if (album.year > 0 && release.year > 0) {
        const int diff = std::abs(album.year - release.year);
        s += diff == 0 ? 10 : diff == 1 ? 5 : diff <= 3 ? 2 : 0;
    }
    if (release.status == QLatin1String("Official"))
        s += 5;
    else if (release.status == QLatin1String("Bootleg") || release.status == QLatin1String("Pseudo-Release"))
        s -= 10;
    return std::max(s, 0);
}

} // namespace mmm::ArtworkMatch
