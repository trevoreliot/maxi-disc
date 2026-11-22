// SPDX-License-Identifier: GPL-3.0-or-later
#include "artshrinker.h"

#include "gstinit.h"

#include <gst/gst.h>

#include <cmath>
#include <memory>

namespace mmm {

namespace {

struct GstObjectUnref
{
    void operator()(gpointer p) const { gst_object_unref(p); }
};
using ElementPtr = std::unique_ptr<GstElement, GstObjectUnref>;
using BusPtr = std::unique_ptr<GstBus, GstObjectUnref>;

quint32 be32(const QByteArray &d, qsizetype at)
{
    return (quint32(quint8(d[at])) << 24) | (quint32(quint8(d[at + 1])) << 16) | (quint32(quint8(d[at + 2])) << 8)
        | quint32(quint8(d[at + 3]));
}
int be16(const QByteArray &d, qsizetype at)
{
    return (int(quint8(d[at])) << 8) | int(quint8(d[at + 1]));
}

bool isJpeg(const QByteArray &d)
{
    return d.size() > 4 && quint8(d[0]) == 0xFF && quint8(d[1]) == 0xD8;
}

// Walks a JPEG's marker segments up to the first start-of-frame: {marker, width, height}.
struct JpegFrame
{
    int marker = 0;
    int width = 0;
    int height = 0;
};
std::optional<JpegFrame> jpegFrame(const QByteArray &d)
{
    if (!isJpeg(d))
        return std::nullopt;
    qsizetype i = 2;
    while (i + 4 <= d.size()) {
        if (quint8(d[i]) != 0xFF)
            return std::nullopt;
        const int marker = quint8(d[i + 1]);
        if (marker == 0xFF) { // fill byte
            ++i;
            continue;
        }
        if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7)) {
            i += 2;
            continue;
        }
        const int length = be16(d, i + 2);
        // SOF0..SOF15, except DHT (C4), JPG (C8) and DAC (CC).
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            if (i + 9 > d.size())
                return std::nullopt;
            return JpegFrame{marker, be16(d, i + 7), be16(d, i + 5)};
        }
        if (marker == 0xDA || length < 2)
            return std::nullopt; // scan data before any frame header: broken
        i += 2 + length;
    }
    return std::nullopt;
}

} // namespace

std::optional<QSize> ArtShrinker::imageSize(const QByteArray &d)
{
    static const QByteArray png("\x89PNG\r\n\x1a\n", 8);
    if (d.startsWith(png) && d.size() >= 24 && d.mid(12, 4) == "IHDR")
        return QSize(static_cast<int>(be32(d, 16)), static_cast<int>(be32(d, 20)));
    if (const auto f = jpegFrame(d); f && f->width > 0 && f->height > 0)
        return QSize(f->width, f->height);
    return std::nullopt;
}

bool ArtShrinker::isProgressiveJpeg(const QByteArray &image)
{
    const auto f = jpegFrame(image);
    return f && (f->marker == 0xC2 || f->marker == 0xC6 || f->marker == 0xCA || f->marker == 0xCE);
}

std::optional<QByteArray> ArtShrinker::shrink(const QByteArray &image, int maxSide, QString *error)
{
    auto fail = [error](const QString &message) -> std::optional<QByteArray> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    const auto size = imageSize(image);
    if (!size || size->isEmpty())
        return fail(QStringLiteral("not a JPEG or PNG image"));
    const int longest = std::max(size->width(), size->height());
    if (longest <= maxSide && isJpeg(image) && !isProgressiveJpeg(image))
        return image;

    const double scale = longest > maxSide ? double(maxSide) / double(longest) : 1.0;
    const int width = std::max(1, static_cast<int>(std::lround(size->width() * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(size->height() * scale)));

    gst::ensureInitialized();
    // decodebin's pad appears once the data is typefound: gst_parse_launch links it then.
    const QString desc = QStringLiteral("appsrc name=src ! decodebin ! videoconvert ! videoscale add-borders=false "
                                        "! video/x-raw,width=%1,height=%2,pixel-aspect-ratio=1/1 ! videoconvert "
                                        "! video/x-raw,format=I420 ! jpegenc quality=88 ! appsink name=sink sync=false")
                             .arg(width)
                             .arg(height);
    GError *err = nullptr;
    ElementPtr pipeline(gst_parse_launch(desc.toUtf8().constData(), &err));
    if (!pipeline) {
        const QString message = QString::fromUtf8(err ? err->message : "cannot build the image pipeline");
        g_clear_error(&err);
        return fail(message);
    }
    g_clear_error(&err);
    ElementPtr src(gst_bin_get_by_name(GST_BIN(pipeline.get()), "src"));
    ElementPtr sink(gst_bin_get_by_name(GST_BIN(pipeline.get()), "sink"));

    if (gst_element_set_state(pipeline.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        gst_element_set_state(pipeline.get(), GST_STATE_NULL);
        return fail(QStringLiteral("cannot start the image pipeline"));
    }
    // The whole image as one buffer, then end of stream (appsrc's action signals: no
    // gstreamer-app library needed).
    GstBuffer *buffer = gst_buffer_new_memdup(image.constData(), static_cast<gsize>(image.size()));
    GstFlowReturn flow = GST_FLOW_OK;
    g_signal_emit_by_name(src.get(), "push-buffer", buffer, &flow);
    gst_buffer_unref(buffer);
    g_signal_emit_by_name(src.get(), "end-of-stream", &flow);

    // Wait for the encoded picture, an error or the end of the stream (a broken image ends in
    // an error, not a sample: don't sit out the whole timeout).
    const BusPtr bus(gst_element_get_bus(pipeline.get()));
    QByteArray out;
    QString busError;
    bool ended = false;
    for (int waited = 0; out.isEmpty() && busError.isEmpty() && waited < 10000; waited += 50) {
        GstSample *sample = nullptr;
        // After EOS the sample (if any) is already queued in appsink: one last look.
        g_signal_emit_by_name(sink.get(), "try-pull-sample", static_cast<GstClockTime>(ended ? 0 : 50 * GST_MSECOND),
                              &sample);
        if (sample) {
            if (GstBuffer *b = gst_sample_get_buffer(sample)) {
                GstMapInfo map;
                if (gst_buffer_map(b, &map, GST_MAP_READ)) {
                    out = QByteArray(reinterpret_cast<const char *>(map.data), static_cast<qsizetype>(map.size));
                    gst_buffer_unmap(b, &map);
                }
            }
            gst_sample_unref(sample);
            break;
        }
        if (ended) {
            busError = QStringLiteral("the image could not be decoded");
            break;
        }
        GstMessage *m = gst_bus_pop_filtered(bus.get(), static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        if (!m)
            continue;
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
            GError *e = nullptr;
            gst_message_parse_error(m, &e, nullptr);
            busError = QString::fromUtf8(e ? e->message : "decode error");
            g_clear_error(&e);
        } else {
            ended = true;
        }
        gst_message_unref(m);
    }
    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
    if (out.isEmpty())
        return fail(busError.isEmpty() ? QStringLiteral("the image could not be decoded") : busError);
    return out;
}

} // namespace mmm
