// SPDX-License-Identifier: GPL-3.0-or-later
#include "gstpcmencoder.h"

#include "gstinit.h"

#include <QFile>
#include <QFileInfo>

#include <gst/gst.h>

#include <memory>

namespace mmm {

namespace {

struct GstObjectUnref
{
    void operator()(gpointer p) const { gst_object_unref(p); }
};
using ElementPtr = std::unique_ptr<GstElement, GstObjectUnref>;
using BusPtr = std::unique_ptr<GstBus, GstObjectUnref>;

} // namespace

bool GstPcmEncoder::decode(const QString &source, const QString &destination, Output output,
                           const std::atomic_bool &cancel, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        QFile::remove(destination);
        return false;
    };
    if (!QFileInfo(source).isFile())
        return fail(QStringLiteral("file not found: %1").arg(source));
    gst::ensureInitialized();
    // decodebin's audio pad is linked when it appears (gst_parse_launch's delayed linking);
    // an embedded cover image has no audio caps, so it never links to audioconvert.
    const QString desc = output == Output::RawBigEndian
        ? QStringLiteral("filesrc name=src ! decodebin ! audioconvert ! audioresample "
                         "! audio/x-raw,format=S16BE,rate=44100,channels=2,layout=interleaved "
                         "! filesink name=sink")
        : QStringLiteral("filesrc name=src ! decodebin ! audioconvert ! audioresample "
                         "! audio/x-raw,format=S16LE,rate=44100,channels=2,layout=interleaved "
                         "! wavenc ! filesink name=sink");
    GError *err = nullptr;
    ElementPtr pipeline(gst_parse_launch(desc.toUtf8().constData(), &err));
    if (!pipeline) {
        const QString message = QString::fromUtf8(err ? err->message : "cannot build the decoder");
        g_clear_error(&err);
        return fail(message);
    }
    g_clear_error(&err);
    {
        ElementPtr src(gst_bin_get_by_name(GST_BIN(pipeline.get()), "src"));
        ElementPtr sink(gst_bin_get_by_name(GST_BIN(pipeline.get()), "sink"));
        g_object_set(src.get(), "location", QFile::encodeName(source).constData(), nullptr);
        g_object_set(sink.get(), "location", QFile::encodeName(destination).constData(), nullptr);
    }

    QString problem;
    bool stopped = false;
    if (gst_element_set_state(pipeline.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        problem = QStringLiteral("cannot decode %1").arg(source);
    } else {
        const BusPtr bus(gst_element_get_bus(pipeline.get()));
        while (true) {
            if (cancel.load()) {
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
                problem = QStringLiteral("cannot decode %1: %2").arg(source, QString::fromUtf8(e ? e->message : "error"));
                g_clear_error(&e);
            }
            gst_message_unref(msg);
            break;
        }
    }
    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
    if (stopped)
        return fail(kEncodeCanceled);
    if (!problem.isEmpty())
        return fail(problem);
    return true;
}

bool GstPcmEncoder::encode(const QString &source, MdRecordMode mode, const QString &destination,
                           const std::atomic_bool &cancel, MdEncodedAudio *out, QString *error)
{
    if (mode != MdRecordMode::Sp) {
        if (error)
            *error = QStringLiteral("PCM is for SP only");
        QFile::remove(destination);
        return false;
    }
    if (!decode(source, destination, Output::RawBigEndian, cancel, error))
        return false;
    const qint64 bytes = QFileInfo(destination).size();
    if (bytes <= 0 || bytes % 4 != 0) {
        if (error)
            *error = QStringLiteral("no audio decoded from %1").arg(source);
        QFile::remove(destination);
        return false;
    }
    if (out) {
        out->mode = mode;
        out->path = destination;
        out->bytes = bytes;
        out->durationMs = bytes * 1000 / kBytesPerSecond;
    }
    return true;
}

} // namespace mmm
