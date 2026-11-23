// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "tracktags.h"

#include <QList>
#include <QString>

#include <optional>

namespace mmm {

// Reads tags, audio properties and embedded pictures with TagLib.
// Stateless and thread-safe: safe to call from QThreadPool workers.
class TagReader
{
public:
    // Returns nullopt if the file can't be opened or isn't a supported audio file;
    // `error` (optional) receives a reason.
    static std::optional<TrackTags> read(const QString &path, QString *error = nullptr);

    // All embedded pictures, in file order.
    static QList<EmbeddedPicture> readPictures(const QString &path);

    // The front cover if there is one, otherwise the first picture.
    // (MP4 stores no picture types, so for .m4a this is always the first picture.)
    static std::optional<EmbeddedPicture> readCover(const QString &path);

    // Sends TagLib's own messages ("Duplicate ID3v2 tags found", ...) to the mmm.taglib
    // logging category (debug level: hidden unless QT_LOGGING_RULES="mmm.taglib.debug=true")
    // instead of stderr. Called by TagReader and TagWriter; idempotent and thread-safe.
    static void routeTagLibMessages();

    // --- Parsing helpers (exposed for tests) ---
    // "3", "3/12", " 03 / 12 " → {3, 12}; garbage → {0, 0}.
    static std::pair<int, int> parseNumberPair(const QString &text);
    // "2019", "2019-05-03", "2019/05" → 2019; otherwise 0.
    static int parseYear(const QString &text);
    // "-6.50 dB", "+1.2", "0.98765" → value; otherwise nullopt.
    static std::optional<double> parseGain(const QString &text);
};

} // namespace mmm
