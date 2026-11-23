// SPDX-License-Identifier: GPL-3.0-or-later
#include "mdtitle.h"

#include <QHash>
#include <QRegularExpression>

namespace mmm {

QString describe(NetMdError e)
{
    switch (e) {
    case NetMdError::None:
        return {};
    case NetMdError::NotFound:
        return QStringLiteral("The recorder isn't connected.");
    case NetMdError::PermissionDenied:
        return QStringLiteral("No permission to use the recorder: install the udev rule (docs/netmd-setup.md), then plug it in again.");
    case NetMdError::Busy:
        return QStringLiteral("The recorder is in use by another program (Web MiniDisc Pro?). Close it, then retry.");
    case NetMdError::NoDisc:
        return QStringLiteral("No disc in the recorder.");
    case NetMdError::Rejected:
        return QStringLiteral("The recorder refused the change (write-protected disc, or no room left for titles).");
    case NetMdError::Disconnected:
        return QStringLiteral("The recorder was disconnected.");
    case NetMdError::Failed:
        return QStringLiteral("The recorder didn't answer as expected.");
    }
    return {};
}

namespace MdTitle {

namespace {

// Spelled out before decomposition (NFKD doesn't take these apart).
const QHash<char16_t, QString> &replacements()
{
    static const QHash<char16_t, QString> map{
        {u'‘', QStringLiteral("'")},  {u'’', QStringLiteral("'")},  {u'‚', QStringLiteral("'")},
        {u'‛', QStringLiteral("'")},  {u'′', QStringLiteral("'")},  {u'´', QStringLiteral("'")},
        {u'`', QStringLiteral("'")},  {u'“', QStringLiteral("\"")}, {u'”', QStringLiteral("\"")},
        {u'„', QStringLiteral("\"")}, {u'‟', QStringLiteral("\"")}, {u'″', QStringLiteral("\"")},
        {u'«', QStringLiteral("\"")}, {u'»', QStringLiteral("\"")}, {u'–', QStringLiteral("-")},
        {u'—', QStringLiteral("-")},  {u'−', QStringLiteral("-")},  {u'‐', QStringLiteral("-")},
        {u'‑', QStringLiteral("-")},  {u'…', QStringLiteral("...")}, {u'ß', QStringLiteral("ss")},
        {u'Æ', QStringLiteral("AE")}, {u'æ', QStringLiteral("ae")}, {u'Œ', QStringLiteral("OE")},
        {u'œ', QStringLiteral("oe")}, {u'Ø', QStringLiteral("O")},  {u'ø', QStringLiteral("o")},
        {u'Ł', QStringLiteral("L")},  {u'ł', QStringLiteral("l")},  {u'Đ', QStringLiteral("D")},
        {u'đ', QStringLiteral("d")},  {u'Ð', QStringLiteral("D")},  {u'ð', QStringLiteral("d")},
        {u'Þ', QStringLiteral("Th")}, {u'þ', QStringLiteral("th")}, {u'×', QStringLiteral("x")},
        {u'÷', QStringLiteral("/")},  {u'·', QStringLiteral("-")},  {u'•', QStringLiteral("-")},
        {u'~', QStringLiteral("-")},  {u'\\', QStringLiteral("/")}, {u'¡', QStringLiteral("!")},
        {u'¿', QStringLiteral("?")},
    };
    return map;
}

// The header is ASCII after clean(): bytes = characters.
int bytes(const QString &s)
{
    return static_cast<int>(s.toLatin1().size());
}

} // namespace

Cleaned clean(const QString &title, int maxBytes)
{
    Cleaned out;
    QString spelled;
    spelled.reserve(title.size());
    for (const QChar c : title) {
        const auto r = replacements().constFind(c.unicode());
        spelled += r != replacements().cend() ? *r : QString(c);
    }
    QString ascii;
    for (const QChar c : spelled.normalized(QString::NormalizationForm_KD)) {
        const char16_t u = c.unicode();
        if (u >= 0x20 && u < 0x7F)
            ascii += c;
        else if (c.isSpace())
            ascii += QLatin1Char(' ');
        // combining marks (the accents NFKD split off) and everything else: dropped
    }
    ascii = ascii.simplified();
    out.changed = ascii != title;
    if (bytes(ascii) > maxBytes) {
        ascii = ascii.left(maxBytes).trimmed();
        out.truncated = true;
    }
    out.text = ascii;
    return out;
}

Header parseHeader(const QString &raw)
{
    Header h;
    if (!raw.contains(QLatin1String("//"))) {
        // A plain title, as most recorders write it (no groups).
        h.title = raw.trimmed();
        return h;
    }
    const QStringList tokens = raw.split(QStringLiteral("//"));
    bool titleFromZero = false; // a "0;Title" token wins over a stray piece without ';'
    for (const QString &token : tokens) {
        if (token.isEmpty())
            continue;
        const qsizetype semi = token.indexOf(QLatin1Char(';'));
        if (semi < 0) {
            if (!titleFromZero && h.title.isEmpty())
                h.title = token; // "Title//" form
            continue;
        }
        const QString range = token.left(semi).trimmed();
        const QString name = token.mid(semi + 1);
        if (range.isEmpty()) {
            if (!name.isEmpty())
                h.groups << MdGroup{name, -1, -1}; // a group without tracks
            continue;
        }
        const qsizetype dash = range.indexOf(QLatin1Char('-'));
        bool ok = false;
        const int start = (dash < 0 ? range : range.left(dash)).toInt(&ok);
        if (!ok)
            continue; // not a range we understand: skip it
        if (start == 0) {
            h.title = name; // "0;Title" or "0-n;Title"
            titleFromZero = true;
            continue;
        }
        int finish = start;
        if (dash >= 0) {
            bool okFinish = false;
            finish = range.mid(dash + 1).toInt(&okFinish);
            if (!okFinish || finish < start)
                finish = start;
        }
        h.groups << MdGroup{name, start - 1, finish - 1};
    }
    return h;
}

QString formatHeader(const Header &header)
{
    if (header.groups.isEmpty() && !header.title.contains(QLatin1Char(';')) && !header.title.contains(QLatin1String("//")))
        return header.title;
    QString out = QStringLiteral("0;") + header.title + QStringLiteral("//");
    for (const MdGroup &g : header.groups) {
        if (g.first < 0)
            out += QLatin1Char(';');
        else if (g.last > g.first)
            out += QStringLiteral("%1-%2;").arg(g.first + 1).arg(g.last + 1);
        else
            out += QStringLiteral("%1;").arg(g.first + 1);
        out += g.title + QStringLiteral("//");
    }
    return out;
}

QString withTitle(const QString &raw, const QString &cleanTitle, bool *truncated)
{
    Header h = parseHeader(raw);
    // Slashes would split the header: no "//" in the title (runs collapse) and none at its
    // end ("Title///" reads as "Title" + a broken group).
    auto tidy = [](QString t) {
        while (t.contains(QLatin1String("//")))
            t.replace(QStringLiteral("//"), QStringLiteral("/"));
        while (t.endsWith(QLatin1Char('/')) || t.endsWith(QLatin1Char(' ')))
            t.chop(1);
        return t;
    };
    h.title = tidy(cleanTitle);
    QString out = formatHeader(h);
    bool cut = false;
    while (bytes(out) > kMaxHeaderBytes && !h.title.isEmpty()) {
        h.title.chop(std::max(1, bytes(out) - kMaxHeaderBytes));
        h.title = tidy(h.title);
        out = formatHeader(h);
        cut = true;
    }
    if (truncated)
        *truncated = cut;
    return out;
}

} // namespace MdTitle
} // namespace mmm
