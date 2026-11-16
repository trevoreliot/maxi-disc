// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace mmm {

struct EmbeddedPicture
{
    QByteArray data;
    QString mimeType;     // e.g. "image/jpeg"
    QString pictureType;  // ID3v2 naming, e.g. "Front Cover"
    QString description;
};

// Everything we read from one audio file: tags plus audio properties.
// Empty string / 0 means "not present".
struct TrackTags
{
    // --- Tags ---
    QString title;
    QString artist;       // multiple values joined with ", "
    QString album;
    QString albumArtist;
    QString genre;        // first genre only (v1)
    QString composer;
    QString comment;
    int year = 0;
    int trackNumber = 0;
    int trackTotal = 0;
    int discNumber = 0;
    int discTotal = 0;
    bool compilation = false;

    std::optional<double> replayGainTrackGain; // dB
    std::optional<double> replayGainTrackPeak; // linear
    std::optional<double> replayGainAlbumGain;
    std::optional<double> replayGainAlbumPeak;

    QString musicBrainzAlbumId;       // release MBID, used for artwork lookup (M7)
    QString musicBrainzAlbumArtistId;
    QString musicBrainzTrackId;       // recording MBID

    // --- Audio properties ---
    QString codec;        // "FLAC", "MP3", "WAV", "AIFF", "ALAC", "AAC", "Vorbis", "Opus", or "Unknown"
    qint64 durationMs = 0;
    int bitrateKbps = 0;
    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0; // 0 for lossy formats

    bool hasEmbeddedArt = false;

    // Album artist if set, otherwise artist: what the library groups albums under.
    QString effectiveAlbumArtist() const { return albumArtist.isEmpty() ? artist : albumArtist; }
};

} // namespace mmm
