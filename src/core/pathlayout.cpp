// SPDX-License-Identifier: GPL-3.0-or-later
#include "pathlayout.h"

#include "deviceprofile.h"
#include "libraryquery.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace mmm::PathLayout {

namespace {

const QStringList kTokens = {QStringLiteral("albumartist"), QStringLiteral("artist"), QStringLiteral("album"),
                             QStringLiteral("title"),       QStringLiteral("year"),   QStringLiteral("track"),
                             QStringLiteral("disc")};

// Inside a token: name, optional ":0N" padding.
const QRegularExpression &tokenRe()
{
    static const QRegularExpression re(QStringLiteral("^([a-z]+)(?::0([1-6]))?$"));
    return re;
}

// The longest prefix of `s` with at most `max` UTF-16 units and `maxBytes` UTF-8 bytes
// (ext4/btrfs names are limited to 255 bytes) that doesn't split a surrogate pair.
QString cut(const QString &s, int max, int maxBytes = 240)
{
    if (max <= 0)
        return {};
    qsizetype n = std::min<qsizetype>(s.size(), max);
    auto fits = [&] { return s.left(n).toUtf8().size() <= maxBytes; };
    while (n > 0 && ((n < s.size() && s.at(n - 1).isHighSurrogate()) || !fits()))
        --n;
    return s.left(n);
}

QString trimEnds(QString s)
{
    // Trailing dots and spaces are dropped by Windows/FAT tools; leading spaces look broken.
    while (!s.isEmpty() && (s.back() == QLatin1Char('.') || s.back().isSpace()))
        s.chop(1);
    while (!s.isEmpty() && s.front().isSpace())
        s.remove(0, 1);
    return s;
}

QString tokenValue(const QString &name, int pad, const TrackRow &t)
{
    auto number = [pad](int n) { return n > 0 ? QStringLiteral("%1").arg(n, pad, 10, QLatin1Char('0')) : QString(); };
    if (name == QLatin1String("albumartist"))
        return !t.albumArtist.trimmed().isEmpty() ? t.albumArtist : !t.artist.trimmed().isEmpty() ? t.artist
                                                                                                 : QStringLiteral("Unknown Artist");
    if (name == QLatin1String("artist"))
        return !t.artist.trimmed().isEmpty() ? t.artist : QStringLiteral("Unknown Artist");
    if (name == QLatin1String("album"))
        return !t.album.trimmed().isEmpty() ? t.album : QStringLiteral("Unknown Album");
    if (name == QLatin1String("title")) {
        if (!t.title.trimmed().isEmpty())
            return t.title;
        const QString base = QFileInfo(t.path).completeBaseName();
        return base.isEmpty() ? QStringLiteral("Untitled") : base;
    }
    if (name == QLatin1String("year"))
        return number(t.year);
    if (name == QLatin1String("track"))
        return number(t.trackNumber);
    if (name == QLatin1String("disc"))
        return number(t.discNumber);
    return {};
}

// Renders the pattern; tag values have their slashes replaced so they stay in one name.
QString expand(const QString &pattern, const TrackRow &track)
{
    QString out;
    QString group;      // text of the current [...] group
    bool inGroup = false;
    bool groupEmpty = false;
    for (qsizetype i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern.at(i);
        QString piece;
        if (c == QLatin1Char('{')) {
            const qsizetype end = pattern.indexOf(QLatin1Char('}'), i);
            const auto m = tokenRe().match(pattern.mid(i + 1, end - i - 1));
            piece = tokenValue(m.captured(1), m.captured(2).isEmpty() ? 1 : m.captured(2).toInt(), track);
            piece.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
            if (piece.isEmpty())
                groupEmpty = true;
            i = end;
        } else if (c == QLatin1Char('[')) {
            inGroup = true;
            groupEmpty = false;
            group.clear();
            continue;
        } else if (c == QLatin1Char(']')) {
            if (!groupEmpty)
                out += group;
            inGroup = false;
            continue;
        } else {
            piece = c;
        }
        (inGroup ? group : out) += piece;
    }
    return out;
}

} // namespace

QString validate(const QString &pattern)
{
    if (pattern.trimmed().isEmpty())
        return QStringLiteral("The layout is empty.");
    bool inGroup = false;
    bool hasTitle = false;
    for (qsizetype i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern.at(i);
        if (c == QLatin1Char('{')) {
            const qsizetype end = pattern.indexOf(QLatin1Char('}'), i);
            if (end < 0)
                return QStringLiteral("A \"{\" has no closing \"}\".");
            const QString inner = pattern.mid(i + 1, end - i - 1);
            const auto m = tokenRe().match(inner);
            if (!m.hasMatch() || !kTokens.contains(m.captured(1)))
                return QStringLiteral("Unknown field {%1}. Use %2.")
                    .arg(inner, QStringLiteral("{albumartist} {artist} {album} {title} {year} {track} {disc}"));
            if (!m.captured(2).isEmpty() && m.captured(1) != QLatin1String("track") && m.captured(1) != QLatin1String("disc")
                && m.captured(1) != QLatin1String("year"))
                return QStringLiteral("Only numbers can be padded: {%1}.").arg(inner);
            if (m.captured(1) == QLatin1String("title"))
                hasTitle = true;
            i = end;
        } else if (c == QLatin1Char('}')) {
            return QStringLiteral("A \"}\" has no opening \"{\".");
        } else if (c == QLatin1Char('[')) {
            if (inGroup)
                return QStringLiteral("Optional parts [...] can't be nested.");
            inGroup = true;
        } else if (c == QLatin1Char(']')) {
            if (!inGroup)
                return QStringLiteral("A \"]\" has no opening \"[\".");
            inGroup = false;
        } else if (c == QLatin1Char('/') && inGroup) {
            return QStringLiteral("An optional part [...] can't contain \"/\".");
        }
    }
    if (inGroup)
        return QStringLiteral("A \"[\" has no closing \"]\".");
    if (pattern.contains(QLatin1String("..")))
        return QStringLiteral("The layout can't contain \"..\".");
    if (pattern.endsWith(QLatin1Char('/')))
        return QStringLiteral("The layout must end with a file name, not \"/\".");
    if (!hasTitle && !pattern.contains(QLatin1String("{track")))
        return QStringLiteral("The file name needs {title} or {track}, or every track gets the same name.");
    return {};
}

QString sanitizeName(const QString &name, int maxLength)
{
    QString s;
    s.reserve(name.size());
    for (const QChar c : name.normalized(QString::NormalizationForm_C)) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7F)
            continue;
        static const QString forbidden = QStringLiteral("<>:\"/\\|?*");
        s += forbidden.contains(c) ? QLatin1Char('_') : c;
    }
    s = s.simplified();
    // Leading dots hide a file on Linux (and many players skip dot files); a leading space
    // between dots (". .foo") mustn't let one through.
    while (!s.isEmpty() && (s.front() == QLatin1Char('.') || s.front().isSpace()))
        s.remove(0, 1);
    s = trimEnds(s);
    // DOS device names, alone or before an extension ("CON", "nul.mp3"): escaped before
    // cutting, so the "_" counts towards the length.
    static const QRegularExpression reserved(QStringLiteral("^(con|prn|aux|nul|com[1-9]|lpt[1-9])(\\..*)?$"),
                                             QRegularExpression::CaseInsensitiveOption);
    const auto m = reserved.match(s);
    if (m.hasMatch())
        s.insert(m.capturedLength(1), QLatin1Char('_'));
    s = trimEnds(cut(s, maxLength));
    return s.isEmpty() ? QStringLiteral("_") : s;
}

QString cleanFolder(const QString &folder)
{
    QStringList parts;
    for (const QString &p : QString(folder).replace(QLatin1Char('\\'), QLatin1Char('/')).split(QLatin1Char('/'))) {
        const QString t = p.trimmed();
        if (t.isEmpty() || t == QLatin1String(".") || t == QLatin1String(".."))
            continue;
        parts << sanitizeName(t);
    }
    return parts.join(QLatin1Char('/'));
}

QString render(const TrackRow &track, const DeviceProfile &profile, int maxLength)
{
    const QString pattern = validate(profile.pathLayout).isEmpty() ? profile.pathLayout : DeviceProfiles::defaultLayout();
    QStringList raw = expand(pattern, track).split(QLatin1Char('/'));
    // The file name is always the last part, even if it came out empty ("..." as a title).
    const QString rawStem = raw.takeLast();
    QString stem = trimEnds(rawStem).isEmpty() || sanitizeName(rawStem) == QLatin1String("_") ? QStringLiteral("Untitled")
                                                                                            : sanitizeName(rawStem);
    QStringList parts;
    for (const QString &p : std::as_const(raw)) {
        if (!trimEnds(p).isEmpty())
            parts << sanitizeName(p); // an empty folder name (e.g. no year) is skipped, not "_"
    }
    const QString ext = QLatin1Char('.') + DeviceProfiles::decide(track.codec, profile).extension;
    const QString prefix = cleanFolder(profile.musicDir);
    if (!prefix.isEmpty())
        parts.prepend(prefix);

    auto join = [&] { return (parts.isEmpty() ? QString() : parts.join(QLatin1Char('/')) + QLatin1Char('/')) + stem + ext; };
    QString path = join();
    // Too long: shorten the file name (down to 32), then the longest folders (down to 16).
    if (path.size() > maxLength) {
        const qsizetype over = path.size() - maxLength;
        stem = sanitizeName(cut(stem, static_cast<int>(std::max<qsizetype>(32, stem.size() - over))));
        path = join();
    }
    while (path.size() > maxLength) {
        qsizetype longest = -1;
        for (qsizetype i = prefix.isEmpty() ? 0 : 1; i < parts.size(); ++i) {
            if (parts[i].size() > 16 && (longest < 0 || parts[i].size() > parts[longest].size()))
                longest = i;
        }
        if (longest < 0)
            break; // can't shorten further without losing the structure
        const qsizetype over = path.size() - maxLength;
        parts[longest] = sanitizeName(cut(parts[longest], static_cast<int>(std::max<qsizetype>(16, parts[longest].size() - over))));
        path = join();
    }
    return path;
}

TrackRow sampleTrack()
{
    TrackRow t;
    t.path = QStringLiteral("/music/AC_DC/Back in Black/2-04 Back in Black.flac");
    t.title = QStringLiteral("Back in Black");
    t.artist = QStringLiteral("AC/DC");
    t.albumArtist = QStringLiteral("AC/DC");
    t.album = QStringLiteral("Back in Black");
    t.trackNumber = 4;
    t.discNumber = 2;
    t.year = 1980;
    t.codec = QStringLiteral("FLAC");
    return t;
}

} // namespace mmm::PathLayout
