// SPDX-License-Identifier: GPL-3.0-or-later
#include "transcoder.h"

#include "artshrinker.h"
#include "gstinit.h"
#include "tagreader.h"
#include "tagwriter.h"

#include <QFile>
#include <QLoggingCategory>

#include <gst/gst.h>

#include <memory>

Q_DECLARE_LOGGING_CATEGORY(lcSync)

namespace mmm {

namespace {

struct GstObjectUnref
{
    void operator()(gpointer p) const { gst_object_unref(p); }
};
using ElementPtr = std::unique_ptr<GstElement, GstObjectUnref>;
using BusPtr = std::unique_ptr<GstBus, GstObjectUnref>;

QString mimeTypeOf(const QByteArray &image)
{
    return image.startsWith("\x89PNG") ? QStringLiteral("image/png") : QStringLiteral("image/jpeg");
}

bool canceled(const std::atomic_bool *cancel)
{
    return cancel && cancel->load();
}

} // namespace

bool Transcoder::transcodeToMp3(const QString &source, const QString &dest, TranscodeTarget target,
                                const std::atomic_bool *cancel, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        QFile::remove(dest);
        return false;
    };
    gst::ensureInitialized();
    // xingmux adds the VBR header players need for the right duration and seeking.
    static const bool haveXing = [] {
        GstElementFactory *f = gst_element_factory_find("xingmux");
        if (f)
            gst_object_unref(f);
        return f != nullptr;
    }();
    const QString encoder = target == TranscodeTarget::Mp3Cbr320
        ? QStringLiteral("lamemp3enc target=bitrate bitrate=320 cbr=true")
        : QStringLiteral("lamemp3enc target=quality quality=0");
    // decodebin's audio pad is linked when it appears (gst_parse_launch's delayed linking);
    // audioconvert/audioresample bring any channel layout and rate to what LAME takes.
    const QString desc = QStringLiteral("filesrc name=src ! decodebin ! audioconvert ! audioresample ! %1 %2 "
                                        "! filesink name=sink")
                             .arg(encoder, haveXing ? QStringLiteral("! xingmux") : QString());
    GError *err = nullptr;
    ElementPtr pipeline(gst_parse_launch(desc.toUtf8().constData(), &err));
    if (!pipeline) {
        const QString message = QString::fromUtf8(err ? err->message : "cannot build the transcode pipeline");
        g_clear_error(&err);
        return fail(message);
    }
    g_clear_error(&err);
    {
        ElementPtr src(gst_bin_get_by_name(GST_BIN(pipeline.get()), "src"));
        ElementPtr sink(gst_bin_get_by_name(GST_BIN(pipeline.get()), "sink"));
        g_object_set(src.get(), "location", QFile::encodeName(source).constData(), nullptr);
        g_object_set(sink.get(), "location", QFile::encodeName(dest).constData(), nullptr);
    }

    QString result;
    bool stopped = false;
    if (gst_element_set_state(pipeline.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        result = QStringLiteral("cannot start transcoding %1").arg(source);
    } else {
        const BusPtr bus(gst_element_get_bus(pipeline.get()));
        while (true) {
            if (canceled(cancel)) {
                stopped = true;
                break;
            }
            GstMessage *msg = gst_bus_timed_pop_filtered(
                bus.get(), 100 * GST_MSECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
            if (!msg)
                continue;
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                GError *e = nullptr;
                gst_message_parse_error(msg, &e, nullptr);
                result = QStringLiteral("cannot transcode %1: %2").arg(source, QString::fromUtf8(e ? e->message : "error"));
                g_clear_error(&e);
            }
            gst_message_unref(msg);
            break;
        }
    }
    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
    if (stopped)
        return fail(QStringLiteral("canceled"));
    if (!result.isEmpty())
        return fail(result);
    return true;
}

bool Transcoder::prepare(const Request &r, const std::atomic_bool *cancel, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        QFile::remove(r.destPath);
        return false;
    };
    QFile::remove(r.destPath);
    if (r.transcode) {
        if (!transcodeToMp3(r.sourcePath, r.destPath, r.target, cancel, error))
            return false;
    } else {
        QFile source(r.sourcePath);
        if (!source.copy(r.destPath))
            return fail(QStringLiteral("cannot copy %1: %2").arg(r.sourcePath, source.errorString()));
        // A copy of a read-only library file is read-only too: TagLib must write it.
        QFile::setPermissions(r.destPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    }
    if (canceled(cancel))
        return fail(QStringLiteral("canceled"));

    // Art: Keep (transcodes only: a plain copy never gets here with Keep) keeps the file's
    // embedded art, or embeds the folder art if it has none; Shrink500 resizes the folder art
    // (it beats embedded art, decisions/0003) or the embedded one; Strip removes all.
    TagWriter::DeviceArt art = TagWriter::DeviceArt::Keep;
    EmbeddedPicture cover;
    if (r.art == ArtworkPolicy::Strip) {
        art = TagWriter::DeviceArt::Strip;
    } else {
        QByteArray image;
        if (!r.artPath.isEmpty()) {
            QFile f(r.artPath);
            if (f.open(QIODevice::ReadOnly))
                image = f.readAll();
        }
        bool folderArt = !image.isEmpty();
        // Keep means the file's own art: folder art only fills in for a file that has none.
        if (folderArt && r.art == ArtworkPolicy::Keep && TagReader::readCover(r.sourcePath)) {
            image.clear();
            folderArt = false;
        }
        if (!folderArt && r.art == ArtworkPolicy::Shrink500) {
            if (const auto embedded = TagReader::readCover(r.sourcePath))
                image = embedded->data;
        }
        if (!image.isEmpty() && r.art == ArtworkPolicy::Shrink500) {
            QString why;
            if (const auto small = ArtShrinker::shrink(image, ArtShrinker::kMaxSide, &why)) {
                image = *small;
            } else {
                qCWarning(lcSync).noquote() << "keeping the original art of" << r.sourcePath << ":" << why;
            }
        }
        if (!image.isEmpty() && (folderArt || r.art == ArtworkPolicy::Shrink500)) {
            art = TagWriter::DeviceArt::Replace;
            cover = EmbeddedPicture{image, mimeTypeOf(image), QStringLiteral("Front Cover"), QString()};
        }
    }
    QString why;
    if (!TagWriter::writeDeviceTags(r.destPath, r.transcode ? r.sourcePath : QString(), art, cover, &why))
        return fail(why);
    return true;
}

} // namespace mmm
