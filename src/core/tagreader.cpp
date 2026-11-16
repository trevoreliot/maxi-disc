// SPDX-License-Identifier: GPL-3.0-or-later
#include "tagreader.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <taglib/aifffile.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/mp4file.h>
#include <taglib/mpegfile.h>
#include <taglib/oggflacfile.h>
#include <taglib/opusfile.h>
#include <taglib/tpropertymap.h>
#include <taglib/vorbisfile.h>
#include <taglib/wavfile.h>

namespace mmm {

namespace {

QString toQString(const TagLib::String &s)
{
    return QString::fromStdString(s.to8Bit(true));
}

QByteArray encodedPath(const QString &path)
{
    return QFile::encodeName(path);
}

class Props
{
public:
    explicit Props(TagLib::PropertyMap map) : m_map(std::move(map)) {}

    // First value of the first key present.
    QString first(std::initializer_list<const char *> keys) const
    {
        for (const char *k : keys) {
            const auto it = m_map.find(k);
            if (it != m_map.end() && !it->second.isEmpty())
                return toQString(it->second.front()).trimmed();
        }
        return {};
    }

    QString joined(const char *key, const QString &sep) const
    {
        const auto it = m_map.find(key);
        if (it == m_map.end())
            return {};
        QStringList out;
        for (const auto &v : it->second) {
            const QString s = toQString(v).trimmed();
            if (!s.isEmpty() && !out.contains(s))
                out << s;
        }
        return out.join(sep);
    }

private:
    TagLib::PropertyMap m_map;
};

struct CodecInfo
{
    QString codec = QStringLiteral("Unknown");
    int bitsPerSample = 0;
};

CodecInfo codecOf(TagLib::File *file)
{
    using namespace TagLib;
    if (auto *f = dynamic_cast<FLAC::File *>(file))
        return {QStringLiteral("FLAC"), f->audioProperties() ? f->audioProperties()->bitsPerSample() : 0};
    if (auto *f = dynamic_cast<Ogg::FLAC::File *>(file))
        return {QStringLiteral("FLAC"), f->audioProperties() ? f->audioProperties()->bitsPerSample() : 0};
    if (auto *f = dynamic_cast<MPEG::File *>(file)) {
        const auto *p = f->audioProperties();
        return {p && p->layer() != 3 ? QStringLiteral("MP%1").arg(p->layer()) : QStringLiteral("MP3"), 0};
    }
    if (auto *f = dynamic_cast<RIFF::WAV::File *>(file))
        return {QStringLiteral("WAV"), f->audioProperties() ? f->audioProperties()->bitsPerSample() : 0};
    if (auto *f = dynamic_cast<RIFF::AIFF::File *>(file))
        return {QStringLiteral("AIFF"), f->audioProperties() ? f->audioProperties()->bitsPerSample() : 0};
    if (auto *f = dynamic_cast<MP4::File *>(file)) {
        const auto *p = f->audioProperties();
        if (p && p->codec() == MP4::Properties::ALAC)
            return {QStringLiteral("ALAC"), p->bitsPerSample()};
        if (p && p->codec() == MP4::Properties::AAC)
            return {QStringLiteral("AAC"), 0};
        return {QStringLiteral("MP4"), 0};
    }
    if (dynamic_cast<Ogg::Vorbis::File *>(file))
        return {QStringLiteral("Vorbis"), 0};
    if (dynamic_cast<Ogg::Opus::File *>(file))
        return {QStringLiteral("Opus"), 0};
    return {};
}

EmbeddedPicture toPicture(const TagLib::VariantMap &m)
{
    EmbeddedPicture p;
    const TagLib::ByteVector data = m.value("data").toByteVector();
    p.data = QByteArray(data.data(), static_cast<qsizetype>(data.size()));
    p.mimeType = toQString(m.value("mimeType").toString());
    p.pictureType = toQString(m.value("pictureType").toString());
    p.description = toQString(m.value("description").toString());
    return p;
}

} // namespace

std::pair<int, int> TagReader::parseNumberPair(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*(\d+)\s*(?:/\s*(\d+)\s*)?$)"));
    const auto m = re.match(text);
    if (!m.hasMatch())
        return {0, 0};
    return {m.captured(1).toInt(), m.captured(2).toInt()}; // empty capture → 0
}

int TagReader::parseYear(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*(\d{4})(?:\D|$))"));
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

std::optional<double> TagReader::parseGain(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*([+-]?\d+(?:\.\d+)?)\s*(?:dB)?\s*$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(text);
    if (!m.hasMatch())
        return std::nullopt;
    bool ok = false;
    const double v = m.captured(1).toDouble(&ok);
    return ok ? std::optional<double>(v) : std::nullopt;
}

std::optional<TrackTags> TagReader::read(const QString &path, QString *error)
{
    const QByteArray name = encodedPath(path);
    TagLib::FileRef ref(name.constData(), true, TagLib::AudioProperties::Average);
    if (ref.isNull() || !ref.file()->isValid()) {
        if (error)
            *error = QFileInfo::exists(path) ? QStringLiteral("not a supported audio file")
                                             : QStringLiteral("file not found");
        return std::nullopt;
    }

    // TagLib accepts any file whose container it can parse (e.g. junk named .mp3);
    // require an actual audio stream.
    const auto *ap = ref.audioProperties();
    if (!ap || ap->sampleRate() <= 0) {
        if (error)
            *error = QStringLiteral("no audio stream found");
        return std::nullopt;
    }

    TrackTags t;
    const Props p(ref.properties());

    t.title = p.joined("TITLE", QStringLiteral(" "));
    t.artist = p.joined("ARTIST", QStringLiteral(", "));
    t.album = p.joined("ALBUM", QStringLiteral(" "));
    t.albumArtist = p.joined("ALBUMARTIST", QStringLiteral(", "));
    t.genre = p.first({"GENRE"});
    t.composer = p.joined("COMPOSER", QStringLiteral(", "));
    t.comment = p.first({"COMMENT"});
    t.year = parseYear(p.first({"DATE", "YEAR", "ORIGINALDATE"}));

    std::tie(t.trackNumber, t.trackTotal) = parseNumberPair(p.first({"TRACKNUMBER"}));
    std::tie(t.discNumber, t.discTotal) = parseNumberPair(p.first({"DISCNUMBER"}));
    if (t.trackTotal == 0)
        t.trackTotal = parseNumberPair(p.first({"TRACKTOTAL", "TOTALTRACKS"})).first;
    if (t.discTotal == 0)
        t.discTotal = parseNumberPair(p.first({"DISCTOTAL", "TOTALDISCS"})).first;

    const QString comp = p.first({"COMPILATION"});
    t.compilation = comp == QLatin1String("1") || comp.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;

    t.replayGainTrackGain = parseGain(p.first({"REPLAYGAIN_TRACK_GAIN"}));
    t.replayGainTrackPeak = parseGain(p.first({"REPLAYGAIN_TRACK_PEAK"}));
    t.replayGainAlbumGain = parseGain(p.first({"REPLAYGAIN_ALBUM_GAIN"}));
    t.replayGainAlbumPeak = parseGain(p.first({"REPLAYGAIN_ALBUM_PEAK"}));

    t.musicBrainzAlbumId = p.first({"MUSICBRAINZ_ALBUMID"});
    t.musicBrainzAlbumArtistId = p.first({"MUSICBRAINZ_ALBUMARTISTID"});
    t.musicBrainzTrackId = p.first({"MUSICBRAINZ_TRACKID"});

    t.durationMs = ap->lengthInMilliseconds();
    t.bitrateKbps = ap->bitrate();
    t.sampleRate = ap->sampleRate();
    t.channels = ap->channels();
    const CodecInfo ci = codecOf(ref.file());
    t.codec = ci.codec;
    t.bitsPerSample = ci.bitsPerSample;

    t.hasEmbeddedArt = ref.complexPropertyKeys().contains("PICTURE");
    return t;
}

QList<EmbeddedPicture> TagReader::readPictures(const QString &path)
{
    const QByteArray name = encodedPath(path);
    TagLib::FileRef ref(name.constData(), false);
    QList<EmbeddedPicture> out;
    if (ref.isNull())
        return out;
    for (const auto &m : ref.complexProperties("PICTURE")) {
        EmbeddedPicture pic = toPicture(m);
        if (!pic.data.isEmpty())
            out << std::move(pic);
    }
    return out;
}

std::optional<EmbeddedPicture> TagReader::readCover(const QString &path)
{
    QList<EmbeddedPicture> pics = readPictures(path);
    if (pics.isEmpty())
        return std::nullopt;
    for (auto &p : pics) {
        if (p.pictureType == QLatin1String("Front Cover"))
            return std::move(p);
    }
    return std::move(pics.front());
}

} // namespace mmm
