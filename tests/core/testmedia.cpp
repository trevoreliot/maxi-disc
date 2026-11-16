// SPDX-License-Identifier: GPL-3.0-or-later
#include "testmedia.h"

#include "gstinit.h"

#include <gst/gst.h>

namespace mmm::test {

QString generateTone(const QString &path, const QString &encoder, int durationMs, const QString &tags)
{
    gst::ensureInitialized();

    constexpr int kRate = 44100;
    constexpr int kSamplesPerBuffer = 441; // 10 ms
    const int buffers = durationMs / 10;

    const QString desc =
        QStringLiteral("audiotestsrc wave=sine freq=440 volume=0.3 num-buffers=%1 samplesperbuffer=%2 "
                       "! audio/x-raw,rate=%3,channels=2 ! taginject name=tagger "
                       "! audioconvert ! audioresample ! %4 ! filesink name=sink")
            .arg(buffers)
            .arg(kSamplesPerBuffer)
            .arg(kRate)
            .arg(encoder);

    GError *err = nullptr;
    GstElement *pipeline = gst_parse_launch(desc.toUtf8().constData(), &err);
    if (!pipeline) {
        QString msg = QString::fromUtf8(err ? err->message : "parse failed");
        g_clear_error(&err);
        return msg;
    }
    g_clear_error(&err); // non-fatal warnings

    // Set path and tags as properties: no gst-launch quoting/escaping issues.
    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    g_object_set(sink, "location", path.toUtf8().constData(), nullptr);
    gst_object_unref(sink);
    if (!tags.isEmpty()) {
        GstElement *tagger = gst_bin_get_by_name(GST_BIN(pipeline), "tagger");
        g_object_set(tagger, "tags", tags.toUtf8().constData(), nullptr);
        gst_object_unref(tagger);
    }

    QString result;
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg = gst_bus_timed_pop_filtered(
        bus, 30 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    if (!msg) {
        result = QStringLiteral("timed out generating %1").arg(path);
    } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
        GError *e = nullptr;
        gst_message_parse_error(msg, &e, nullptr);
        result = QString::fromUtf8(e ? e->message : "error");
        g_clear_error(&e);
    }
    if (msg)
        gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return result;
}

} // namespace mmm::test
