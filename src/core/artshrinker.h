// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>

#include <optional>

namespace mmm {

// Cover art for small players (M9, ArtworkPolicy::Shrink500): at most `maxSide` pixels on
// the longer side, as a baseline JPEG (many DAPs can't decode progressive JPEG or PNG).
// Decodes and scales with GStreamer, since the core has no QtGui. Thread-safe.
class ArtShrinker
{
public:
    static constexpr int kMaxSide = 500;

    // The JPEG to embed: `image` itself if it's already a baseline JPEG that fits, else a
    // re-encoded one (never upscaled). nullopt (+ `error`) if the image can't be decoded.
    static std::optional<QByteArray> shrink(const QByteArray &image, int maxSide = kMaxSide,
                                            QString *error = nullptr);

    // Pixel size from a JPEG or PNG header; nullopt for anything else or garbage.
    static std::optional<QSize> imageSize(const QByteArray &image);
    // A JPEG whose frame is progressive (SOF2): what shrink() always re-encodes.
    static bool isProgressiveJpeg(const QByteArray &image);
};

} // namespace mmm
