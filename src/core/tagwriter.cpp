// SPDX-License-Identifier: GPL-3.0-or-later
#include "tagwriter.h"

#include "database.h"
#include "tagreader.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>

#include <taglib/aifffile.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/id3v2header.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4file.h>
#include <taglib/mpegfile.h>
#include <taglib/oggfile.h>
#include <taglib/tpropertymap.h>
#include <taglib/wavfile.h>

#include <filesystem>
#include <system_error>

namespace mmm {

namespace {

TagLib::String toTag(const QString &s)
{
    return TagLib::String(s.toStdString(), TagLib::String::UTF8);
}

QString firstValue(const TagLib::PropertyMap &map, std::initializer_list<const char *> keys)
{
    for (const char *k : keys) {
        const auto it = map.find(k);
        if (it != map.end() && !it->second.isEmpty())
            return QString::fromStdString(it->second.front().to8Bit(true)).trimmed();
    }
    return {};
}

class Editor
{
public:
    Editor(TagLib::PropertyMap map, bool separateTotals)
        : m_map(std::move(map)), m_separateTotals(separateTotals)
    {
    }

    void text(const char *key, const std::optional<QString> &value)
    {
        if (!value)
            return;
        touch(key);
        const QString v = value->trimmed();
        if (v.isEmpty())
            m_map.erase(key);
        else
            m_map.replace(key, TagLib::StringList(toTag(v)));
    }

    void year(const std::optional<int> &year)
    {
        if (!year)
            return;
        touch("DATE");
        if (*year <= 0) {
            m_map.erase("DATE");
            m_map.erase("YEAR");
            return;
        }
        // Keep a full date ("2019-05-03") whose year is already right.
        if (TagReader::parseYear(firstValue(m_map, {"DATE"})) == *year)
            return;
        m_map.replace("DATE", TagLib::StringList(toTag(QString::number(*year))));
        m_map.erase("YEAR");
    }

    // "TRACKNUMBER" + "TRACKTOTAL"/"TOTALTRACKS", or the disc equivalents.
    void numberPair(const char *numberKey, const char *totalKey, const char *altTotalKey,
                    const std::optional<int> &number, const std::optional<int> &total)
    {
        if (!number && !total)
            return;
        touch(numberKey);
        auto [n, t] = TagReader::parseNumberPair(firstValue(m_map, {numberKey}));
        if (t == 0)
            t = TagReader::parseNumberPair(firstValue(m_map, {totalKey, altTotalKey})).first;
        n = std::max(0, number.value_or(n));
        t = std::max(0, total.value_or(t));

        m_map.erase(altTotalKey);
        if (m_separateTotals) {
            // Vorbis comments: TRACKNUMBER=3, TRACKTOTAL=12 (what most taggers write).
            touch(totalKey);
            setOrErase(numberKey, n);
            setOrErase(totalKey, t);
            return;
        }
        // ID3v2 TRCK / MP4 trkn hold both: "3/12". A total without a number can't be stored.
        m_map.erase(totalKey);
        if (n == 0)
            m_map.erase(numberKey);
        else
            m_map.replace(numberKey, TagLib::StringList(toTag(t > 0 ? QStringLiteral("%1/%2").arg(n).arg(t)
                                                                  : QString::number(n))));
    }

    void flag(const char *key, const std::optional<bool> &value)
    {
        if (!value)
            return;
        touch(key);
        if (*value)
            m_map.replace(key, TagLib::StringList(TagLib::String("1")));
        else
            m_map.erase(key);
    }

    const TagLib::PropertyMap &map() const { return m_map; }
    const QList<const char *> &touched() const { return m_touched; }

private:
    void touch(const char *key) { m_touched << key; }
    void setOrErase(const char *key, int v)
    {
        if (v > 0)
            m_map.replace(key, TagLib::StringList(toTag(QString::number(v))));
        else
            m_map.erase(key);
    }

    TagLib::PropertyMap m_map;
    bool m_separateTotals;
    QList<const char *> m_touched;
};

TagLib::ID3v2::Version id3VersionOf(const TagLib::ID3v2::Tag *tag)
{
    // Keep ID3v2.3 files at 2.3: some players and car stereos can't read 2.4.
    return tag && tag->header()->majorVersion() == 3 ? TagLib::ID3v2::v3 : TagLib::ID3v2::v4;
}

bool save(TagLib::File *file)
{
    using namespace TagLib;
    if (auto *f = dynamic_cast<MPEG::File *>(file)) {
        // Update the tags on disk (ID3v2, plus ID3v1/APE if present); don't add new types.
        // (TagLib keeps an ID3v1 tag in memory for every MP3, so check what's on disk.)
        const int tags = MPEG::File::ID3v2 | (f->hasID3v1Tag() ? MPEG::File::ID3v1 : 0)
            | (f->hasAPETag() ? MPEG::File::APE : 0);
        return f->save(tags, File::StripNone, id3VersionOf(f->hasID3v2Tag() ? f->ID3v2Tag() : nullptr),
                       File::DoNotDuplicate);
    }
    if (auto *f = dynamic_cast<RIFF::WAV::File *>(file))
        return f->save(RIFF::WAV::File::AllTags, File::StripOthers,
                       id3VersionOf(f->hasID3v2Tag() ? f->ID3v2Tag() : nullptr));
    if (auto *f = dynamic_cast<RIFF::AIFF::File *>(file))
        return f->save(id3VersionOf(f->hasID3v2Tag() ? f->tag() : nullptr));
    return file->save();
}

// Front cover first, then the pictures worth keeping.
bool replaceFrontCover(TagLib::File *file, const EmbeddedPicture &cover)
{
    using namespace TagLib;
    const QByteArray &data = cover.data;
    VariantMap front;
    front.insert("data", ByteVector(data.constData(), static_cast<unsigned int>(data.size())));
    front.insert("mimeType", toTag(cover.mimeType));
    front.insert("pictureType", String("Front Cover"));
    front.insert("description", toTag(cover.description));
    List<VariantMap> pictures;
    pictures.append(front);
    if (!dynamic_cast<MP4::File *>(file)) {
        for (const VariantMap &p : file->complexProperties("PICTURE")) {
            if (p.value("pictureType").toString() != String("Front Cover"))
                pictures.append(p);
        }
    }
    // MP3/WAV/AIFF: write the ID3v2 tag itself (the file's tag is a union with ID3v1/INFO).
    if (auto *f = dynamic_cast<MPEG::File *>(file))
        return f->ID3v2Tag(true)->setComplexProperties("PICTURE", pictures);
    if (auto *f = dynamic_cast<RIFF::WAV::File *>(file))
        return f->ID3v2Tag()->setComplexProperties("PICTURE", pictures);
    if (auto *f = dynamic_cast<RIFF::AIFF::File *>(file))
        return f->tag()->setComplexProperties("PICTURE", pictures);
    return file->setComplexProperties("PICTURE", pictures);
}

bool applyChanges(const QString &path, const TagChanges &c, QString *error)
{
    const QByteArray name = QFile::encodeName(path);
    TagLib::FileRef ref(name.constData(), true, TagLib::AudioProperties::Fast);
    if (ref.isNull() || !ref.file()->isValid()) {
        *error = QStringLiteral("not a supported audio file");
        return false;
    }
    // Like TagReader: TagLib parses junk named .mp3; require an audio stream.
    if (!ref.audioProperties() || ref.audioProperties()->sampleRate() <= 0) {
        *error = QStringLiteral("no audio stream found");
        return false;
    }
    TagLib::File *file = ref.file();
    if (file->readOnly()) {
        *error = QStringLiteral("cannot open the file for writing");
        return false;
    }

    const bool vorbisComments =
        dynamic_cast<TagLib::FLAC::File *>(file) || dynamic_cast<TagLib::Ogg::File *>(file);
    Editor e(file->properties(), vorbisComments);
    e.text("TITLE", c.title);
    e.text("ARTIST", c.artist);
    e.text("ALBUM", c.album);
    e.text("ALBUMARTIST", c.albumArtist);
    e.text("GENRE", c.genre);
    e.text("COMPOSER", c.composer);
    e.text("COMMENT", c.comment);
    e.year(c.year);
    e.numberPair("TRACKNUMBER", "TRACKTOTAL", "TOTALTRACKS", c.trackNumber, c.trackTotal);
    e.numberPair("DISCNUMBER", "DISCTOTAL", "TOTALDISCS", c.discNumber, c.discTotal);
    e.flag("COMPILATION", c.compilation);

    // A cover-only change leaves the text tags exactly as they are.
    const TagLib::PropertyMap rejected = e.touched().isEmpty() ? TagLib::PropertyMap() : file->setProperties(e.map());
    for (const char *key : e.touched()) {
        if (rejected.contains(key)) {
            *error = QStringLiteral("this file type can't store %1").arg(QLatin1StringView(key));
            return false;
        }
    }
    if (c.frontCover && !replaceFrontCover(file, *c.frontCover)) {
        *error = QStringLiteral("this file type can't store cover art");
        return false;
    }
    if (!save(file)) {
        *error = QStringLiteral("TagLib could not save the tags");
        return false;
    }
    return true;
}

} // namespace

bool TagWriter::write(const QString &path, const TagChanges &changes, QString *error)
{
    QString err;
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (changes.isEmpty())
        return true;
    const QFileInfo info(path);
    if (!info.isFile())
        return fail(QStringLiteral("file not found"));
    if (!info.isWritable())
        return fail(QStringLiteral("the file is read-only"));

    // Hidden, so a library scan never picks it up; same suffix, so TagLib detects the type.
    QString tmpPath;
    {
        QTemporaryFile reserve(info.dir().filePath(QStringLiteral(".mmm-edit-XXXXXX.") + info.suffix()));
        reserve.setAutoRemove(false);
        if (!reserve.open())
            return fail(QStringLiteral("cannot create a file in %1: %2").arg(info.path(), reserve.errorString()));
        tmpPath = reserve.fileName();
    }
    // QFile::copy refuses to overwrite; it clones (reflinks) where the filesystem can.
    QFile::remove(tmpPath);
    QFile source(path);
    if (!source.copy(tmpPath))
        return fail(QStringLiteral("cannot copy the file: %1").arg(source.errorString()));

    if (!applyChanges(tmpPath, changes, &err)) {
        QFile::remove(tmpPath);
        return fail(err);
    }
    // rename() replaces the original atomically (QFile::rename won't overwrite).
    std::error_code ec;
    std::filesystem::rename(QFile::encodeName(tmpPath).toStdString(), QFile::encodeName(path).toStdString(), ec);
    if (ec) {
        QFile::remove(tmpPath);
        return fail(QStringLiteral("cannot replace the file: %1").arg(QString::fromStdString(ec.message())));
    }
    return true;
}

TagEditResult editTracks(Database &db, const QStringList &paths, const TagChanges &changes)
{
    TagEditResult result;
    for (const QString &path : paths) {
        QString error;
        if (!TagWriter::write(path, changes, &error)) {
            result.failures << path + QStringLiteral(": ") + error;
            continue;
        }
        result.written << path;
        const auto tags = TagReader::read(path, &error);
        const QFileInfo info(path);
        if (!tags) {
            result.failures << path + QStringLiteral(": tags written, but the file can't be read back: ") + error;
            continue;
        }
        const TrackFile file{path, info.lastModified().toMSecsSinceEpoch(), info.size(), 0};
        if (!db.updateEditedTrack(file, *tags, &error))
            result.failures << path + QStringLiteral(": tags written, but the library wasn't updated: ") + error;
    }
    if (!result.written.isEmpty()) {
        db.pruneOrphans();
        result.tracks = LibraryQuery(db).tracksByPaths(result.written);
    }
    return result;
}

} // namespace mmm
