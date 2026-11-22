// SPDX-License-Identifier: GPL-3.0-or-later
#include "tageditor.h"

#include "library.h"
#include "tagreader.h"

#include <QtConcurrent>

namespace {

enum class Kind { Text, Number, Flag };
struct Field
{
    const char *name;
    Kind kind;
    int max = 0; // numbers: largest allowed value
};
constexpr Field kFields[] = {
    {"title", Kind::Text},       {"artist", Kind::Text},         {"album", Kind::Text},
    {"albumArtist", Kind::Text}, {"genre", Kind::Text},          {"composer", Kind::Text},
    {"comment", Kind::Text},     {"year", Kind::Number, 9999},   {"trackNumber", Kind::Number, 9999},
    {"trackTotal", Kind::Number, 9999}, {"discNumber", Kind::Number, 999}, {"discTotal", Kind::Number, 999},
    {"compilation", Kind::Flag},
};

const QHash<QString, QString> &labels()
{
    static const QHash<QString, QString> l{
        {QStringLiteral("year"), QStringLiteral("Year")},
        {QStringLiteral("trackNumber"), QStringLiteral("Track number")},
        {QStringLiteral("trackTotal"), QStringLiteral("Track total")},
        {QStringLiteral("discNumber"), QStringLiteral("Disc number")},
        {QStringLiteral("discTotal"), QStringLiteral("Disc total")},
    };
    return l;
}

QString numberText(int n)
{
    return n > 0 ? QString::number(n) : QString();
}

using ReadResults = QList<std::optional<mmm::TrackTags>>;

} // namespace

TagEditor::TagEditor(QObject *parent)
    : QObject(parent)
{
}

QVariantMap TagEditor::toValues(const mmm::TrackTags &t)
{
    return {
        {QStringLiteral("title"), t.title},
        {QStringLiteral("artist"), t.artist},
        {QStringLiteral("album"), t.album},
        {QStringLiteral("albumArtist"), t.albumArtist},
        {QStringLiteral("genre"), t.genre},
        {QStringLiteral("composer"), t.composer},
        {QStringLiteral("comment"), t.comment},
        {QStringLiteral("year"), numberText(t.year)},
        {QStringLiteral("trackNumber"), numberText(t.trackNumber)},
        {QStringLiteral("trackTotal"), numberText(t.trackTotal)},
        {QStringLiteral("discNumber"), numberText(t.discNumber)},
        {QStringLiteral("discTotal"), numberText(t.discTotal)},
        {QStringLiteral("compilation"), t.compilation},
    };
}

void TagEditor::setPaths(const QStringList &paths)
{
    // The same paths again still re-read the files: their tags may have changed.
    const bool changed = paths != m_paths;
    m_paths = paths;
    m_values.clear();
    m_mixed.clear();
    setError({});
    if (changed)
        Q_EMIT pathsChanged();
    Q_EMIT valuesChanged();

    const quint64 generation = ++m_generation;
    if (paths.isEmpty()) {
        setLoading(false);
        return;
    }
    setLoading(true);
    QtConcurrent::run([paths] {
        ReadResults out;
        out.reserve(paths.size());
        for (const QString &p : paths)
            out << mmm::TagReader::read(p);
        return out;
    }).then(this, [this, generation](const ReadResults &results) {
        if (generation != m_generation)
            return; // paths changed meanwhile
        QVariantMap common;
        QStringList mixed;
        int unreadable = 0;
        bool first = true;
        for (const auto &tags : results) {
            if (!tags) {
                ++unreadable;
                continue;
            }
            const QVariantMap v = toValues(*tags);
            if (first) {
                common = v;
                first = false;
                continue;
            }
            for (auto it = v.cbegin(); it != v.cend(); ++it) {
                if (!mixed.contains(it.key()) && common.value(it.key()) != it.value())
                    mixed << it.key();
            }
        }
        for (const QString &f : std::as_const(mixed))
            common[f] = f == QLatin1String("compilation") ? QVariant(false) : QVariant(QString());
        m_values = common;
        m_mixed = mixed;
        Q_EMIT valuesChanged();
        if (unreadable > 0)
            setError(unreadable == results.size()
                         ? QStringLiteral("The files can't be read.")
                         : QStringLiteral("%1 of %2 files can't be read.").arg(unreadable).arg(results.size()));
        setLoading(false);
    });
}

std::optional<mmm::TagChanges> TagEditor::toChanges(const QVariantMap &changes, QString *error)
{
    mmm::TagChanges c;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        const Field *field = nullptr;
        for (const Field &f : kFields) {
            if (it.key() == QLatin1StringView(f.name))
                field = &f;
        }
        if (!field) {
            *error = QStringLiteral("Unknown field %1").arg(it.key());
            return std::nullopt;
        }
        std::optional<int> number;
        if (field->kind == Kind::Number) {
            const QString text = it.value().toString().trimmed();
            bool ok = true;
            const int n = text.isEmpty() ? 0 : text.toInt(&ok);
            if (!ok || n < 0 || n > field->max) {
                *error = QStringLiteral("%1 must be a number from 0 to %2.")
                             .arg(labels().value(it.key()), QString::number(field->max));
                return std::nullopt;
            }
            number = n;
        }
        const QString text = it.value().toString();
        const QString &k = it.key();
        if (k == QLatin1String("title")) c.title = text;
        else if (k == QLatin1String("artist")) c.artist = text;
        else if (k == QLatin1String("album")) c.album = text;
        else if (k == QLatin1String("albumArtist")) c.albumArtist = text;
        else if (k == QLatin1String("genre")) c.genre = text;
        else if (k == QLatin1String("composer")) c.composer = text;
        else if (k == QLatin1String("comment")) c.comment = text;
        else if (k == QLatin1String("year")) c.year = number;
        else if (k == QLatin1String("trackNumber")) c.trackNumber = number;
        else if (k == QLatin1String("trackTotal")) c.trackTotal = number;
        else if (k == QLatin1String("discNumber")) c.discNumber = number;
        else if (k == QLatin1String("discTotal")) c.discTotal = number;
        else if (k == QLatin1String("compilation")) c.compilation = it.value().toBool();
    }
    return c;
}

bool TagEditor::apply(const QVariantMap &changes)
{
    if (m_saving) {
        setError(QStringLiteral("Still saving."));
        return false;
    }
    QString error;
    const auto c = toChanges(changes, &error);
    if (!c) {
        setError(error);
        return false;
    }
    setError({});
    if (c->isEmpty() || m_paths.isEmpty()) {
        Q_EMIT finished(true, QStringLiteral("Nothing changed"), 0);
        return true;
    }
    Library *lib = Library::instance();
    if (!lib) {
        setError(QStringLiteral("No library."));
        return false;
    }
    m_saving = true;
    Q_EMIT savingChanged();
    lib->editTags(m_paths, *c).then(this, [this](const mmm::TagEditResult &r) {
        m_saving = false;
        Q_EMIT savingChanged();
        qint64 albumId = r.tracks.isEmpty() ? 0 : r.tracks.front().albumId;
        for (const mmm::TrackRow &t : r.tracks) {
            if (t.albumId != albumId)
                albumId = 0;
        }
        const bool ok = r.failures.isEmpty();
        const QString message = ok ? QStringLiteral("Saved")
                                   : QStringLiteral("%1 of %2 tracks failed: %3")
                                         .arg(r.failures.size()).arg(m_paths.size()).arg(r.failures.front());
        if (!ok)
            setError(message);
        Q_EMIT finished(ok, message, static_cast<int>(albumId));
    });
    return true;
}

void TagEditor::setError(const QString &message)
{
    if (message == m_error)
        return;
    m_error = message;
    Q_EMIT errorStringChanged();
}

void TagEditor::setLoading(bool loading)
{
    if (loading == m_loading)
        return;
    m_loading = loading;
    Q_EMIT loadingChanged();
}
